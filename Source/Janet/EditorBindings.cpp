#include "EditorBindings.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/CodeFoldSettings.h"
#include "Editor/FinalNewline.h"
#include "Editor/FormatOnSave.h"
#include "Editor/Link.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ScratchPad.h"
#include "Editor/ScriptingSession.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/TabWidth.h"
#include "Value.h"

namespace ned::janet {

namespace {

    // Set once by InstallEditorBindings; there is exactly one Environment per
    // process (see Environment.h), so a plain static is sufficient here -- this
    // is narrower and Janet-specific, unlike ScriptingSession's registry/keymap
    // bridge, which is why it lives in this file rather than in ScriptingSession
    // itself.
    JanetTable* g_env = nullptr;

    editor::CommandContext& CurrentContext() {
        editor::ScriptingSession& session = editor::ScriptingSessionScope::Current();
        if (!session.context) {
            throw std::runtime_error("ned: no active command context");
        }
        return *session.context;
    }

    void NedInsert(std::string text) {
        CurrentContext().buffer.InsertAtPoint(text);
    }

    void NedForwardChar() {
        CurrentContext().buffer.MoveForward();
    }

    void NedBackwardChar() {
        CurrentContext().buffer.MoveBackward();
    }

    void NedDeleteChar() {
        CurrentContext().buffer.DeleteForwardAtPoint();
    }

    void NedBackwardDeleteChar() {
        CurrentContext().buffer.DeleteBackwardAtPoint();
    }

    std::size_t NedPoint() {
        return CurrentContext().buffer.Point();
    }

    std::string NedBufferText() {
        return CurrentContext().buffer.Text();
    }

    void NedMessage(std::string text) {
        editor::CommandContext& context = CurrentContext();
        if (context.message) {
            *context.message = std::move(text);
        }
    }

    // Registers fn (a Janet function value) as a named editor command. fn is
    // bound into the environment under a generated name via janet_def rather
    // than held as a RootedValue -- see the CAUTION comment on RootedValue in
    // Value.h for why: invoking a rooted function later via janet_pcall is the
    // specific combination that corrupts state in this Janet build. Routing
    // invocation through janet_dostring on the generated name sidesteps that
    // entirely and has been stress-tested (many commands, GC pressure,
    // redefinition) without issue.
    //
    // Precondition: name should be a valid Janet symbol fragment (no spaces or
    // parens) -- it's spliced directly into a generated form. Command names are
    // conventionally kebab-case identifiers anyway; a malformed name fails
    // clearly (a Janet parse error) the first time the command runs, not silently.
    void NedRegisterCommand(std::string name, std::string docstring, Janet fn) {
        editor::ScriptingSession& session = editor::ScriptingSessionScope::Current();

        if (!g_env) {
            throw std::runtime_error("ned: janet environment not installed");
        }

        const std::string internalName = "ned/janet-command-" + name;
        janet_def(g_env, internalName.c_str(), fn, docstring.c_str());

        const std::string invokeExpr = "(" + internalName + ")";
        JanetTable*       env        = g_env;

        session.registry.Register(name, docstring, [env, invokeExpr](editor::CommandContext& context) {
            editor::CommandContextScope contextScope(context);

            Janet     out;
            const int signal = janet_dostring(env, invokeExpr.c_str(), "ned-command", &out);
            if (signal != 0) {
                throw std::runtime_error("ned: error running janet command (see stderr for details)");
            }
        });
    }

    void NedDefineKey(std::string keySequence, std::string commandName) {
        editor::ScriptingSession& session = editor::ScriptingSessionScope::Current();
        session.scriptKeymap.Bind(editor::ParseKeySequence(keySequence), commandName);
    }

    // Empty string clears it (save-buffer stops formatting) -- simpler than a
    // separate nil/optional case for Janet callers to handle, and matches
    // RunFormatCommand's own "empty command means nothing to run" convention.
    void NedSetFormatCommand(std::string command) {
        editor::SetFormatCommand(command.empty() ? std::nullopt : std::optional<std::string>(std::move(command)));
    }

    void NedSetUrlOpenCommand(std::string command) {
        editor::link::SetUrlOpenCommand(command.empty() ? std::nullopt : std::optional<std::string>(std::move(command)));
    }

    void NedSetTabWidth(std::int64_t columns) {
        editor::SetTabWidth(static_cast<int>(columns));
    }

    void NedSetAutoDetectProjectRoot(bool enabled) {
        editor::SetAutoDetectProjectRoot(enabled);
    }

    void NedSetScratchAutoSave(bool enabled) {
        editor::SetScratchAutoSaveEnabled(enabled);
    }

    void NedSetEnsureFinalNewline(bool enabled) {
        editor::SetEnsureFinalNewline(enabled);
    }

