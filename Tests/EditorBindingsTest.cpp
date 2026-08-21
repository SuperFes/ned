#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/CodeFoldSettings.h"
#include "Editor/Dispatcher.h"
#include "Editor/FinalNewline.h"
#include "Editor/FormatOnSave.h"
#include "Editor/Key.h"
#include "Editor/Keymap.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ScratchPad.h"
#include "Editor/ScriptingSession.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/ThemeSetting.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/Value.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::janet::Environment;
using ned::janet::FromJanet;
using ned::janet::InstallEditorBindings;

namespace {

struct Fixture {
    ned::text::Buffer            buffer{"scratch"};
    ned::text::KillRing          killRing;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    ned::editor::Keymap          scriptKeymap;

    ned::editor::CommandContext Context() {
        return ned::editor::CommandContext{buffer, killRing, bufferList};
    }
};

} // namespace

TEST_CASE("ned/insert and point bindings operate on the active CommandContext", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});
    ned::editor::CommandContext        context = fixture.Context();
    ned::editor::CommandContextScope   contextScope(context);

    env.DoString(R"((ned/insert "hello"))");
    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(FromJanet<std::size_t>(env.DoString("(ned/point)")) == 5);

    env.DoString("(ned/backward-char)");
    REQUIRE(fixture.buffer.Point() == 4);

    REQUIRE(FromJanet<std::string>(env.DoString("(ned/buffer-text)")) == "hello");

    env.DoString("(ned/delete-char)");
    REQUIRE(fixture.buffer.Text() == "hell");

    env.DoString("(ned/backward-delete-char)");
    REQUIRE(fixture.buffer.Text() == "hel");
}

TEST_CASE("ned/message writes into the active CommandContext's message sink", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    std::string                 message;
    ned::editor::CommandContext context = fixture.Context();
    context.message                     = &message;
    ned::editor::CommandContextScope contextScope(context);

    env.DoString(R"((ned/message "hello from janet"))");
    REQUIRE(message == "hello from janet");
}

TEST_CASE("ned/* functions error when no command context is active", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});
    // deliberately no CommandContextScope active

    REQUIRE_THROWS_AS(env.DoString("(ned/point)"), std::runtime_error);
    REQUIRE_THROWS_AS(env.DoString(R"((ned/insert "x"))"), std::runtime_error);
}

TEST_CASE("ned/register-command makes a Janet function invocable through CommandRegistry", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"(
        (ned/register-command "janet-greet" "Greets."
          (fn [] (ned/insert "hi")))
    )");

    REQUIRE(fixture.registry.Find("janet-greet") != nullptr);

    ned::editor::CommandContext context = fixture.Context();
    fixture.registry.Invoke("janet-greet", context);

    REQUIRE(fixture.buffer.Text() == "hi");
}

TEST_CASE("Re-registering a Janet command under the same name replaces its behavior", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"((ned/register-command "redef-test" "" (fn [] (ned/insert "A"))))");
    env.DoString(R"((ned/register-command "redef-test" "" (fn [] (ned/insert "B"))))");

    ned::editor::CommandContext context = fixture.Context();
    fixture.registry.Invoke("redef-test", context);

    REQUIRE(fixture.buffer.Text() == "B");
}

TEST_CASE("Janet code can define a new command, bind it to a key, and a real Dispatcher fires it", "[EditorBindings]") {
    // The litmus test for "everything programmable": Janet defines a brand
    // new interactive command and binds it to a key sequence, with no C++
    // code involved beyond installing the bindings -- then a real Dispatcher,
    // fed real key chords, actually runs it and mutates a real buffer.
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"(
        (ned/register-command "insert-greeting" "Insert a greeting."
          (fn [] (ned/insert "hello from janet")))
        (ned/define-key "C-c C-j" "insert-greeting")
    )");

    const ned::editor::KeymapStack keymaps({&fixture.scriptKeymap});
    ned::editor::Dispatcher        dispatcher(fixture.registry, keymaps);
    ned::editor::CommandContext    context = fixture.Context();

    using ned::editor::ParseKeyChord;
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == ned::editor::Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-j"), context) == ned::editor::Dispatcher::Outcome::Invoked);

    REQUIRE(fixture.buffer.Text() == "hello from janet");
}

TEST_CASE("ned/set-format-command configures the process-wide format command", "[EditorBindings]") {
    // FormatCommand is process-wide state (see FormatOnSave.h); guaranteed
    // reset via RAII so this doesn't leak into other tests.
    struct FormatCommandGuard {
        ~FormatCommandGuard() {
            ned::editor::SetFormatCommand(std::nullopt);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-format-command "clang-format"))");
    REQUIRE(ned::editor::FormatCommand() == std::optional<std::string>("clang-format"));

    env.DoString(R"((ned/set-format-command ""))"); // empty string clears it
    REQUIRE_FALSE(ned::editor::FormatCommand().has_value());
}