    void NedSetCodeFoldingEnabled(bool enabled) {
        editor::SetCodeFoldingEnabled(enabled);
    }

    void NedRegisterLanguageGrammar(std::string name, std::string libraryPath, std::string queryPath) {
        editor::RegisterDynamicMode(name, libraryPath, queryPath);
    }

    void NedSetModeForExtension(std::string extension, std::string modeName) {
        editor::SetModeForExtension(extension, modeName);
    }

    void NedSetModeForFilename(std::string filename, std::string modeName) {
        editor::SetModeForFilename(filename, modeName);
    }

    // syntax-theme-overrides follow-up: "each themeable entry" (Comment,
    // DocComment, Keyword, ...) reachable from Janet through one generic,
    // class-name-string-keyed mechanism (Editor/SyntaxTheme.h) rather than
    // one-off bindings per SyntaxClass. SyntaxClassByName throws
    // std::runtime_error for an unrecognized name -- auto-converted to a
    // Janet panic by Register<Fn>, the same as every other binding here,
    // matching this codebase's "a bad call surfaces as a real error"
    // convention. The two color setters follow NedSetFormatCommand's own
    // "empty string clears" precedent just above; the four trait setters
    // take a raw Janet value (nil clears, true/false sets) since there's no
    // clean empty-string-style sentinel for a bool.
    void NedSetSyntaxForeground(std::string className, std::string hex) {
        editor::SetSyntaxForeground(editor::SyntaxClassByName(className),
                                    hex.empty() ? std::nullopt : std::optional<std::string>(std::move(hex)));
    }

    void NedSetSyntaxBackground(std::string className, std::string hex) {
        editor::SetSyntaxBackground(editor::SyntaxClassByName(className),
                                    hex.empty() ? std::nullopt : std::optional<std::string>(std::move(hex)));
    }

    std::optional<bool> JanetToOptionalBool(Janet value) {
        if (janet_checktype(value, JANET_NIL)) {
            return std::nullopt;
        }
        return FromJanet<bool>(value);
    }

    void NedSetSyntaxBold(std::string className, Janet value) {
        editor::SetSyntaxBold(editor::SyntaxClassByName(className), JanetToOptionalBool(value));
    }

    void NedSetSyntaxItalic(std::string className, Janet value) {
        editor::SetSyntaxItalic(editor::SyntaxClassByName(className), JanetToOptionalBool(value));
    }

    void NedSetSyntaxUnderlined(std::string className, Janet value) {
        editor::SetSyntaxUnderlined(editor::SyntaxClassByName(className), JanetToOptionalBool(value));
    }

    void NedSetSyntaxStrikethrough(std::string className, Janet value) {
        editor::SetSyntaxStrikethrough(editor::SyntaxClassByName(className), JanetToOptionalBool(value));
    }

    std::optional<std::string> NedSyntaxForeground(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).foreground;
    }

    std::optional<std::string> NedSyntaxBackground(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).background;
    }

    std::optional<bool> NedSyntaxBold(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).bold;
    }

    std::optional<bool> NedSyntaxItalic(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).italic;
    }

    std::optional<bool> NedSyntaxUnderlined(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).underlined;
    }

    std::optional<bool> NedSyntaxStrikethrough(std::string className) {
        return editor::SyntaxOverrideFor(editor::SyntaxClassByName(className)).strikethrough;
    }

    std::vector<std::string> NedSyntaxClasses() {
        return editor::SyntaxClassNames();
    }

} // namespace