TEST_CASE("ned/set-url-open-command configures the process-wide URL-open command", "[EditorBindings]") {
    // UrlOpenCommand is process-wide state (see Editor/Link.h) whose true
    // default is "xdg-open", not nullopt -- restores whatever was actually
    // configured before this test ran rather than unconditionally clearing
    // it, so this stays order-independent regardless of what ran before it.
    struct UrlOpenCommandGuard {
        std::optional<std::string> previous = ned::editor::link::UrlOpenCommand();
        ~UrlOpenCommandGuard() {
            ned::editor::link::SetUrlOpenCommand(previous);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-url-open-command "wslview"))");
    REQUIRE(ned::editor::link::UrlOpenCommand() == std::optional<std::string>("wslview"));

    env.DoString(R"((ned/set-url-open-command ""))"); // empty string clears it
    REQUIRE_FALSE(ned::editor::link::UrlOpenCommand().has_value());
}

TEST_CASE("ned/set-auto-detect-project-root configures the process-wide toggle", "[EditorBindings]") {
    // AutoDetectProjectRoot is process-wide state (see ProjectRoot.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct AutoDetectGuard {
        ~AutoDetectGuard() {
            ned::editor::SetAutoDetectProjectRoot(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-auto-detect-project-root false))");
    REQUIRE_FALSE(ned::editor::AutoDetectProjectRoot());

    env.DoString(R"((ned/set-auto-detect-project-root true))");
    REQUIRE(ned::editor::AutoDetectProjectRoot());
}

TEST_CASE("ned/set-scratch-auto-save configures the process-wide toggle", "[EditorBindings]") {
    // ScratchAutoSaveEnabled is process-wide state (see ScratchPad.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct AutoSaveGuard {
        ~AutoSaveGuard() {
            ned::editor::SetScratchAutoSaveEnabled(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-scratch-auto-save false))");
    REQUIRE_FALSE(ned::editor::ScratchAutoSaveEnabled());

    env.DoString(R"((ned/set-scratch-auto-save true))");
    REQUIRE(ned::editor::ScratchAutoSaveEnabled());
}

TEST_CASE("ned/set-ensure-final-newline configures the process-wide toggle", "[EditorBindings]") {
    // EnsureFinalNewline is process-wide state (see FinalNewline.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct FinalNewlineGuard {
        ~FinalNewlineGuard() {
            ned::editor::SetEnsureFinalNewline(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-ensure-final-newline false))");
    REQUIRE_FALSE(ned::editor::EnsureFinalNewline());

    env.DoString(R"((ned/set-ensure-final-newline true))");
    REQUIRE(ned::editor::EnsureFinalNewline());
}

TEST_CASE("ned/set-code-folding-enabled configures the process-wide toggle", "[EditorBindings]") {
    // CodeFoldingEnabled is process-wide state (see CodeFoldSettings.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct CodeFoldingGuard {
        ~CodeFoldingGuard() {
            ned::editor::SetCodeFoldingEnabled(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-code-folding-enabled false))");
    REQUIRE_FALSE(ned::editor::CodeFoldingEnabled());

    env.DoString(R"((ned/set-code-folding-enabled true))");
    REQUIRE(ned::editor::CodeFoldingEnabled());
}