void InstallEditorBindings(Environment& env) {
    g_env = env.Env();

    env.Register<&NedInsert>("ned", "insert", "Insert text at point.");
    env.Register<&NedForwardChar>("ned", "forward-char", "Move point forward one grapheme cluster.");
    env.Register<&NedBackwardChar>("ned", "backward-char", "Move point backward one grapheme cluster.");
    env.Register<&NedDeleteChar>("ned", "delete-char", "Delete the grapheme cluster at point.");
    env.Register<&NedBackwardDeleteChar>("ned", "backward-delete-char", "Delete the grapheme cluster before point.");
    env.Register<&NedPoint>("ned", "point", "Return the current point as a byte offset.");
    env.Register<&NedBufferText>("ned", "buffer-text", "Return the full text of the current buffer.");
    env.Register<&NedMessage>("ned", "message", "Show a status/echo-area message.");
    env.Register<&NedRegisterCommand>("ned", "register-command", "Register a Janet function as a named, bindable command.");
    env.Register<&NedDefineKey>("ned", "define-key", "Bind a key sequence (e.g. \"C-c C-j\") to a command name.");
    env.Register<&NedSetFormatCommand>(
        "ned", "set-format-command",
        "Set the shell command save-buffer pipes buffer content through before writing (empty string clears it).");
    env.Register<&NedSetUrlOpenCommand>(
        "ned", "set-url-open-command",
        "Set the command open-link-at-point launches (as its own argument, never a shell string) to open a URL -- "
        "defaults to \"xdg-open\"; empty string clears it entirely, disabling URL-following.");
    env.Register<&NedSetTabWidth>("ned", "set-tab-width",
                                  "Set the display width (in columns) a tab character expands to (default 4).");
    env.Register<&NedSetAutoDetectProjectRoot>(
        "ned", "set-auto-detect-project-root",
        "Enable/disable walking upward from an opened file for a VCS marker directory to find the project root "
        "(default true).");
    env.Register<&NedSetScratchAutoSave>(
        "ned", "set-scratch-auto-save",
        "Enable/disable automatically saving modified scratch notes (find-scratch) on a periodic timer (default "
        "true).");
    env.Register<&NedSetEnsureFinalNewline>(
        "ned", "set-ensure-final-newline",
        "Enable/disable appending a trailing newline to a file's written content on save if it's missing one "
        "(default true).");
    env.Register<&NedSetCodeFoldingEnabled>(
        "ned", "set-code-folding-enabled",
        "Enable/disable the gutter code-folding affordance for modes with a fold query (default true).");
    env.Register<&NedRegisterLanguageGrammar>(
        "ned", "register-language-grammar",
        "Load a tree-sitter grammar at runtime: (name library-path query-path). library-path is a shared library "
        "exporting tree_sitter_<name>; query-path is a highlights.scm-style query file. Re-registering the same "
        "name replaces it. The registered name can then be used as the mode-name argument to "
        "ned/set-mode-for-extension or ned/set-mode-for-filename.");
    env.Register<&NedSetModeForExtension>(
        "ned", "set-mode-for-extension",
        "Map a file extension (with or without a leading '.') to a mode name -- either one registered via "
        "ned/register-language-grammar, or one of ned's own built-in mode names (e.g. \"php-mode\", \"python-mode\"). "
        "Checked before ned's own built-in extension table, so this can override a bundled mapping too, not just add "
        "a new one.");
    env.Register<&NedSetModeForFilename>(
        "ned", "set-mode-for-filename",
        "Map an exact, full filename (e.g. \"CMakeLists.txt\", not a pattern/glob) to a mode name, the same way "
        "ned/set-mode-for-extension does for an extension -- checked first, before any extension mapping, for files "
        "identified by name rather than by a distinguishing extension.");

    env.Register<&NedSetSyntaxForeground>(
        "ned", "set-syntax-foreground",
        "Override a syntax class's foreground color (e.g. \"comment\") as \"#rrggbb\" -- empty string clears the "
        "override. See ned/syntax-classes for every valid class name.");
    env.Register<&NedSetSyntaxBackground>(
        "ned", "set-syntax-background",
        "Override a syntax class's background color as \"#rrggbb\" -- empty string clears the override.");
    env.Register<&NedSetSyntaxBold>(
        "ned", "set-syntax-bold", "Override a syntax class's bold trait (true/false) -- nil clears the override.");
    env.Register<&NedSetSyntaxItalic>(
        "ned", "set-syntax-italic", "Override a syntax class's italic trait (true/false) -- nil clears the override.");
    env.Register<&NedSetSyntaxUnderlined>(
        "ned", "set-syntax-underlined",
        "Override a syntax class's underlined trait (true/false) -- nil clears the override.");
    env.Register<&NedSetSyntaxStrikethrough>(
        "ned", "set-syntax-strikethrough",
        "Override a syntax class's strikethrough trait (true/false) -- nil clears the override.");
    env.Register<&NedSyntaxForeground>(
        "ned", "syntax-foreground", "The syntax class's overridden foreground color, or nil if unset.");
    env.Register<&NedSyntaxBackground>(
        "ned", "syntax-background", "The syntax class's overridden background color, or nil if unset.");
    env.Register<&NedSyntaxBold>("ned", "syntax-bold", "The syntax class's overridden bold trait, or nil if unset.");
    env.Register<&NedSyntaxItalic>("ned", "syntax-italic", "The syntax class's overridden italic trait, or nil if unset.");
    env.Register<&NedSyntaxUnderlined>(
        "ned", "syntax-underlined", "The syntax class's overridden underlined trait, or nil if unset.");
    env.Register<&NedSyntaxStrikethrough>(
        "ned", "syntax-strikethrough", "The syntax class's overridden strikethrough trait, or nil if unset.");
    env.Register<&NedSyntaxClasses>("ned", "syntax-classes", "Every valid syntax class name, sorted.");
}

} // namespace ned::janet