TEST_CASE("ned/syntax-* bindings set/get/clear overrides, with nil for an unset field", "[EditorBindings]") {
    // SyntaxTheme overrides are process-wide state; guaranteed reset via RAII.
    struct SyntaxThemeGuard {
        ~SyntaxThemeGuard() {
            ned::editor::SetSyntaxForeground(ned::editor::SyntaxClass::Comment, std::nullopt);
            ned::editor::SetSyntaxBackground(ned::editor::SyntaxClass::Comment, std::nullopt);
            ned::editor::SetSyntaxBold(ned::editor::SyntaxClass::Comment, std::nullopt);
            ned::editor::SetSyntaxItalic(ned::editor::SyntaxClass::Comment, std::nullopt);
            ned::editor::SetSyntaxUnderlined(ned::editor::SyntaxClass::Comment, std::nullopt);
            ned::editor::SetSyntaxStrikethrough(ned::editor::SyntaxClass::Comment, std::nullopt);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    // Unset -- nil, not an error.
    REQUIRE(janet_checktype(env.DoString(R"((ned/syntax-foreground "comment"))"), JANET_NIL));
    REQUIRE(janet_checktype(env.DoString(R"((ned/syntax-bold "comment"))"), JANET_NIL));

    env.DoString(R"((ned/set-syntax-foreground "comment" "#5c6370"))");
    REQUIRE(FromJanet<std::string>(env.DoString(R"((ned/syntax-foreground "comment"))")) == "#5c6370");

    env.DoString(R"((ned/set-syntax-background "comment" "#282c34"))");
    REQUIRE(FromJanet<std::string>(env.DoString(R"((ned/syntax-background "comment"))")) == "#282c34");

    env.DoString(R"((ned/set-syntax-bold "comment" true))");
    REQUIRE(FromJanet<bool>(env.DoString(R"((ned/syntax-bold "comment"))")));

    env.DoString(R"((ned/set-syntax-italic "comment" false))");
    REQUIRE_FALSE(FromJanet<bool>(env.DoString(R"((ned/syntax-italic "comment"))")));

    env.DoString(R"((ned/set-syntax-underlined "comment" true))");
    REQUIRE(FromJanet<bool>(env.DoString(R"((ned/syntax-underlined "comment"))")));

    env.DoString(R"((ned/set-syntax-strikethrough "comment" true))");
    REQUIRE(FromJanet<bool>(env.DoString(R"((ned/syntax-strikethrough "comment"))")));

    // nil clears a trait override back to unset.
    env.DoString(R"((ned/set-syntax-bold "comment" nil))");
    REQUIRE(janet_checktype(env.DoString(R"((ned/syntax-bold "comment"))"), JANET_NIL));

    // Empty string clears a color override back to unset.
    env.DoString(R"((ned/set-syntax-foreground "comment" ""))");
    REQUIRE(janet_checktype(env.DoString(R"((ned/syntax-foreground "comment"))"), JANET_NIL));

    // An unrecognized class name is a real error, not nil.
    REQUIRE_THROWS_AS(env.DoString(R"((ned/set-syntax-foreground "not-a-real-class" "#ffffff"))"), std::runtime_error);
}

TEST_CASE("ned/syntax-classes lists every valid class name", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    const Janet result = env.DoString(R"((ned/syntax-classes))");
    REQUIRE(janet_checktype(result, JANET_ARRAY));
    const JanetArray* array = janet_unwrap_array(result);
    REQUIRE(array->count == static_cast<std::int32_t>(ned::editor::SyntaxClassNames().size()));
}

TEST_CASE("ned/set-lsp-command configures a per-language LSP server command", "[EditorBindings]") {
    struct LspCommandGuard {
        ~LspCommandGuard() {
            ned::editor::lsp::SetLspServerCommand("ned-editor-bindings-test-c", {});
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-lsp-command "ned-editor-bindings-test-c" ["clangd"]))");
    const auto command = ned::editor::lsp::LspServerCommand("ned-editor-bindings-test-c");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"clangd"});
}

TEST_CASE("ned/set-lsp-command accepts a real Janet array too, not just a tuple", "[EditorBindings]") {
    struct LspCommandGuard {
        ~LspCommandGuard() {
            ned::editor::lsp::SetLspServerCommand("ned-editor-bindings-test-py", {});
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-lsp-command "ned-editor-bindings-test-py" @["pyright-langserver" "--stdio"]))");
    const auto command = ned::editor::lsp::LspServerCommand("ned-editor-bindings-test-py");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"pyright-langserver", "--stdio"});
}

TEST_CASE("ned/set-lsp-command with an empty argv clears the configured command", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-lsp-command "ned-editor-bindings-test-clear" ["some-server"]))");
    REQUIRE(ned::editor::lsp::LspServerCommand("ned-editor-bindings-test-clear").has_value());

    env.DoString(R"((ned/set-lsp-command "ned-editor-bindings-test-clear" []))");
    REQUIRE_FALSE(ned::editor::lsp::LspServerCommand("ned-editor-bindings-test-clear").has_value());
}

// theme-editing follow-up: ned/theme-set is pure accumulation into the
// ThemeSetting override store -- interpretation (key validation, color
// parsing) happens in main.cpp via ui::SetThemeColorByKey, deliberately not
// here (see the binding's own comment).
TEST_CASE("ned/theme-set accumulates keyed overrides in insertion order", "[EditorBindings]") {
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    ned::editor::ClearThemeColorOverrides();

    env.DoString(R"((ned/theme-set "background" "#123456"))");
    env.DoString(R"((ned/theme-set "keyword_foreground" "x:4"))");
    env.DoString(R"((ned/theme-set "background" "#654321"))"); // later same-key call appends; application order makes it win

    const auto overrides = ned::editor::ThemeColorOverrides();
    REQUIRE(overrides.size() == 3);
    REQUIRE(overrides[0] == std::pair<std::string, std::string>{"background", "#123456"});
    REQUIRE(overrides[1] == std::pair<std::string, std::string>{"keyword_foreground", "x:4"});
    REQUIRE(overrides[2] == std::pair<std::string, std::string>{"background", "#654321"});

    ned::editor::ClearThemeColorOverrides();
}
