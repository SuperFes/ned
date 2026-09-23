#include "EditorBindings.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Editor/Acp/Config.h"
#include "Editor/Acp/PanelConfig.h"
#include "Editor/AutoFormatOnSave.h"
#include "Editor/AutoMerge.h"
#include "Editor/AutoPair.h"
#include "Editor/AutoRevert.h"
#include "Editor/Backup.h"
#include "Editor/BlankLineCleanup.h"
#include "Editor/CaptureClassifiers.h"
#include "Editor/ClassFileSyncSettings.h"
#include "Editor/Clipboard.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/ColorSwatchSettings.h"
#include "Editor/Coverage/Config.h"
#include "Editor/Dap/Config.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/DiffRefreshSettings.h"
#include "Editor/FileNaming.h"
#include "Editor/FileWatch.h"
#include "Editor/FillColumn.h"
#include "Editor/FinalNewline.h"
#include "Editor/FormatOnSave.h"
#include "Editor/FormatRules.h"
#include "Editor/HighlightSettings.h"
#include "Editor/HugeStructuralWindow.h"
#include "Editor/ImportFixupSettings.h"
#include "Editor/IndentRuleOverride.h"
#include "Editor/IndentStyle.h"
#include "Editor/InjectedIndent.h"
#include "Editor/InlineDebugValues.h"
#include "Editor/InlineDiagnostics.h"
#include "Editor/LanguageRegistry.h"
#include "Editor/LineEndingPolicy.h"
#include "Editor/Link.h"
#include "Editor/Lsp/BackgroundSync.h"
#include "Editor/Lsp/ProseChecker.h"
#include "Editor/Lsp/RootResolver.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/MacroRegistry.h"
#include "Editor/MaxConsecutiveBlankLines.h"
#include "Editor/Mcp/BridgeSetting.h"
#include "Editor/MinimapSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/MultibufferFoldSettings.h"
#include "Editor/MultibufferLimits.h"
#include "Editor/MultibufferSearchSettings.h"
#include "Editor/Org.h"
#include "Editor/OrgCapture.h"
#include "Editor/PageScroll.h"
#include "Editor/PersistentUndo.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Session.h"
#include "Editor/Project/Switch.h"
#include "Editor/Project/Trust.h"
#include "Editor/RecencyGlow.h"
#include "Editor/RelativeLineNumberSettings.h"
#include "Editor/RenameReviewSettings.h"
#include "Editor/Repl/Config.h"
#include "Editor/RulerSettings.h"
#include "Editor/ScratchPad.h"
#include "Editor/ScriptingSession.h"
#include "Editor/SearchEverywhereGestureSettings.h"
#include "Editor/SearchEverywhereTextSearchSettings.h"
#include "Editor/SearchSettings.h"
#include "Editor/Session.h"
#include "Editor/SnippetRegistry.h"
#include "Editor/StatusGutterSettings.h"
#include "Editor/StickyScrollSettings.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/TabWidth.h"
#include "Editor/Tasks/TaskConfig.h"
#include "Editor/Terminal/Config.h"
#include "Editor/TestRun/Config.h"
#include "Editor/TestRun/TestOutputParser.h"
#include "Editor/ThemeSetting.h"
#include "Editor/ToolchainIncludePaths.h"
#include "Editor/TrimOnSave.h"
#include "Editor/Vcs/ProviderRegistry.h"
#include "Editor/Vim/Settings.h"
#include "Editor/WhichKeySettings.h"
#include "Editor/WhitespaceSettings.h"
#include "Editor/WrapIndent.h"
#include "Editor/WrapOverrides.h"
#include "JanetVcsProvider.h"
#include "Text/BufferList.h"
#include "Text/FilePreservation.h"
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

        session.registry.Register(name, docstring, [env, invokeExpr, name](editor::CommandContext& context) {
            editor::CommandContextScope contextScope(context);

            Janet       out;
            std::string capturedError;
            const int   signal = DoStringCapturingStacktrace(env, invokeExpr, "ned-command", &out, &capturedError);
            if (signal != 0) {
                editor::LogMessage(editor::LogCategory::Janet, editor::LogSeverity::Error,
                                   "command \"" + name + "\": " + capturedError);
                throw std::runtime_error(capturedError);
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

    // smart-indentation follow-up. modeName empty sets the process-wide
    // default; non-empty (a Mode::name, e.g. "python-mode") sets a per-mode
    // override -- same "empty string means the process-wide/no-override
    // case" convention NedSetFormatCommand/NedSetUrlOpenCommand already use,
    // since Value.h has no std::optional<std::string> FromJanet
    // specialization yet (see the org-capture-register-template binding's
    // own comment on the same gap).
    void NedSetIndentStyle(std::string modeName, bool useTabs, std::int64_t width) {
        const editor::IndentStyle style{.useTabs = useTabs, .width = static_cast<int>(width)};
        if (modeName.empty()) {
            editor::SetIndentStyle(style);
        }
        else {
            editor::SetIndentStyleForMode(modeName, style);
        }
    }

    // ned/set-indent-rule follow-up: key is a grammar node-type string (see
    // Editor/IndentRuleOverride.h's own header comment for why, not a
    // capture name), or its language-scoped form ("cpp/access_specifier").
    // policy empty clears the rule; otherwise "offset" or "absolute",
    // matching IndentRulePolicy's own two values by name.
    // injected-region-indentation follow-up: same process-wide-bool-toggle
    // shape as NedSetVimMode -- default true, see Editor/InjectedIndent.h.
    void NedSetIndentInjectedRegions(bool enabled) {
        editor::SetIndentInjectedRegions(enabled);
    }

    void NedSetIndentRule(std::string key, std::string policy, std::int64_t value) {
        if (policy.empty()) {
            editor::SetIndentRule(key, std::nullopt);
            return;
        }
        if (policy == "offset") {
            editor::SetIndentRule(key, editor::IndentRuleValue{editor::IndentRulePolicy::Offset, static_cast<int>(value)});
        }
        else if (policy == "absolute") {
            editor::SetIndentRule(key, editor::IndentRuleValue{editor::IndentRulePolicy::Absolute, static_cast<int>(value)});
        }
        else {
            throw std::runtime_error("ned: set-indent-rule: policy must be \"offset\", \"absolute\", or \"\" (to clear), got \"" +
                                     policy + "\"");
        }
    }

    void NedSetFillColumn(std::int64_t columns) {
        editor::SetFillColumn(static_cast<int>(columns));
    }

    // Vim-mode follow-up: same process-wide-bool-toggle shape as
    // NedSetLspAutoComplete -- default false, see Editor/Vim/Settings.h.
    void NedSetVimMode(bool enabled) {
        editor::vim::SetModeEnabled(enabled);
    }

    void NedSetProjectSearchThreads(std::int64_t threads) {
        editor::SetProjectSearchThreads(static_cast<int>(threads));
    }

    void NedSetTerminalHeightPercent(std::int64_t percent) {
        editor::terminal::SetTerminalHeightPercent(static_cast<int>(percent));
    }

    // diagnostics-log follow-up: deliberately does NOT reach into
    // CurrentContext() to force an immediate "*Messages*" rebuild -- unlike
    // ned/insert and friends, this (like every other ned/set-* setting
    // binding) must work when called from init.janet at startup, outside
    // any CommandContextScope. The buffer rebuilds lazily, the next time
    // show-messages runs.
    void NedSetLogCategoryVisible(std::string category, bool visible) {
        const std::optional<editor::LogCategory> parsed = editor::LogCategoryFromString(category);
        if (!parsed) {
            throw std::runtime_error("ned: unknown log category \"" + category + "\"");
        }
        editor::SetLogCategoryVisible(*parsed, visible);
    }

    void NedSetLogMaxEntries(std::int64_t maxEntries) {
        editor::SetLogMaxEntries(static_cast<std::size_t>(std::max<std::int64_t>(1, maxEntries)));
    }

    void NedSetPageScrollFraction(double fraction) {
        editor::SetPageScrollFraction(fraction);
    }

    // ChildProcess-hang-protection-round-2 follow-up.
    void NedSetSubprocessReadTimeoutMs(std::int64_t milliseconds) {
        editor::SetSubprocessReadTimeoutMs(static_cast<int>(milliseconds));
    }

    // write-side-hang-protection follow-up.
    void NedSetSubprocessWriteTimeoutMs(std::int64_t milliseconds) {
        editor::SetSubprocessWriteTimeoutMs(static_cast<int>(milliseconds));
    }

    void NedSetProtocolReadStallTimeoutMs(std::int64_t milliseconds) {
        editor::SetProtocolReadStallTimeoutMs(static_cast<int>(milliseconds));
    }

    void NedSetProtocolWriteStallTimeoutMs(std::int64_t milliseconds) {
        editor::SetProtocolWriteStallTimeoutMs(static_cast<int>(milliseconds));
    }

    void NedSetProtocolRequestTimeoutMs(std::int64_t milliseconds) {
        editor::SetProtocolRequestTimeoutMs(static_cast<int>(milliseconds));
    }

    void NedSetDiffRefreshDebounceMs(std::int64_t milliseconds) {
        editor::SetDiffRefreshDebounceMs(static_cast<int>(milliseconds));
    }

    // rich-theme-set follow-up (Phase 1): stores the *name* only -- resolved
    // against ui::ThemeByName by main.cpp at startup, after init.janet has
    // loaded, so no validation is possible (or wanted) here; see
    // Editor/ThemeSetting.h's own header comment for the layering.
    void NedSetTheme(std::string name) {
        editor::SetPreferredThemeName(name);
    }

    // Theme-editing follow-up: same string-only deferral as NedSetTheme just
    // above -- keys/tokens are validated by ui::SetThemeColorByKey when
    // main.cpp applies them, not here.
    void NedThemeSet(std::string key, std::string token) {
        editor::AddThemeColorOverride(key, token);
    }

    // Translucency follow-up: a paint arrives as its one-line form and is
    // parsed by the UI layer once a real Theme exists to resolve $slot
    // references against -- the same deferral NedThemeSet just above uses,
    // and the reason this layer can stay free of any UI dependency. The
    // array form the docs show (`[:y "$bg" 3 "$bg+8"]`) is Janet-side sugar
    // in the bundled gradients plugin, which flattens to exactly this.
    void NedThemeGradient(std::string name, std::string spec) {
        editor::AddNamedPaint(name, spec);
    }

    void NedThemeSurface(std::string surface, std::string part, std::string spec) {
        editor::AddSurfacePaint(surface, part, spec);
    }

    void NedSetMinimapEnabled(bool enabled) {
        editor::SetMinimapEnabled(enabled);
    }

    void NedSetMinimapWidth(std::int64_t columns) {
        editor::SetMinimapWidth(static_cast<int>(columns));
    }

    void NedSetMinimapCharsPerDot(double columns) {
        editor::SetMinimapCharsPerDot(columns);
    }

    void NedSetRulerEnabled(bool enabled) {
        editor::SetRulerEnabled(enabled);
    }

    void NedSetRulerColumn(std::int64_t column) {
        editor::SetRulerColumn(static_cast<int>(column));
    }

    void NedSetTrailingWhitespaceHighlightEnabled(bool enabled) {
        editor::SetTrailingWhitespaceHighlightEnabled(enabled);
    }

    void NedSetIndentGuidesEnabled(bool enabled) {
        editor::SetIndentGuidesEnabled(enabled);
    }

    void NedSetIndentGuideDepthColorsEnabled(bool enabled) {
        editor::SetIndentGuideDepthColorsEnabled(enabled);
    }

    void NedSetTabGlyphsEnabled(bool enabled) {
        editor::SetTabGlyphsEnabled(enabled);
    }

    void NedSetAutoDetectProjectRoot(bool enabled) {
        editor::SetAutoDetectProjectRoot(enabled);
    }

    void NedSetScratchAutoSave(bool enabled) {
        editor::SetScratchAutoSaveEnabled(enabled);
    }

    // backup-and-recovery follow-up.
    void NedSetFileAutoSave(bool enabled) {
        editor::SetFileAutoSaveEnabled(enabled);
    }

    void NedSetBackupMaxAgeDays(std::int64_t days) {
        editor::SetBackupMaxAgeDays(static_cast<int>(days));
    }

    void NedSetBackupMaxVersions(std::int64_t versions) {
        editor::SetBackupMaxVersions(static_cast<int>(versions));
    }

    void NedSetBackupMaxSizeMb(std::int64_t megabytes) {
        editor::SetBackupMaxSizeMb(static_cast<int>(megabytes));
    }

    // huge-file-crash-recovery-backup follow-up.
    void NedSetBackupVersionMaxSizeMb(std::int64_t megabytes) {
        editor::SetBackupVersionMaxSizeMb(static_cast<int>(megabytes));
    }

    // persistent-undo follow-up.
    void NedSetPersistentUndo(bool enabled) {
        editor::SetPersistentUndoEnabled(enabled);
    }

    void NedSetPersistentUndoMaxSizeMb(std::int64_t megabytes) {
        editor::SetPersistentUndoMaxSizeMb(static_cast<int>(megabytes));
    }

    // The version list ned/recover-backup indexes into -- both must agree,
    // so both go through editor::ListBackupVersions on the same buffer path.
    std::vector<std::string> NedListBackups() {
        const editor::CommandContext& context = CurrentContext();
        if (!context.buffer.Path().has_value()) {
            return {};
        }
        std::vector<std::string> paths;
        for (const editor::BackupVersion& version : editor::ListBackupVersions(*context.buffer.Path())) {
            paths.push_back(version.path.string());
        }
        return paths;
    }

    void NedRecoverBackup(std::int64_t index) {
        editor::CommandContext& context = CurrentContext();
        if (!context.buffer.Path().has_value()) {
            throw std::runtime_error("ned: buffer \"" + context.buffer.Name() + "\" has no file to recover");
        }
        const std::vector<editor::BackupVersion> versions = editor::ListBackupVersions(*context.buffer.Path());
        if (index < 0 || static_cast<std::size_t>(index) >= versions.size()) {
            throw std::runtime_error("ned: no backup version " + std::to_string(index) + " (have " + std::to_string(versions.size()) + ")");
        }
        context.buffer.RestoreContent(editor::ReadBackupVersion(versions[static_cast<std::size_t>(index)].path));
    }

    void NedSetAutoRevert(bool enabled) {
        editor::SetAutoRevertEnabled(enabled);
    }

    void NedSetAcpMcpBridge(bool enabled) {
        editor::mcp::SetAcpMcpBridgeEnabled(enabled);
    }

    void NedSetAutoPairEnabled(bool enabled) {
        editor::SetAutoPairEnabled(enabled);
    }

    void NedSetAutoMerge(bool enabled) {
        editor::SetAutoMergeEnabled(enabled);
    }

    void NedSetLspSyncBackgroundBuffers(bool enabled) {
        editor::lsp::SetLspBackgroundSyncEnabled(enabled);
    }

    void NedSetFileWatch(bool enabled) {
        editor::SetFileWatchEnabled(enabled);
    }

    void NedSetSavePlace(bool enabled) {
        editor::SetSavePlaceEnabled(enabled);
    }

    void NedSetSessionRestore(bool enabled) {
        editor::SetSessionRestoreEnabled(enabled);
    }

    void NedSetProjectTrustExpiryDays(std::int64_t days) {
        editor::SetProjectTrustExpiryDays(static_cast<int>(days));
    }

    void NedSetAsyncLoadThreshold(std::int64_t bytes) {
        text::SetAsyncLoadThreshold(bytes > 0 ? static_cast<std::uintmax_t>(bytes) : 0);
    }

    // huge-file-editing follow-up: same shape as NedSetAsyncLoadThreshold
    // just above.
    void NedSetHugeFileThreshold(std::int64_t bytes) {
        text::SetHugeFileThreshold(bytes > 0 ? static_cast<std::uintmax_t>(bytes) : 0);
    }

    // disk-space-safety follow-up.
    void NedSetHugeFileMinFreeSpaceMultiplier(double multiplier) {
        text::SetHugeFileMinFreeSpaceMultiplier(multiplier);
    }

    void NedSetHugeFileDiskSpaceCheckEnabled(bool enabled) {
        text::SetHugeFileDiskSpaceCheckEnabled(enabled);
    }

    // file-attribute-preservation follow-up (Text/FilePreservation.h).
    void NedSetFollowSymlinksOnSave(bool enabled) {
        text::SetFollowSymlinksOnSave(enabled);
    }

    void NedSetPreserveHardLinksOnSave(bool enabled) {
        text::SetPreserveHardLinksOnSave(enabled);
    }

    void NedSetMaxHighlightBytes(std::int64_t bytes) {
        editor::SetMaxHighlightBytes(bytes > 0 ? static_cast<std::size_t>(bytes) : 0);
    }

    void NedSetHugeStructuralWindowBytes(std::int64_t bytes) {
        editor::SetHugeStructuralWindowBytes(bytes > 0 ? static_cast<std::size_t>(bytes) : 0);
    }

    void NedSetEnsureFinalNewline(bool enabled) {
        editor::SetEnsureFinalNewline(enabled);
    }

    void NedSetTrimTrailingWhitespaceOnSave(bool enabled) {
        editor::SetTrimTrailingWhitespaceOnSave(enabled);
    }

    void NedSetWrapIndent(bool enabled) {
        editor::SetWrapIndent(enabled);
    }

    // automatic-scoped-on-save follow-up.
    void NedSetAutoFormatOnSave(bool enabled) {
        editor::SetAutoFormatOnSave(enabled);
    }

    // configurable-formatter Hygiene-pass follow-up: a negative value means
    // "no limit" (MaxConsecutiveBlankLines.h's own nullopt sentinel) --
    // simpler for a Janet caller than a separate enabled/disabled argument.
    void NedSetMaxConsecutiveBlankLines(std::int64_t max) {
        editor::SetMaxConsecutiveBlankLines(static_cast<int>(max));
    }

    void NedSetCleanBlankLineOnNewline(bool enabled) {
        editor::SetCleanBlankLineOnNewline(enabled);
    }

    // crlf-handling follow-up: "preserve" (default) keeps whatever ending
    // Buffer::FromFile detected per-buffer; "lf"/"crlf"/"cr" force every
    // save to that ending regardless of what was detected. See
    // Editor/LineEndingPolicy.h.
    void NedSetLineEndingPolicy(std::string policy) {
        editor::SetLineEndingPolicyFromString(policy);
    }

    void NedSetCodeFoldingEnabled(bool enabled) {
        editor::SetCodeFoldingEnabled(enabled);
    }

    void NedSetMultibufferAutoCollapseLineThreshold(std::int64_t lines) {
        editor::SetMultibufferAutoCollapseLineThreshold(lines > 0 ? static_cast<std::size_t>(lines) : 0);
    }

    void NedSetMultibufferAutoCollapseByteThreshold(std::int64_t bytes) {
        editor::SetMultibufferAutoCollapseByteThreshold(bytes > 0 ? static_cast<std::size_t>(bytes) : 0);
    }

    void NedSetMultibufferMaxExcerpts(std::int64_t count) {
        editor::SetMultibufferMaxExcerpts(count > 0 ? static_cast<std::size_t>(count) : 0);
    }

    void NedSetMultibufferScopedSearch(bool enabled) {
        editor::SetMultibufferScopedSearch(enabled);
    }

    void NedSetRenameReview(bool enabled) {
        editor::SetRenameThroughReview(enabled);
    }

    void NedSetSearchEverywhereGesture(bool enabled) {
        editor::SetSearchEverywhereGestureEnabled(enabled);
    }

    void NedSetSearchEverywhereTextSearch(bool enabled) {
        editor::SetSearchEverywhereTextSearchEnabled(enabled);
    }

    void NedSetClassFileSync(bool enabled) {
        editor::SetClassFileSync(enabled);
    }

    void NedSetImportFixup(bool enabled) {
        editor::SetImportFixupEnabled(enabled);
    }

    void NedSetImportFixupMaxFiles(std::int64_t count) {
        editor::SetImportFixupMaxFiles(count > 0 ? static_cast<std::size_t>(count) : 0);
    }

    void NedSetMultibufferAutoCollapseExcerptCap(std::int64_t count) {
        editor::SetMultibufferAutoCollapseExcerptCap(count > 0 ? static_cast<std::size_t>(count) : 0);
    }

    void NedSetStickyScrollEnabled(bool enabled) {
        editor::SetStickyScrollEnabled(enabled);
    }

    void NedSetStickyScrollMaxRows(std::int64_t rows) {
        editor::SetStickyScrollMaxRows(static_cast<int>(rows));
    }

    void NedSetRelativeLineNumbers(bool enabled) {
        editor::SetRelativeLineNumbersEnabled(enabled);
    }

    void NedSetUnsavedChangeSwatch(bool enabled) {
        editor::SetUnsavedChangeSwatchEnabled(enabled);
    }

    void NedSetUnseenContentMarker(bool enabled) {
        editor::SetUnseenContentMarkerEnabled(enabled);
    }

    void NedSetUnseenContentMarkerStyle(std::string style) {
        if (style == "band") {
            editor::SetUnseenContentMarkerStyle(editor::UnseenContentMarkerStyle::Band);
            return;
        }
        if (style == "boundary") {
            editor::SetUnseenContentMarkerStyle(editor::UnseenContentMarkerStyle::Boundary);
            return;
        }
        throw std::runtime_error("unknown unseen content marker style: " + style +
                                 " (expected \"band\" or \"boundary\")");
    }

    void NedSetColorSwatches(bool enabled) {
        editor::SetColorSwatchesEnabled(enabled);
    }

    void NedSetColorSwatchStyle(std::string style) {
        if (style == "block") {
            editor::SetColorSwatchStyle(editor::ColorSwatchStyle::Block);
            return;
        }
        if (style == "underlay") {
            editor::SetColorSwatchStyle(editor::ColorSwatchStyle::Underlay);
            return;
        }
        throw std::runtime_error("unknown color swatch style: " + style +
                                 " (expected \"block\" or \"underlay\")");
    }

    void NedSetWhichKeyEnabled(bool enabled) {
        editor::SetWhichKeyEnabled(enabled);
    }

    void NedSetInlineDiagnostics(bool enabled) {
        editor::SetInlineDiagnosticsEnabled(enabled);
    }

    void NedSetInlineDebugValues(bool enabled) {
        editor::SetInlineDebugValuesEnabled(enabled);
    }

    void NedSetInlineDiagnosticStyle(std::string style) {
        if (style == "end-of-line") {
            editor::SetInlineDiagnosticStyle(editor::InlineDiagnosticStyle::EndOfLine);
            return;
        }
        if (style == "callout") {
            editor::SetInlineDiagnosticStyle(editor::InlineDiagnosticStyle::Callout);
            return;
        }
        throw std::runtime_error("unknown inline diagnostic style: " + style + " (expected \"end-of-line\" or \"callout\")");
    }

    void NedSetRecencyGlow(bool enabled) {
        editor::SetRecencyGlowEnabled(enabled);
    }

    void NedRegisterLanguage(std::string directory) {
        editor::LoadLanguageDirectory(directory);
    }

    void NedSetOrgTodoKeywords(std::vector<std::string> keywords) {
        editor::org::SetTodoKeywords(keywords.empty() ? editor::org::DefaultTodoKeywords() : std::move(keywords));
    }

    std::vector<std::string> NedOrgTodoKeywords() {
        return editor::org::TodoKeywords();
    }

    // Wraps a per-text Janet fn as a batch CaptureClassifier
    // (Editor/CaptureClassifiers.h): the fn is bound into the environment
    // under a generated name (janet_def -- reachability through the env
    // table, never a manual root; see Value.h's CAUTION) and invoked as one
    // (map fn args) per batch through janet_dostring, so a thousand
    // captures cost one small compile, not a thousand. Janet is
    // main-thread-only: a call from any other thread (ModePrewarm's
    // background highlight, whose spans are discarded) answers
    // all-Fallthrough via the recorded registering thread's id.
    void NedRegisterCaptureClassifier(std::string languageKey, std::string captureName, Janet fn) {
        if (!g_env) {
            throw std::runtime_error("ned: janet environment not installed");
        }
        if (janet_checktype(fn, JANET_NIL)) {
            editor::RegisterCaptureClassifier(languageKey, captureName, {});
            return;
        }
        const std::string internalName = "ned/capture-classifier-" + languageKey + "-" + captureName;
        janet_def(g_env, internalName.c_str(), fn, "");

        JanetTable*           env         = g_env;
        const std::thread::id janetThread = std::this_thread::get_id();
        editor::RegisterCaptureClassifier(
            languageKey, captureName,
            [env, internalName, languageKey, captureName, janetThread](std::span<const std::string_view> texts)
                -> std::vector<editor::CaptureClassification> {
                std::vector<editor::CaptureClassification> out(texts.size());
                if (std::this_thread::get_id() != janetThread) {
                    return out; // off the Janet thread: all-Fallthrough
                }
                JanetArray* args = janet_array(static_cast<std::int32_t>(texts.size()));
                for (const std::string_view text : texts) {
                    janet_array_push(args, janet_wrap_string(janet_string(
                                               reinterpret_cast<const std::uint8_t*>(text.data()),
                                               static_cast<std::int32_t>(text.size()))));
                }
                const std::string argName = "ned/capture-classifier-arg";
                janet_def(env, argName.c_str(), janet_wrap_array(args), "");

                const std::string invokeExpr = "(map " + internalName + " " + argName + ")";
                Janet             result;
                std::string       capturedError;
                const int         signal = DoStringCapturingStacktrace(env, invokeExpr, "ned-capture-classifier", &result,
                                                                       &capturedError);
                if (signal != 0) {
                    throw std::runtime_error("ned: capture classifier \"" + languageKey + "/" + captureName +
                                             "\": " + capturedError);
                }
                // Per element: a :syntax-class keyword classifies, false
                // suppresses the span outright, nil falls through.
                if (!janet_checktype(result, JANET_ARRAY) && !janet_checktype(result, JANET_TUPLE)) {
                    return out;
                }
                const Janet* items;
                std::int32_t count = 0;
                if (janet_checktype(result, JANET_ARRAY)) {
                    JanetArray* array = janet_unwrap_array(result);
                    items             = array->data;
                    count             = array->count;
                }
                else {
                    const Janet* tuple = janet_unwrap_tuple(result);
                    items              = tuple;
                    count              = janet_tuple_length(tuple);
                }
                for (std::int32_t i = 0; i < count && i < static_cast<std::int32_t>(out.size()); ++i) {
                    if (janet_checktype(items[i], JANET_KEYWORD)) {
                        const std::uint8_t* keyword      = janet_unwrap_keyword(items[i]);
                        out[static_cast<std::size_t>(i)] = editor::CaptureClassification::Class(
                            editor::SyntaxClassByName(reinterpret_cast<const char*>(keyword)));
                    }
                    else if (janet_checktype(items[i], JANET_BOOLEAN) && !janet_unwrap_boolean(items[i])) {
                        out[static_cast<std::size_t>(i)] = editor::CaptureClassification::Suppressed();
                    }
                }
                return out;
            });
    }

    void NedSetModeForExtension(std::string extension, std::string modeName) {
        editor::SetModeForExtension(extension, modeName);
    }

    void NedSetModeForFilename(std::string filename, std::string modeName) {
        editor::SetModeForFilename(filename, modeName);
    }

    void NedSetWrapForExtension(std::string extension, bool wrap) {
        editor::SetWrapForExtension(extension, wrap);
    }

    void NedSetWrapForFilename(std::string filename, bool wrap) {
        editor::SetWrapForFilename(filename, wrap);
    }

    // LSP client follow-up: argv[0] is the server executable, remaining
    // elements its arguments, e.g. (ned/set-lsp-command "python"
    // ["pyright-langserver" "--stdio"]). An empty argv clears any existing
    // registration for language, mirroring NedSetFormatCommand's own
    // empty-clears convention.
    void NedSetLspCommand(std::string language, std::vector<std::string> argv) {
        editor::lsp::SetLspServerCommand(language, std::move(argv));
    }

    // LSP multi-root follow-up: overrides language's root-marker list (see
    // Editor/Lsp/RootResolver.h) -- e.g. (ned/set-lsp-root-markers "rust"
    // ["Cargo.toml"]) for a language with no compiled-in default. An empty
    // markers list clears the override, reverting to the compiled-in
    // default (if any) rather than to "walk for nothing," unlike
    // NedSetLspCommand's own empty-clears-entirely convention -- see
    // SetLspRootMarkers's own doc comment for why.
    void NedSetLspRootMarkers(std::string language, std::vector<std::string> markers) {
        editor::lsp::SetLspRootMarkers(language, std::move(markers));
    }

    // DAP client slice 1: same argv shape as NedSetLspCommand for the
    // adapter subprocess; the launch config stays an opaque JSON string on
    // purpose -- it's the DAP `launch` request's own adapter-specific
    // arguments object, whose keys differ per adapter (see
    // Editor/Dap/Config.h).
    void NedSetDapAdapter(std::string language, std::vector<std::string> argv) {
        editor::dap::SetAdapterCommand(language, std::move(argv));
    }

    void NedSetDapLaunch(std::string language, std::string launchConfigJson) {
        editor::dap::SetLaunchConfig(language, std::move(launchConfigJson));
    }

    // DAP round 3: same opaque-JSON shape as NedSetDapLaunch, for
    // Manager::Attach's `attach` request instead of the launch path.
    void NedSetDapAttach(std::string language, std::string attachConfigJson) {
        editor::dap::SetAttachConfig(language, std::move(attachConfigJson));
    }

    // task-runner follow-up: argv[0] is the task's executable, remaining
    // elements its arguments, e.g. (ned/set-task-command "build" ["cmake"
    // "--build" "."]). An empty argv clears any existing registration for
    // name, mirroring NedSetLspCommand's own empty-clears convention.
    void NedSetTaskCommand(std::string name, std::vector<std::string> argv) {
        editor::tasks::SetTaskCommand(name, std::move(argv));
    }

    // REPL-engine follow-up: NedSetTaskCommand's own argv shape/empty-clears
    // convention -- a REPL is keyed by an arbitrary user-chosen name, e.g.
    // (ned/set-repl-command "python" ["python3" "-i"]). run-repl spawns this
    // argv on a real pty (UI/TerminalPanel.h) and shows the language's own
    // interactive CLI REPL as-is -- readline/completion/history/coloring all
    // come from the process itself, same as running it in any terminal.
    void NedSetReplCommand(std::string name, std::vector<std::string> argv) {
        editor::repl::SetCommand(name, std::move(argv));
    }

    // named-projects follow-up: the escape hatch in switch-project/
    // open-project's activate-root chain for a terminal TerminalTabLauncher
    // doesn't auto-detect (or a user's own preferred invocation) -- same
    // argv shape/empty-clears convention as NedSetTaskCommand, plus a
    // "{root}" placeholder substituted into every argv element.
    void NedSetProjectOpenCommand(std::vector<std::string> argv) {
        editor::SetProjectOpenCommand(std::move(argv));
    }

    // ACP client slice 1: same argv shape/empty-clears convention as
    // NedSetTaskCommand -- an ACP agent is keyed by an arbitrary user-chosen
    // name, not a language, e.g. (ned/set-acp-agent "claude-code"
    // ["claude-code-acp"]).
    void NedSetAcpAgent(std::string name, std::vector<std::string> argv) {
        editor::acp::SetAcpAgentCommand(name, std::move(argv));
    }

    // test-runner-integration: the one project-wide test command plus the
    // format its output parses as -- (ned/set-test-command ["ctest"
    // "--test-dir" "build"] "ctest"). Empty argv clears, NedSetTaskCommand's
    // convention throughout this group.
    void NedSetTestCommand(std::vector<std::string> argv, std::string format) {
        editor::testrun::SetTestCommand(std::move(argv), std::move(format));
    }

    void NedSetTestFilterCommand(std::vector<std::string> argvTemplate) {
        editor::testrun::SetTestFilterCommand(std::move(argvTemplate));
    }

    void NedSetTestResultsFile(std::string path) {
        editor::testrun::SetTestResultsFile(std::move(path));
    }

    // code-coverage-gutter follow-up: the configured lcov .info path --
    // (ned/set-coverage-file "coverage.info"), paired with the
    // load-coverage-report command (Commands.cpp). Empty clears.
    void NedSetCoverageFile(std::string path) {
        editor::coverage::SetFile(std::move(path));
    }

    // Converts a Janet test parser's return value into a Outcome --
    // either a bare array of result tables, or a table {:results [...]
    // :failures-only true :passed n} for the metadata a failures-only
    // format needs. Field reading follows JanetVcsProvider.cpp's
    // "degrade, don't crash" convention: a malformed entry is skipped, a
    // missing field defaults, never a panic.
    editor::testrun::Outcome JanetTestOutcome(Janet value, const std::string& formatName) {
        using editor::testrun::TestResult;
        editor::testrun::Outcome outcome;
        outcome.format   = formatName;
        outcome.parsedOk = true;

        Janet resultsValue = value;
        if (janet_checktype(value, JANET_TABLE) || janet_checktype(value, JANET_STRUCT)) {
            const Janet results = janet_get(value, janet_ckeywordv("results"));
            if (!janet_checktype(results, JANET_NIL)) {
                resultsValue             = results;
                const Janet failuresOnly = janet_get(value, janet_ckeywordv("failures-only"));
                outcome.failuresOnly     = janet_checktype(failuresOnly, JANET_BOOLEAN) && janet_unwrap_boolean(failuresOnly);
                const Janet parsed       = janet_get(value, janet_ckeywordv("parsed-ok"));
                if (janet_checktype(parsed, JANET_BOOLEAN)) {
                    outcome.parsedOk = janet_unwrap_boolean(parsed);
                }
                const Janet passed = janet_get(value, janet_ckeywordv("passed"));
                if (janet_checktype(passed, JANET_NUMBER) && janet_unwrap_number(passed) > 0) {
                    outcome.passed = static_cast<std::size_t>(janet_unwrap_number(passed));
                }
            }
        }

        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(resultsValue, &items, &count)) {
            throw std::runtime_error("ned: a test parser must return an array of result tables (or {:results [...]})");
        }
        const auto stringField = [](Janet entry, const char* key) -> std::string {
            const Janet field = janet_get(entry, janet_ckeywordv(key));
            if (!janet_checktype(field, JANET_STRING)) {
                return {};
            }
            return FromJanet<std::string>(field);
        };
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            TestResult result;
            result.name = stringField(items[i], "name");
            if (result.name.empty()) {
                continue;
            }
            const Janet status = janet_get(items[i], janet_ckeywordv("status"));
            std::string statusName;
            if (janet_checktype(status, JANET_KEYWORD)) {
                const auto* keyword = janet_unwrap_keyword(status);
                statusName          = std::string(reinterpret_cast<const char*>(keyword), janet_string_length(keyword));
            }
            else if (janet_checktype(status, JANET_STRING)) {
                statusName = FromJanet<std::string>(status);
            }
            if (statusName == "passed") {
                result.status = TestResult::Status::Passed;
            }
            else if (statusName == "skipped") {
                result.status = TestResult::Status::Skipped;
            }
            else {
                result.status = TestResult::Status::Failed; // :failed, and the safe default for anything unrecognized
            }
            result.file      = stringField(items[i], "file");
            const Janet line = janet_get(items[i], janet_ckeywordv("line"));
            if (janet_checktype(line, JANET_NUMBER) && janet_unwrap_number(line) > 0) {
                result.line = static_cast<std::size_t>(janet_unwrap_number(line));
            }
            result.message = stringField(items[i], "message");
            outcome.results.push_back(std::move(result));
        }

        std::size_t passedInList = 0;
        for (const TestResult& result : outcome.results) {
            switch (result.status) {
                case TestResult::Status::Passed:
                    ++passedInList;
                    break;
                case TestResult::Status::Failed:
                    ++outcome.failed;
                    break;
                case TestResult::Status::Skipped:
                    ++outcome.skipped;
                    break;
            }
        }
        if (outcome.passed == 0) {
            outcome.passed = passedInList;
        }
        return outcome;
    }

    // Registers fn as the parser for a test format name, resolved by
    // TestRunner ahead of the built-in table (so a user parser may
    // deliberately shadow a built-in name). Same janet_def-not-RootedValue
    // invocation shape as NedRegisterCommand above -- see that function's
    // comment and Value.h's CAUTION. The wrapped fn only ever runs on the
    // main thread (TestRunner documents this), which is what makes a Janet
    // callback legal here at all. A nil fn clears the registration.
    void NedRegisterTestParser(std::string name, Janet fn) {
        if (!g_env) {
            throw std::runtime_error("ned: janet environment not installed");
        }
        if (janet_checktype(fn, JANET_NIL)) {
            editor::testrun::RegisterTestParser(name, {});
            return;
        }
        const std::string internalName = "ned/test-parser-" + name;
        janet_def(g_env, internalName.c_str(), fn, "");

        JanetTable* env = g_env;
        editor::testrun::RegisterTestParser(name, [env, internalName, name](const std::string& output) {
            const std::string argName  = "ned/test-parser-arg";
            const Janet       argValue = janet_wrap_string(
                janet_string(reinterpret_cast<const std::uint8_t*>(output.data()), static_cast<std::int32_t>(output.size())));
            janet_def(env, argName.c_str(), argValue, "");

            const std::string invokeExpr = "(" + internalName + " " + argName + ")";
            Janet             out;
            std::string       capturedError;
            const int         signal = DoStringCapturingStacktrace(env, invokeExpr, "ned-test-parser", &out, &capturedError);
            if (signal != 0) {
                throw std::runtime_error("ned: test parser \"" + name + "\": " + capturedError);
            }
            return JanetTestOutcome(out, name);
        });
    }

    // capture-templates follow-up: registers a template org-capture (C-c k)
    // can later expand by key -- headline empty means "file at the end of
    // targetFile" rather than under a specific headline, mirroring
    // NedSetTaskCommand's own empty-clears convention for "nothing
    // configured." key must be exactly one character; Value.h has no
    // std::optional<std::string> FromJanet specialization yet, so headline
    // uses the same empty-string-means-absent shape rather than adding one
    // just for this.
    void NedOrgCaptureRegisterTemplate(std::string key, std::string name, std::string targetFile,
                                       std::string templateText, std::string headline) {
        if (key.size() != 1) {
            throw std::runtime_error("ned/org-capture-register-template: key must be exactly one character");
        }
        editor::org::RegisterCaptureTemplate(editor::org::CaptureTemplate{
            key[0], std::move(name), std::move(targetFile), std::move(templateText), std::move(headline)});
    }

    // snippet-expansion follow-up: registers a trigger-word snippet
    // (Editor/SnippetRegistry.h) -- language-key "" means every mode, an
    // empty body clears, re-registering overwrites (NedSetTaskCommand's own
    // conventions). An empty trigger throws (auto-panics via Register).
    void NedRegisterSnippet(std::string languageKey, std::string trigger, std::string body) {
        editor::RegisterSnippet(languageKey, trigger, body);
    }

    std::vector<std::string> NedSnippetTriggers(std::string languageKey) {
        return editor::SnippetTriggers(languageKey);
    }

    // search-everywhere follow-up: named keyboard macros (Editor/
    // MacroRegistry.h). Each chord is Emacs kbd-style text (Editor/Key.h's
    // ParseKeyChord -- the same notation ned/define-key's ParseKeySequence
    // already parses one chord at a time from); kmacro-insert-macro-definition
    // is what actually generates a call shaped like this, via FormatKeyChord.
    // Empty chords clears the name (NedRegisterSnippet's own convention).
    void NedRegisterMacro(std::string name, std::vector<std::string> chords) {
        std::vector<editor::KeyChord> parsed;
        parsed.reserve(chords.size());
        for (const std::string& chord : chords) {
            parsed.push_back(editor::ParseKeyChord(chord));
        }
        editor::RegisterMacro(name, parsed);
    }

    std::vector<std::string> NedMacroNames() {
        return editor::MacroNames();
    }

    // ACP chat panel: which edge the dock hugs and how much of the screen it
    // covers, mirroring NedSetTerminalHeightPercent's own shape.
    void NedSetAcpPanelDock(std::string side) {
        editor::acp::SetAcpPanelDock(side);
    }

    void NedSetAcpPanelSizePercent(std::int64_t percent) {
        editor::acp::SetAcpPanelSizePercent(static_cast<int>(percent));
    }

    // hover/completion follow-up: the only way ServerConfig.h's own
    // process-wide auto-complete toggle/debounce ever get configured away
    // from their defaults (enabled, 500ms), same "just forward to the
    // process-wide setter" shape NedSetTabWidth already established.
    void NedSetLspAutoComplete(bool enabled) {
        editor::lsp::SetLspAutoCompleteEnabled(enabled);
    }

    void NedSetLspCompletionDebounce(std::int64_t milliseconds) {
        editor::lsp::SetLspCompletionDebounceMs(static_cast<int>(milliseconds));
    }

    // signature-help-auto-trigger follow-up: same "just forward to the
    // process-wide setter" shape as NedSetLspAutoComplete above.
    void NedSetLspSignatureHelpAutoTrigger(bool enabled) {
        editor::lsp::SetLspSignatureHelpAutoTriggerEnabled(enabled);
    }

    // hover-tooltips follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspSignatureHelpAutoTrigger above.
    void NedSetLspCommitCharacters(bool enabled) {
        editor::lsp::SetLspCommitCharactersEnabled(enabled);
    }

    void NedSetLspHoverOnMouseMove(bool enabled) {
        editor::lsp::SetLspHoverOnMouseMoveEnabled(enabled);
    }

    // lsp-format-on-save follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspAutoComplete above.
    void NedSetLspFormatOnSave(bool enabled) {
        editor::lsp::SetLspFormatOnSaveEnabled(enabled);
    }

    // on-type-formatting follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspFormatOnSave above.
    void NedSetLspOnTypeFormatting(bool enabled) {
        editor::lsp::SetLspOnTypeFormattingEnabled(enabled);
    }

    // pull-diagnostics follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspFormatOnSave above.
    void NedSetLspPullDiagnostics(bool enabled) {
        editor::lsp::SetLspPullDiagnosticsEnabled(enabled);
    }

    // project-wide-diagnostics follow-up: same "just forward to the
    // process-wide setter" shape as NedSetLspPullDiagnostics above.
    void NedSetProjectDiagnostics(bool enabled) {
        editor::lsp::SetProjectDiagnosticsEnabled(enabled);
    }

    // semanticTokens follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspSignatureHelpAutoTrigger above.
    void NedSetLspSemanticHighlighting(bool enabled) {
        editor::lsp::SetLspSemanticHighlightingEnabled(enabled);
    }

    void NedSetLspWorkspaceFolders(bool enabled) {
        editor::lsp::SetLspWorkspaceFoldersEnabled(enabled);
    }

    // inlayHint follow-up: same "just forward to the process-wide setter"
    // shape as NedSetLspSemanticHighlighting above.
    void NedSetLspInlayHints(bool enabled) {
        editor::lsp::SetLspInlayHintsEnabled(enabled);
    }

    // codeLens follow-up: same "just forward to the process-wide setter"
    // shape as NedSetLspInlayHints above.
    void NedSetLspCodeLens(bool enabled) {
        editor::lsp::SetLspCodeLensEnabled(enabled);
    }

    // code-action-hints follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspCodeLens above.
    void NedSetLspCodeActionHints(bool enabled) {
        editor::lsp::SetLspCodeActionHintsEnabled(enabled);
    }

    // diagnostics-debounce follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspCompletionDebounce, for how long a buffer's
    // inline diagnostics wait after the server's most recent publish before
    // actually updating (see Manager::HandlePublishDiagnostics).
    void NedSetLspDiagnosticsDebounce(std::int64_t milliseconds) {
        editor::lsp::SetLspDiagnosticsDebounceMs(static_cast<int>(milliseconds));
    }

    // sync-debounce follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspCompletionDebounce above, for how long
    // Manager waits after an edit before actually sending
    // textDocument/didChange (see ServerConfig.h's own doc comment for
    // why this fix exists and why it must stay shorter than the completion
    // debounce).
    void NedSetLspSyncDebounce(std::int64_t milliseconds) {
        editor::lsp::SetLspSyncDebounceMs(static_cast<int>(milliseconds));
    }

    // Same "just forward to the process-wide setter" shape again, for the
    // window Manager::RequestViewportFeatures throttles a moving viewport
    // down to.
    void NedSetLspRequestIdle(std::int64_t milliseconds) {
        editor::lsp::SetLspRequestIdleMs(static_cast<int>(milliseconds));
    }

    // toolchain-include-paths follow-up: same "just forward to the
    // process-wide setter" shape as NedSetLspCompletionDebounce above.
    void NedSetIncludePathCacheTtlSeconds(std::int64_t seconds) {
        editor::SetIncludePathCacheTtlSeconds(static_cast<int>(seconds));
    }

    // prose-checking follow-up: same argv shape/empty-clears convention as
    // NedSetLspCommand, but for the one, fixed prose-checker connection --
    // auto-wired to harper-ls when it's on $PATH if no override is set (see
    // Editor/Lsp/ProseChecker.h).
    void NedSetProseCheckerCommand(std::vector<std::string> argv) {
        editor::lsp::SetProseCheckerCommand(std::move(argv));
    }

    void NedSetProseCheckerEnabled(bool enabled) {
        editor::lsp::SetProseCheckingEnabled(enabled);
    }

    // system-clipboard follow-up: same argv shape/empty-clears convention
    // as NedSetProseCheckerCommand, but copy and paste are independently
    // overridable (see Editor/Clipboard.h's own header comment for why
    // there are two).
    void NedSetClipboardCopyCommand(std::vector<std::string> argv) {
        editor::SetClipboardCopyCommand(std::move(argv));
    }

    void NedSetClipboardPasteCommand(std::vector<std::string> argv) {
        editor::SetClipboardPasteCommand(std::move(argv));
    }

    void NedSetClipboardEnabled(bool enabled) {
        editor::SetClipboardEnabled(enabled);
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

    // blank-lines-kind follow-up: BlankRuleValue's fields are counts, not
    // bools -- same nil-clears convention as JanetToOptionalBool above.
    std::optional<int> JanetToOptionalInt(Janet value) {
        if (janet_checktype(value, JANET_NIL)) {
            return std::nullopt;
        }
        return static_cast<int>(FromJanet<std::int64_t>(value));
    }

    // file-naming-conventions follow-up: a header-guard template needs THREE
    // states (nil clears an override back to the built-in default; an
    // explicit "" turns a built-in default off; any other string overrides
    // it), which the "empty string clears" convention every other string
    // setting in this file uses can't express -- nil is the clear signal
    // here instead, same as JanetToOptionalBool/Int above.
    std::optional<std::string> JanetToOptionalString(Janet value) {
        if (janet_checktype(value, JANET_NIL)) {
            return std::nullopt;
        }
        return FromJanet<std::string>(value);
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

    // exhaustive-highlighting follow-up: the per-capture-name tier below
    // SyntaxClass -- same setter/getter shapes as the ned/syntax-* family
    // above (empty string clears a color, nil clears a trait), keyed by a
    // raw dotted tree-sitter capture name ("function.builtin", no leading
    // '@') instead of a class name. Resolution inherits along the dotted
    // chain and then falls through to the class tier -- see
    // Editor/SyntaxTheme.h.
    void NedSetCaptureForeground(std::string captureName, std::string hex) {
        editor::SetCaptureForeground(captureName, hex.empty() ? std::nullopt : std::optional<std::string>(std::move(hex)));
    }

    void NedSetCaptureBackground(std::string captureName, std::string hex) {
        editor::SetCaptureBackground(captureName, hex.empty() ? std::nullopt : std::optional<std::string>(std::move(hex)));
    }

    void NedSetCaptureBold(std::string captureName, Janet value) {
        editor::SetCaptureBold(captureName, JanetToOptionalBool(value));
    }

    void NedSetCaptureItalic(std::string captureName, Janet value) {
        editor::SetCaptureItalic(captureName, JanetToOptionalBool(value));
    }

    void NedSetCaptureUnderlined(std::string captureName, Janet value) {
        editor::SetCaptureUnderlined(captureName, JanetToOptionalBool(value));
    }

    void NedSetCaptureStrikethrough(std::string captureName, Janet value) {
        editor::SetCaptureStrikethrough(captureName, JanetToOptionalBool(value));
    }

    std::optional<std::string> NedCaptureForeground(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).foreground;
    }

    std::optional<std::string> NedCaptureBackground(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).background;
    }

    std::optional<bool> NedCaptureBold(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).bold;
    }

    std::optional<bool> NedCaptureItalic(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).italic;
    }

    std::optional<bool> NedCaptureUnderlined(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).underlined;
    }

    std::optional<bool> NedCaptureStrikethrough(std::string captureName) {
        return editor::CaptureOverrideFor(captureName).strikethrough;
    }

    // Re-bases what a capture name *is* (its SyntaxClass, hence every
    // built-in theme color/trait that class carries) -- JetBrains' "inherit
    // values from" control; empty string clears, back to the built-in
    // mapping. Also the general successor to the extension-era ask "make
    // capture X render like class Y without a rebuild."
    void NedSetCaptureClass(std::string captureName, std::string className) {
        editor::SetSyntaxClassForCapture(captureName, className.empty()
                                                          ? std::nullopt
                                                          : std::optional(editor::SyntaxClassByName(className)));
    }

    std::optional<std::string> NedCaptureClass(std::string captureName) {
        const auto cls = editor::SyntaxClassOverrideForCapture(captureName);
        return cls ? std::optional(editor::SyntaxClassName(*cls)) : std::nullopt;
    }

    // The known capture-name universe: every name the built-in defaults
    // table maps (Mode.h's BuiltinCaptureNames) merged with every name this
    // session has interned from a real query or configured an override/
    // remap for (SyntaxTheme.h's KnownCaptureNames) -- sorted, deduplicated.
    std::vector<std::string> NedCaptureNames() {
        std::vector<std::string> names = editor::BuiltinCaptureNames();
        for (std::string& name : editor::KnownCaptureNames()) {
            names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    // configurable-formatter-rules follow-up: kind 2 (Space) / kind 3 (Break,
    // brace placement folded in) per-capture-name rule overrides --
    // Editor/FormatRules.h's own flat, language-prefixed key convention
    // ("control.parens" or "cpp/control.parens", the language-scoped form
    // resolved by prefix alone, not a separate parameter). Same
    // nil-clears-a-field / empty-string-clears-an-enum shape as
    // ned/set-capture-*/ned/set-capture-class above; getters are exact-name
    // lookups only, matching ned/capture-foreground's own shape (no
    // dotted/language resolution surfaced to Janet -- FormatRules.h's
    // language-scoped overload is for a future pass's own internal use).
    void NedSetFormatSpaceBefore(std::string captureName, Janet value) {
        editor::SetSpaceBefore(captureName, JanetToOptionalBool(value));
    }

    void NedSetFormatSpaceAfter(std::string captureName, Janet value) {
        editor::SetSpaceAfter(captureName, JanetToOptionalBool(value));
    }

    void NedSetFormatSpaceWithin(std::string captureName, Janet value) {
        editor::SetSpaceWithin(captureName, JanetToOptionalBool(value));
    }

    std::optional<bool> NedFormatSpaceBefore(std::string captureName) {
        return editor::SpaceRuleFor(captureName).before;
    }

    std::optional<bool> NedFormatSpaceAfter(std::string captureName) {
        return editor::SpaceRuleFor(captureName).after;
    }

    std::optional<bool> NedFormatSpaceWithin(std::string captureName) {
        return editor::SpaceRuleFor(captureName).within;
    }

    void NedSetFormatBreakBefore(std::string captureName, Janet value) {
        editor::SetBreakBefore(captureName, JanetToOptionalBool(value));
    }

    void NedSetFormatBreakAfter(std::string captureName, Janet value) {
        editor::SetBreakAfter(captureName, JanetToOptionalBool(value));
    }

    // Empty string clears, mirroring ned/set-capture-class's own convention
    // for an enum-valued field.
    void NedSetFormatBracePlacement(std::string captureName, std::string placement) {
        editor::SetBracePlacement(captureName, placement.empty()
                                                   ? std::nullopt
                                                   : std::optional(editor::BracePlacementByName(placement)));
    }

    // format-buffer-tier follow-up: per-language, with the empty key as the
    // process-wide default -- ned/set-indent-style's own convention.
    void NedSetLspFormatBuffer(std::string language, Janet enabled) {
        editor::lsp::SetLspFormatBufferEnabled(language, JanetToOptionalBool(enabled));
    }

    bool NedLspFormatBuffer(std::string language) {
        return editor::lsp::FormatBufferEnabled(language);
    }

    void NedSetFormatBraceCollapseEmpty(std::string captureName, Janet value) {
        editor::SetBraceCollapseEmpty(captureName, JanetToOptionalBool(value));
    }

    void NedSetFormatBraceCollapseSimple(std::string captureName, Janet value) {
        editor::SetBraceCollapseSimple(captureName, JanetToOptionalBool(value));
    }

    std::optional<bool> NedFormatBreakBefore(std::string captureName) {
        return editor::BreakRuleFor(captureName).before;
    }

    std::optional<bool> NedFormatBreakAfter(std::string captureName) {
        return editor::BreakRuleFor(captureName).after;
    }

    std::optional<std::string> NedFormatBracePlacement(std::string captureName) {
        const auto placement = editor::BreakRuleFor(captureName).placement;
        return placement ? std::optional(editor::BracePlacementName(*placement)) : std::nullopt;
    }

    std::optional<bool> NedFormatBraceCollapseEmpty(std::string captureName) {
        return editor::BreakRuleFor(captureName).collapseEmpty;
    }

    std::optional<bool> NedFormatBraceCollapseSimple(std::string captureName) {
        return editor::BreakRuleFor(captureName).collapseSimple;
    }

    // Empty string clears, same convention NedSetFormatBracePlacement uses
    // for its own enum-valued field.
    void NedSetFormatWrapPolicy(std::string captureName, std::string policy) {
        editor::SetWrapPolicy(captureName,
                              policy.empty() ? std::nullopt : std::optional(editor::WrapPolicyByName(policy)));
    }

    void NedSetFormatWrapForceTrailingComma(std::string captureName, Janet value) {
        editor::SetWrapForceTrailingComma(captureName, JanetToOptionalBool(value));
    }

    std::optional<std::string> NedFormatWrapPolicy(std::string captureName) {
        const auto policy = editor::WrapRuleFor(captureName).policy;
        return policy ? std::optional(editor::WrapPolicyName(*policy)) : std::nullopt;
    }

    std::optional<bool> NedFormatWrapForceTrailingComma(std::string captureName) {
        return editor::WrapRuleFor(captureName).forceTrailingComma;
    }

    // Empty string clears, same convention NedSetFormatWrapPolicy uses for its
    // own enum-valued field. `entityKind` is a bare entity-kind string
    // ("function", "parameter", ...) or its language-scoped form
    // ("cpp/function") -- see FormatRules.h's own CaseConvention header
    // comment for why this is not a real query-capture name.
    void NedSetFormatCaseConvention(std::string entityKind, std::string convention) {
        editor::SetCaseConvention(
            entityKind, convention.empty() ? std::nullopt : std::optional(editor::CaseConventionByName(convention)));
    }

    std::optional<std::string> NedFormatCaseConvention(std::string entityKind) {
        const auto convention = editor::CaseRuleFor(entityKind).convention;
        return convention ? std::optional(editor::CaseConventionName(*convention)) : std::nullopt;
    }

    // file-naming-conventions follow-up: same empty-string-clears convention
    // NedSetFormatCaseConvention uses above -- `language` is a bare language
    // name ("cpp"), not an entity kind, so there's no cross-language scoping
    // to worry about here.
    void NedSetFileNamingCaseConvention(std::string language, std::string convention) {
        editor::SetFileNamingCaseConvention(
            language, convention.empty() ? std::nullopt : std::optional(editor::CaseConventionByName(convention)));
    }

    std::optional<std::string> NedFileNamingCaseConvention(std::string language) {
        const auto convention = editor::FileNamingRuleFor(language).caseConvention;
        return convention ? std::optional(editor::CaseConventionName(*convention)) : std::nullopt;
    }

    void NedSetHeaderGuardTemplate(std::string language, Janet value) {
        editor::SetHeaderGuardTemplate(language, JanetToOptionalString(value));
    }

    std::optional<std::string> NedHeaderGuardTemplate(std::string language) {
        return editor::FileNamingRuleFor(language).headerGuardTemplate;
    }

    void NedSetAutoHeaderGuard(bool enabled) {
        editor::SetAutoHeaderGuard(enabled);
    }

    bool NedAutoHeaderGuardEnabled() {
        return editor::AutoHeaderGuardEnabled();
    }

    void NedSetFormatBlankMinBefore(std::string captureName, Janet value) {
        editor::SetBlankMinBefore(captureName, JanetToOptionalInt(value));
    }

    void NedSetFormatBlankMaxBefore(std::string captureName, Janet value) {
        editor::SetBlankMaxBefore(captureName, JanetToOptionalInt(value));
    }

    std::optional<int> NedFormatBlankMinBefore(std::string captureName) {
        return editor::BlankRuleFor(captureName).minBefore;
    }

    std::optional<int> NedFormatBlankMaxBefore(std::string captureName) {
        return editor::BlankRuleFor(captureName).maxBefore;
    }

    // align/arrange/rewrite-kind rollout: same nil-clears-a-field /
    // empty-string-clears-an-enum shape every prior rule kind's own binding
    // above already uses.
    void NedSetFormatAlignEnabled(std::string captureName, Janet value) {
        editor::SetAlignEnabled(captureName, JanetToOptionalBool(value));
    }

    std::optional<bool> NedFormatAlignEnabled(std::string captureName) {
        return editor::AlignRuleFor(captureName).enabled;
    }

    void NedSetFormatArrangeEnabled(std::string captureName, Janet value) {
        editor::SetArrangeEnabled(captureName, JanetToOptionalBool(value));
    }

    void NedSetFormatArrangeCaseInsensitive(std::string captureName, Janet value) {
        editor::SetArrangeCaseInsensitive(captureName, JanetToOptionalBool(value));
    }

    std::optional<bool> NedFormatArrangeEnabled(std::string captureName) {
        return editor::ArrangeRuleFor(captureName).enabled;
    }

    std::optional<bool> NedFormatArrangeCaseInsensitive(std::string captureName) {
        return editor::ArrangeRuleFor(captureName).caseInsensitive;
    }

    // Empty string clears, same convention NedSetFormatWrapPolicy uses for
    // its own enum-valued field.
    void NedSetFormatRewriteQuoteStyle(std::string captureName, std::string style) {
        editor::SetRewriteQuoteStyle(captureName,
                                     style.empty() ? std::nullopt : std::optional(editor::QuoteStyleByName(style)));
    }

    std::optional<std::string> NedFormatRewriteQuoteStyle(std::string captureName) {
        const auto style = editor::RewriteRuleFor(captureName).quoteStyle;
        return style ? std::optional(editor::QuoteStyleName(*style)) : std::nullopt;
    }

    void NedSetFormatRewriteExpandElseif(std::string captureName, Janet value) {
        editor::SetRewriteExpandElseif(captureName, JanetToOptionalBool(value));
    }

    std::optional<bool> NedFormatRewriteExpandElseif(std::string captureName) {
        return editor::RewriteRuleFor(captureName).expandElseif;
    }

    // Registers a VCS-agnostic plugin from one struct/table of callbacks
    // keyed by keyword -- see JanetVcsProvider's header comment for the
    // full key list and which are optional (vocabulary-completion
    // follow-up: replaced the original 7-positional-callback signature
    // outright once the vocabulary grew to sixteen operations; a clean
    // break, not a compatibility shim, since the positional form was days
    // old with one caller). Each present callback is bound into the
    // environment by JanetVcsProvider's own constructor (janet_def, not
    // RootedValue -- see that class's header comment). Re-registering name
    // overwrites the previous provider, matching NedRegisterCommand's own
    // convention. Clears the provider-resolution cache afterward so a root
    // checked before this registration (and resolved to no provider, or a
    // different one) gets a fresh answer.
    void NedVcsRegisterProvider(std::string name, Janet callbacks) {
        if (!g_env) {
            throw std::runtime_error("ned: janet environment not installed");
        }
        auto provider = std::make_unique<JanetVcsProvider>(g_env, name, callbacks);
        editor::vcs::RegisterProvider(name, std::move(provider));
        editor::vcs::ClearProviderCache();
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
    env.Register<&NedSetIndentStyle>(
        "ned", "set-indent-style",
        "Set the indent style smart-indentation (indent-for-tab-command/newline/indent-region/indent-buffer) writes: "
        "(mode-name-or-empty use-tabs? width). An empty mode-name sets the process-wide default (spaces, width 4); "
        "a Mode name (e.g. \"python-mode\") sets a per-mode override, checked first.");
    env.Register<&NedSetIndentInjectedRegions>(
        "ned", "set-indent-injected-regions",
        "Indent a region written in an injected language by that language's own rules (default true) -- the HTML in "
        "a PHP template, the JavaScript in an HTML <script>. The host grammar decides where the region sits, the "
        "injected one how far into its own structure each line is. False indents by the host grammar alone, which "
        "leaves a region's every line at the column the host put the region at.");
    env.Register<&NedSetIndentRule>(
        "ned", "set-indent-rule",
        "Override the indent of every line whose own leading construct is a given grammar node type (e.g. "
        "\"access_specifier\", or \"cpp/access_specifier\" for a one-language override): (key policy value), "
        "policy \"offset\" (add value columns, positive or negative, to whatever the ordinary indent would be) or "
        "\"absolute\" (value IS the column, ignoring nesting depth entirely); empty policy clears the rule.");
    env.Register<&NedSetFormatSpaceBefore>(
        "ned", "set-format-space-before",
        "Override whether a space is inserted before the given capture name (e.g. \"control.parens\", or "
        "\"cpp/control.parens\" for a one-language override) -- true/false, nil clears. No format.scm query "
        "consumes this yet (configurable-formatter-rules follow-up, Editor/FormatRules.h).");
    env.Register<&NedSetFormatSpaceAfter>(
        "ned", "set-format-space-after", "Override whether a space is inserted after the given capture name -- true/false, nil clears.");
    env.Register<&NedSetFormatSpaceWithin>(
        "ned", "set-format-space-within",
        "Override whether a space is inserted just inside the given capture's delimiter pair -- true/false, nil clears.");
    env.Register<&NedFormatSpaceBefore>(
        "ned", "format-space-before", "The capture name's own overridden space-before rule, or nil if unset (no inheritance walk).");
    env.Register<&NedFormatSpaceAfter>(
        "ned", "format-space-after", "The capture name's own overridden space-after rule, or nil if unset.");
    env.Register<&NedFormatSpaceWithin>(
        "ned", "format-space-within", "The capture name's own overridden space-within rule, or nil if unset.");
    env.Register<&NedSetFormatBreakBefore>(
        "ned", "set-format-break-before",
        "Override whether a newline is forced before the given capture name -- true/false, nil clears. The capture "
        "names a keyword token (\"control.keyword\": else/elseif/catch/finally, do-while's while); true puts it on its "
        "own line at the preceding closer's column (Allman), false normalises horizontal space only and never "
        "un-breaks a keyword already on its own line.");
    env.Register<&NedSetFormatBreakAfter>(
        "ned", "set-format-break-after",
        "Override whether a newline is forced after the given capture name -- true/false, nil clears. Same shape as "
        "ned/set-format-break-before, on the far side of the token.");
    env.Register<&NedSetFormatBracePlacement>(
        "ned", "set-format-brace-placement",
        "Override brace placement for a brace-carrying capture name: \"same-line\" (K&R), \"next-line\" (Allman), "
        "or \"next-line-indented\" (GNU/Whitesmiths); empty string clears.");
    env.Register<&NedSetFormatBraceCollapseEmpty>(
        "ned", "set-format-brace-collapse-empty",
        "Override whether the given brace-carrying capture keeps empty braces/block on one line -- true/false, nil clears.");
    env.Register<&NedSetFormatBraceCollapseSimple>(
        "ned", "set-format-brace-collapse-simple",
        "Override whether the given brace-carrying capture keeps a simple one-statement block on one line -- "
        "true/false, nil clears.");
    env.Register<&NedFormatBreakBefore>(
        "ned", "format-break-before", "The capture name's own overridden break-before rule, or nil if unset.");
    env.Register<&NedFormatBreakAfter>(
        "ned", "format-break-after", "The capture name's own overridden break-after rule, or nil if unset.");
    env.Register<&NedFormatBracePlacement>(
        "ned", "format-brace-placement", "The capture name's own overridden brace-placement name, or nil if unset.");
    env.Register<&NedFormatBraceCollapseEmpty>(
        "ned", "format-brace-collapse-empty", "The capture name's own overridden collapse-empty rule, or nil if unset.");
    env.Register<&NedFormatBraceCollapseSimple>(
        "ned", "format-brace-collapse-simple", "The capture name's own overridden collapse-simple rule, or nil if unset.");
    env.Register<&NedSetFormatWrapPolicy>(
        "ned", "set-format-wrap-policy",
        "Override the wrap policy for a delimited-list capture name: \"never\" (always collapse to one line) or "
        "\"always\" (always one item per line); empty string clears.");
    env.Register<&NedSetFormatWrapForceTrailingComma>(
        "ned", "set-format-wrap-force-trailing-comma",
        "Override whether a wrapped (multi-line) list gets a trailing separator after its last item -- true/false, "
        "nil clears.");
    env.Register<&NedFormatWrapPolicy>(
        "ned", "format-wrap-policy", "The capture name's own overridden wrap-policy name, or nil if unset.");
    env.Register<&NedFormatWrapForceTrailingComma>(
        "ned", "format-wrap-force-trailing-comma",
        "The capture name's own overridden wrap-force-trailing-comma rule, or nil if unset.");
    env.Register<&NedSetFormatCaseConvention>(
        "ned", "set-format-case-convention",
        "Override the naming-case convention for an entity kind (\"function\", \"parameter\", \"local\", "
        "\"type\", \"namespace\", or \"<language>/<entity-kind>\" for a language-scoped override): "
        "\"none\", \"lowercase\", \"uppercase\", \"camel-case\", \"pascal-case\", \"snake-case\", "
        "\"leading-snake-case\", \"upper-snake-case\", \"screaming-snake-case\", or \"lisp-case\"; empty string "
        "clears. This is a CHECKER-only setting -- it is never applied automatically by format-buffer/--format, "
        "matching Docs/FormattingCapabilities.md's own stance that renaming on save would be hostile.");
    env.Register<&NedFormatCaseConvention>(
        "ned", "format-case-convention",
        "The entity kind's own overridden case-convention name, or nil if unset.");
    env.Register<&NedSetFileNamingCaseConvention>(
        "ned", "set-file-naming-case-convention",
        "Override the case convention for a NEW file's own basename in the given language (\"cpp\", \"python\", "
        "...) -- same convention-name set as ned/set-format-case-convention, empty string clears. A CHECKER only: "
        "surfaced as a status-line note when a new file's name doesn't conform, never a rename.");
    env.Register<&NedFileNamingCaseConvention>(
        "ned", "file-naming-case-convention",
        "The language's own overridden new-file case-convention name, or nil if unset.");
    env.Register<&NedSetHeaderGuardTemplate>(
        "ned", "set-header-guard-template",
        "Override the header-guard macro-name template for a language (\"cpp\", \"c\") -- substitutes "
        "${PROJECT_NAME}/${FILE_NAME}/${EXT}, each uppercased and sanitized to a valid identifier fragment. nil "
        "clears an override back to that language's built-in default (cpp/c ship "
        "\"${PROJECT_NAME}_${FILE_NAME}_${EXT}\"); an explicit empty string turns a built-in default off entirely.");
    env.Register<&NedHeaderGuardTemplate>(
        "ned", "header-guard-template",
        "The language's own effective header-guard template (including any built-in default), or nil if none.");
    env.Register<&NedSetAutoHeaderGuard>(
        "ned", "set-auto-header-guard",
        "Whether creating a new header file auto-populates it with the expanded #ifndef/#define/#endif guard "
        "skeleton. Default off.");
    env.Register<&NedAutoHeaderGuardEnabled>("ned", "auto-header-guard-enabled",
                                             "Whether ned/set-auto-header-guard is currently on.");
    env.Register<&NedSetFormatBlankMinBefore>(
        "ned", "set-format-blank-min-before",
        "Override the minimum blank lines required immediately before the given capture name -- an integer, nil "
        "clears. Skipped when the capture is the first named child of its own container (nothing above it to "
        "separate from but the container's own opening line).");
    env.Register<&NedSetFormatBlankMaxBefore>(
        "ned", "set-format-blank-max-before",
        "Override the maximum blank lines preserved immediately before the given capture name -- an integer, nil "
        "clears. Applied unconditionally, unlike min-before.");
    env.Register<&NedFormatBlankMinBefore>(
        "ned", "format-blank-min-before", "The capture name's own overridden blank-min-before rule, or nil if unset.");
    env.Register<&NedFormatBlankMaxBefore>(
        "ned", "format-blank-max-before", "The capture name's own overridden blank-max-before rule, or nil if unset.");
    env.Register<&NedSetFormatAlignEnabled>(
        "ned", "set-format-align-enabled",
        "Override whether a run of adjacent, same-indent lines sharing the given capture name gets their anchor "
        "tokens padded to a shared column (kind 5, Align) -- true/false, nil clears.");
    env.Register<&NedFormatAlignEnabled>(
        "ned", "format-align-enabled", "The capture name's own overridden align-enabled rule, or nil if unset.");
    env.Register<&NedSetFormatArrangeEnabled>(
        "ned", "set-format-arrange-enabled",
        "Override whether a run of adjacent sibling captures sharing the given capture name gets reordered by "
        "sort key (kind 8, Arrange) -- true/false, nil clears.");
    env.Register<&NedSetFormatArrangeCaseInsensitive>(
        "ned", "set-format-arrange-case-insensitive",
        "Override whether the given capture name's own Arrange sort folds ASCII case before comparing -- "
        "true/false, nil clears (unset behaves as an ordinary case-sensitive ordinal compare).");
    env.Register<&NedFormatArrangeEnabled>(
        "ned", "format-arrange-enabled", "The capture name's own overridden arrange-enabled rule, or nil if unset.");
    env.Register<&NedFormatArrangeCaseInsensitive>(
        "ned", "format-arrange-case-insensitive",
        "The capture name's own overridden arrange-case-insensitive rule, or nil if unset.");
    env.Register<&NedSetFormatRewriteQuoteStyle>(
        "ned", "set-format-rewrite-quote-style",
        "Override the quote-style rewrite for a string-literal capture name (kind 9, Rewrite): \"single\" or "
        "\"double\"; empty string clears. Declined per-string whenever a pure delimiter swap isn't safe -- see "
        "Editor/FormatRewrite.h's own header comment.");
    env.Register<&NedFormatRewriteQuoteStyle>(
        "ned", "format-rewrite-quote-style", "The capture name's own overridden rewrite-quote-style name, or nil if unset.");
    env.Register<&NedSetFormatRewriteExpandElseif>(
        "ned", "set-format-rewrite-expand-elseif",
        "Override whether an \"elseif\" keyword token capture name gets rewritten to \"else if\" (kind 9, "
        "Rewrite) -- true/false, nil clears.");
    env.Register<&NedFormatRewriteExpandElseif>(
        "ned", "format-rewrite-expand-elseif",
        "The capture name's own overridden rewrite-expand-elseif rule, or nil if unset.");
    env.Register<&NedSetFillColumn>(
        "ned", "set-fill-column",
        "Set the target line width (in codepoints) fill-paragraph (M-q) wraps prose/comments to (default 70).");
    env.Register<&NedSetVimMode>(
        "ned", "set-vim-mode",
        "Enable or disable Vim-style modal editing (Normal/Insert/Visual/Replace/command-line, default false). "
        "Insert mode still runs through ned's own Emacs-bound keymap underneath (self-insert-command, auto-pair, "
        "snippets, LSP completion all keep working) -- only Normal/Visual/Replace/command-line dispatch is Vim's own.");
    env.Register<&NedSetLogCategoryVisible>(
        "ned", "set-log-category-visible",
        "Show/hide one category (\"general\"/\"janet\"/\"lsp\"/\"dap\"/\"acp\"/\"vcs\"/\"task\"/\"subprocess\") in the "
        "*Messages* buffer -- \"lsp\" defaults hidden, everything else visible.");
    env.Register<&NedSetLogMaxEntries>(
        "ned", "set-log-max-entries",
        "Set how many recent *Messages* entries are kept in memory (default 5000) -- infinity doesn't exist in RAM.");
    env.Register<&NedSetProjectSearchThreads>(
        "ned", "set-project-search-threads",
        "Set the worker-thread cap for project-wide search/replace's internal file scan (default 4).");
    env.Register<&NedSetTerminalHeightPercent>(
        "ned", "set-terminal-height-percent",
        "Set how much of the screen the terminal drawer covers, as a percentage (default 40, clamped to 10-90).");
    env.Register<&NedSetPageScrollFraction>(
        "ned", "set-page-scroll-fraction",
        "Set the fraction of the viewport height a page up/down moves (default 0.65, clamped to (0, 1]).");
    env.Register<&NedSetDiffRefreshDebounceMs>(
        "ned", "set-diff-refresh-debounce-ms",
        "Set how long, in milliseconds, the VCS diff gutter waits after the last edit before refreshing (default "
        "1200; non-positive values are clamped to 1).");
    env.Register<&NedSetSubprocessReadTimeoutMs>(
        "ned", "set-subprocess-read-timeout-ms",
        "Set how long, in milliseconds, a main-thread blocking subprocess read (system-clipboard paste, first "
        "toolchain-include-path query for a language) waits before killing the child and failing gracefully "
        "(default 5000; non-positive values are clamped to 1).");
    env.Register<&NedSetSubprocessWriteTimeoutMs>(
        "ned", "set-subprocess-write-timeout-ms",
        "Set how long, in milliseconds, a blocking write to a subprocess's stdin (system-clipboard copy, an "
        "LSP/ACP frame, a terminal keystroke) waits for the child to keep draining before giving up (default 5000; "
        "non-positive values are clamped to 1).");
    env.Register<&NedSetProtocolReadStallTimeoutMs>(
        "ned", "set-protocol-read-stall-timeout-ms",
        "Set how long, in milliseconds, silence after an LSP/DAP/ACP frame or message has started arriving is "
        "tolerated before the connection is treated as stalled and disconnected -- idle time between messages "
        "stays unbounded regardless (default 30000; non-positive values are clamped to 1). See also "
        "set-protocol-write-stall-timeout-ms for the write-side twin.");
    env.Register<&NedSetProtocolWriteStallTimeoutMs>(
        "ned", "set-protocol-write-stall-timeout-ms",
        "Set how long, in milliseconds, a write of an LSP/DAP/ACP frame or message to a server's stdin waits for "
        "it to keep draining before the connection is treated as stalled and disconnected (default 30000; "
        "non-positive values are clamped to 1). See also set-protocol-read-stall-timeout-ms for the read-side "
        "twin.");
    env.Register<&NedSetProtocolRequestTimeoutMs>(
        "ned", "set-protocol-request-timeout-ms",
        "Set how long, in milliseconds, a sent LSP/DAP/ACP request is kept pending before it's resolved with a "
        "synthetic timeout failure (default 30000; non-positive values are clamped to 1).");
    env.Register<&NedSetTheme>(
        "ned", "set-theme",
        "Select the startup theme by name (e.g. \"dark\", \"light\", \"gruvbox-dark\"). Beats the desktop probe; "
        "an unknown name is reported at startup and falls back. Empty string clears the preference.");
    env.Register<&NedThemeSet>(
        "ned", "theme-set",
        "Override one theme color or Brush trait by key (e.g. (ned/theme-set \"keyword_foreground\" \"#f042d6\") or "
        "(ned/theme-set \"active_tab_bold\" \"false\")) on top of the startup theme -- keys match the theme file's "
        "own, trait values are \"true\"/\"false\"; Docs/Themes.md lists every key "
        "for hand-editing, loaded via (dofile ...) from init.janet.");
    env.Register<&NedThemeGradient>(
        "ned", "theme-gradient",
        "Register a named paint usable anywhere a paint is (e.g. (ned/theme-gradient \"brand\" \"diag $accent 2 "
        "$keyword\")). The spec is an optional axis or pattern keyword, then stops, with numbers between them as "
        "relative weights: stops are \"#rrggbb\"/\"#rrggbbaa\"/\"default\", a percentage like \"60%\" (making it a "
        "fade of whatever colour is already there), or \"$slot\" with optional +lighten/-darken/\/alpha "
        "adjustments. A stop naming another paint expands to that paint's own stops.");
    env.Register<&NedThemeSurface>(
        "ned", "theme-surface",
        "Set one part of one themed surface: (ned/theme-surface \"popup\" \"fill\" \"y $bg/78 3 $bg/52\"). Parts are "
        "\"fill\", \"border\" and \"text\"; the spec is the same paint grammar ned/theme-gradient takes. Surface "
        "names come from the UI layer (\"buffer\", \"buffer.current_line\", \"modeline\", \"tab.active\", \"panel\", "
        "\"popup\", ...); an unknown name or part is reported at startup.");
    env.Register<&NedSetMinimapEnabled>(
        "ned", "set-minimap-enabled",
        "Enable/disable the minimap (replaces the plain scrollbar) as the default starting state for newly-opened "
        "panes (default true). See also toggle-minimap for flipping it at runtime.");
    env.Register<&NedSetMinimapWidth>(
        "ned", "set-minimap-width",
        "Set the minimap's width in columns (default 5). Each column packs 2 braille sub-columns of resolution.");
    env.Register<&NedSetMinimapCharsPerDot>(
        "ned", "set-minimap-chars-per-dot",
        "Set how many real buffer columns one minimap dot represents (default 8, applies uniformly in both glyph "
        "and real-pixel rendering). Fractional values (e.g. 8.5) are accepted for finer-grained tuning. A line "
        "longer than minimap-width * chars-per-dot * 2 columns simply isn't rendered past that point -- not "
        "compressed.");
    env.Register<&NedSetRulerEnabled>(
        "ned", "set-ruler-enabled",
        "Enable/disable the print-margin/fill-column ruler: a single-column background wash spanning every "
        "visible row, marking ned/set-ruler-column's own column. Default true. Themed via the \"buffer.ruler\" "
        "surface (ned/theme-surface).");
    env.Register<&NedSetRulerColumn>(
        "ned", "set-ruler-column",
        "Set which 0-indexed buffer column the ruler marks (default 80). Has no effect while the ruler is "
        "disabled (ned/set-ruler-enabled).");
    env.Register<&NedSetTrailingWhitespaceHighlightEnabled>(
        "ned", "set-trailing-whitespace-highlight-enabled",
        "Enable/disable a subtle background highlight on trailing whitespace (spaces/tabs after the last "
        "non-whitespace character on a line). Default false, matching Emacs' own opt-in "
        "show-trailing-whitespace precedent rather than VSCode/Sublime's forced-on default.");
    env.Register<&NedSetIndentGuidesEnabled>(
        "ned", "set-indent-guides-enabled",
        "Enable/disable vertical indentation guide glyphs at each indent-width column within a line's own "
        "leading whitespace. Default false, same opt-in reasoning as set-trailing-whitespace-highlight-enabled.");
    env.Register<&NedSetIndentGuideDepthColorsEnabled>(
        "ned", "set-indent-guide-depth-colors-enabled",
        "Enable/disable cycling each indent guide's color by its own nesting level (Theme's "
        "indent-guide-depth-palette) instead of one flat color. Only visible when indent guides themselves are "
        "on. Default true.");
    env.Register<&NedSetTabGlyphsEnabled>(
        "ned", "set-tab-glyphs-enabled",
        "Enable/disable a glyph at the first cell of every expanded real tab byte, distinguishing it from the "
        "space cells the rest of its expansion still renders as. Default false, same opt-in reasoning as "
        "set-trailing-whitespace-highlight-enabled.");
    env.Register<&NedSetAutoDetectProjectRoot>(
        "ned", "set-auto-detect-project-root",
        "Enable/disable walking upward from an opened file for a VCS marker directory to find the project root "
        "(default true).");
    env.Register<&NedSetScratchAutoSave>(
        "ned", "set-scratch-auto-save",
        "Enable/disable automatically saving modified scratch notes (find-scratch) on a periodic timer (default "
        "true).");
    env.Register<&NedSetFileAutoSave>(
        "ned", "set-file-auto-save",
        "Enable/disable periodic crash-recovery snapshots of modified file buffers into the backup store (default "
        "true). Snapshots never touch the file itself and are dropped by a real save.");
    env.Register<&NedSetBackupMaxAgeDays>(
        "ned", "set-backup-max-age-days",
        "Days a backup version is kept before pruning (default 14; <= 0 keeps versions regardless of age).");
    env.Register<&NedSetBackupMaxSizeMb>(
        "ned", "set-backup-max-size-mb",
        "Buffers past this size, in MiB, are skipped by the periodic crash-recovery autosave writer (default 64; "
        "non-positive values are clamped to 1). Deliberately conservative -- this write runs on a 5-second timer.");
    env.Register<&NedSetBackupVersionMaxSizeMb>(
        "ned", "set-backup-version-max-size-mb",
        "Files past this size, in MiB, are skipped by the pre-save version-backup copy (default 65536, 64 GiB; "
        "non-positive values are clamped to 1). Far more generous than set-backup-max-size-mb since this is a "
        "one-time disk copy done on save, not a periodic in-editor write.");
    env.Register<&NedSetBackupMaxVersions>(
        "ned", "set-backup-max-versions",
        "Backup versions kept per file, oldest pruned first (default 20; <= 0 keeps unlimited versions).");
    env.Register<&NedSetPersistentUndo>(
        "ned", "set-persistent-undo",
        "Enable/disable persisting each file buffer's full undo tree to $XDG_STATE_HOME/ned/undo/ across editor "
        "restarts (default true). On reopen, restored only if the file's current content still matches some node "
        "in the persisted tree (not necessarily its tip -- e.g. quitting without saving); otherwise the persisted "
        "history is discarded and the buffer starts fresh, same as a normal first-time open. No merging.");
    env.Register<&NedSetPersistentUndoMaxSizeMb>(
        "ned", "set-persistent-undo-max-size-mb",
        "Buffers past this content size, in MiB, are skipped by persistent-undo saving (default 16; non-positive "
        "values are clamped to 1). A large file's undo tree multiplies this cutoff by however many undo steps it "
        "has, unlike a single backup version.");
    env.Register<&NedListBackups>(
        "ned", "list-backups",
        "Backup snapshots recoverable for the current buffer, as an array of absolute paths -- the crash-recovery "
        "autosave first if one exists, then saved versions newest-first. Empty for a pathless buffer or when "
        "nothing was backed up. Index into it with ned/recover-backup.");
    env.Register<&NedRecoverBackup>(
        "ned", "recover-backup",
        "Restore the current buffer's content from backup snapshot `index` (0 = the autosave if present, else the "
        "newest version -- ned/list-backups' order). One undoable step; the buffer is left modified, so save to "
        "keep the recovery. Panics on a bad index or unreadable snapshot.");
    env.Register<&NedSetAutoRevert>(
        "ned", "set-auto-revert",
        "Enable/disable automatically reloading an open, unmodified buffer when its file changes on disk (default "
        "true). A buffer with local edits is never auto-reverted; saving it instead asks before overwriting.");
    env.Register<&NedSetAcpMcpBridge>(
        "ned", "set-acp-mcp-bridge",
        "Enable/disable advertising ned's own MCP tool-server bridge (get_diagnostics/hover/goto_definition/"
        "find_references/git_status/git_diff/search_project/run_tests/get_test_results) to an ACP agent on session "
        "start (default true). Off falls back to sending an empty mcpServers list, as if no bridge were wired at all.");
    env.Register<&NedSetAutoPairEnabled>(
        "ned", "set-auto-pair-enabled",
        "Enable/disable auto-closing matching brackets/quotes as you type -- typing an opener inserts its "
        "matching closer, typing a redundant closer skips over it, backspace between an empty pair removes both "
        "(default true). Which characters pair is per-mode (Mode's own autoPairs); this only turns the whole "
        "feature on or off.");
    env.Register<&NedSetAutoMerge>(
        "ned", "set-auto-merge",
        "Enable/disable automatically three-way merging a buffer's local edits with a file that also changed on "
        "disk (default true). A clean merge applies silently; a genuine conflict inserts <<<<<<< markers instead "
        "of guessing. Always one undoable step. A separate toggle from ned/set-auto-revert.");
    env.Register<&NedSetLspSyncBackgroundBuffers>(
        "ned", "set-lsp-sync-background-buffers",
        "Enable/disable syncing every open buffer to its configured LSP server(s), not just the pane-active one "
        "(default true). Runs on the same periodic background tick as auto-save, not per-frame, so a background "
        "tab's diagnostics/completions stay current without interrupting the buffer you're actually editing.");
    env.Register<&NedSetFileWatch>(
        "ned", "set-file-watch",
        "Enable/disable the inotify file watcher that triggers auto-revert/auto-merge near-instantly when an open "
        "buffer's file changes on disk (default true). The periodic 5s sweep keeps running either way (the safety "
        "net for filesystems inotify can't see, e.g. NFS); disabling this just falls back to that sweep alone. A "
        "flip takes effect at the next sweep tick (within ~5s).");
    env.Register<&NedSetSavePlace>(
        "ned", "set-save-place",
        "Enable/disable remembering each file's last point and scroll position across editor runs (default true). "
        "Off disables both restoring and recording.");
    env.Register<&NedSetSessionRestore>(
        "ned", "set-session-restore",
        "Enable/disable per-project session persistence -- open buffers, active file, sidebar state, and DAP "
        "breakpoints, restored when ned starts inside a project (default true). Off disables both restoring and "
        "saving; --no-restore does the same for a single launch.");
    env.Register<&NedSetProjectTrustExpiryDays>(
        "ned", "set-project-trust-expiry-days",
        "Days of disuse before an \"always\"-trusted project init.janet must be re-approved (default 30; 0 or "
        "negative = never expire). Trust decays from last use, not from when it was granted; a changed init file "
        "always re-prompts regardless.");
    env.Register<&NedSetAsyncLoadThreshold>(
        "ned", "set-async-load-threshold",
        "File size in bytes above which files load asynchronously in the background instead of blocking "
        "(default 16 MiB, i.e. (* 16 1024 1024)). 0 loads every file asynchronously.");
    env.Register<&NedSetHugeFileThreshold>(
        "ned", "set-huge-file-threshold",
        "File size in bytes above which a file opens via the piece-table storage engine -- never fully read into "
        "memory, edits/undo/save all work, but LSP sync and whole-buffer regex search stay limited for now "
        "(default 1 GiB, i.e. (* 1024 1024 1024)). Checked ahead of set-async-load-threshold, so a file clearing "
        "both always takes this path. 0 routes every file through it. Does not yet support CRLF/CR line endings "
        "(Buffer::FromHugeFile throws) -- such a file still opens fine via the normal loader.");
    env.Register<&NedSetHugeFileMinFreeSpaceMultiplier>(
        "ned", "set-huge-file-min-free-space-multiplier",
        "Safety margin (default 2.0) a huge file's save must clear: available free disk space must be at least "
        "this many times the content's byte length, since the atomic save pattern needs the new content's full "
        "size in free space and a copy-on-write filesystem (Btrfs, ZFS) can transiently need close to double that. "
        "Below this, a newly opened huge buffer downgrades to read-only (toggle-read-only overrides); a save "
        "attempt on a huge buffer always refuses regardless of that override -- see set-huge-file-disk-space-"
        "check-enabled to turn the check off entirely instead.");
    env.Register<&NedSetHugeFileDiskSpaceCheckEnabled>(
        "ned", "set-huge-file-disk-space-check-enabled",
        "Enable/disable the free-disk-space safety check for huge (piece-table-backed) buffers entirely (default "
        "true). Off skips both the open-time read-only downgrade and the save-time refusal.");
    env.Register<&NedSetFollowSymlinksOnSave>(
        "ned", "set-follow-symlinks-on-save",
        "Whether saving a file reached through a symlink writes the file the link points at (default true) or "
        "replaces the link itself with a regular file (false). Following also means the temporary file a save "
        "writes is created beside the real target, so a link pointing to another filesystem still saves.");
    env.Register<&NedSetPreserveHardLinksOnSave>(
        "ned", "set-preserve-hard-links-on-save",
        "Whether saving a file that has more than one hard link keeps every link pointing at the same content "
        "(default true). Doing so requires rewriting the file in place instead of the usual write-a-temp-file-"
        "then-rename, which means a crash mid-save can leave that file truncated -- recoverable from a backup "
        "version. False keeps the atomic save and lets the save break the link, leaving the other names on the "
        "old content.");
    env.Register<&NedSetMaxHighlightBytes>(
        "ned", "set-max-highlight-bytes",
        "Buffer size in bytes above which syntax highlighting is skipped entirely (default 8 MiB). 0 disables "
        "highlighting for every buffer.");
    env.Register<&NedSetHugeStructuralWindowBytes>(
        "ned", "set-huge-structural-window-bytes",
        "For a huge (piece-table-backed) buffer only, the byte margin the code-folding/symbol-kind/test-discovery "
        "gutters expand the visible viewport by on each side when deciding how much of the buffer to parse "
        "(default 4 MiB) -- a fold region, symbol definition, or test whose start/end falls outside that window "
        "won't show until scrolling brings it closer. No effect on an ordinary (non-huge) buffer, which is never "
        "windowed.");
    env.Register<&NedSetEnsureFinalNewline>(
        "ned", "set-ensure-final-newline",
        "Enable/disable appending a trailing newline to a file's written content on save if it's missing one "
        "(default true).");
    env.Register<&NedSetWrapIndent>(
        "ned", "set-wrap-indent",
        "Enable/disable hanging a soft-wrapped line's continuation rows under its own leading whitespace, rather "
        "than restarting flush at the gutter's own left edge (default true). Purely a rendering choice -- no "
        "effect on buffer content, undo, or a hard-wrapped paragraph's own fill-paragraph indentation.");
    env.Register<&NedSetTrimTrailingWhitespaceOnSave>(
        "ned", "set-trim-trailing-whitespace-on-save",
        "Enable/disable stripping trailing spaces/tabs from every line and collapsing trailing blank lines at "
        "end-of-file, applied to a file's written content on save (default true). Disk-only, same as "
        "set-ensure-final-newline -- the buffer's own live content is never touched.");
    env.Register<&NedSetAutoFormatOnSave>(
        "ned", "set-auto-format-on-save",
        "Enable/disable running the Native reindent/space/break/wrap/blank-line rules and a scoped Hygiene "
        "trim, restricted to the lines touched since the buffer was last loaded/saved, before every save-buffer "
        "(default false). Skipped entirely whenever an external format-command or a running LSP server already "
        "formats this save (see set-format-command/set-lsp-format-on-save) -- those keep their existing "
        "whole-buffer precedence.");
    env.Register<&NedSetMaxConsecutiveBlankLines>(
        "ned", "set-max-consecutive-blank-lines",
        "Set the longest run of consecutive blank lines the Hygiene pass (format-buffer's native fallback) leaves "
        "in place -- a longer run is collapsed down to exactly this many (default 2). A negative value disables "
        "the rule entirely (no limit).");
    env.Register<&NedSetCleanBlankLineOnNewline>(
        "ned", "set-clean-blank-line-on-newline",
        "Enable/disable clearing a whitespace-only line's own leading run before splitting it when the newline "
        "command is invoked with point on one (default true) -- a second Enter on a line that only ever got "
        "auto-indented and never actually typed into removes that dangling whitespace instead of leaving it "
        "behind, while the new line's own indent is still computed fresh.");
    env.Register<&NedSetLineEndingPolicy>(
        "ned", "set-line-ending-policy",
        "\"preserve\" (default) keeps each buffer's own detected/converted line ending on save; \"lf\"/\"crlf\"/"
        "\"cr\" force every save to that ending regardless of what was detected. Per-buffer "
        "convert-line-endings-to-lf/-crlf/-cr override a single buffer's own ending independent of this "
        "process-wide policy.");
    env.Register<&NedSetCodeFoldingEnabled>(
        "ned", "set-code-folding-enabled",
        "Enable/disable the gutter code-folding affordance for modes with a fold query (default true).");
    env.Register<&NedSetMultibufferAutoCollapseLineThreshold>(
        "ned", "set-multibuffer-auto-collapse-line-threshold",
        "An excerpt (diff hunk, reference, ...) in a *vcs diff*/*vcs commit*/*references*/*diagnostics*/... "
        "multibuffer whose own body has more lines than this collapses by default when the multibuffer is built "
        "(default 40) -- code-fold-toggle/unfold-all still work on it from there like any other fold.");
    env.Register<&NedSetMultibufferAutoCollapseByteThreshold>(
        "ned", "set-multibuffer-auto-collapse-byte-threshold",
        "Same as set-multibuffer-auto-collapse-line-threshold, but measured in bytes (default 2000) -- catches a "
        "single huge/minified line a line-count threshold alone would miss.");
    env.Register<&NedSetMultibufferAutoCollapseExcerptCap>(
        "ned", "set-multibuffer-auto-collapse-excerpt-cap",
        "Once a multibuffer's own excerpt count passes this (default 100), every remaining excerpt collapses by "
        "default regardless of its own size -- catches a plain-large result set (e.g. project-find-references on a "
        "very common identifier) rather than dumping hundreds of expanded excerpts into view at once.");
    env.Register<&NedSetMultibufferMaxExcerpts>(
        "ned", "set-multibuffer-max-excerpts",
        "Hard cap on how many excerpts a multibuffer stitches at all (default 500; 0 = unlimited) -- unlike "
        "set-multibuffer-auto-collapse-excerpt-cap, which only collapses excerpts that were all still built, this "
        "stops the work. Anything past the cap is dropped and named in a trailing \"N more not shown\" line of the "
        "buffer itself; project-find-references also stops reading source lines off disk at this point.");
    env.Register<&NedSetMultibufferScopedSearch>(
        "ned", "set-multibuffer-scoped-search",
        "Enable/disable confining isearch and query-replace to a multibuffer's excerpt bodies (default true) -- "
        "header paths, rule lines and the blanks between excerpts stop matching, and query-replace stops offering a "
        "replacement inside chrome the buffer would then silently refuse. Turn it off to search a review buffer's "
        "whole composite text, e.g. to find the excerpt whose header names a particular file. No effect on an "
        "ordinary buffer.");
    env.Register<&NedSetClassFileSync>(
        "ned", "set-class-file-sync",
        "Enable/disable offering, unprompted, to keep a file's name and the single type declared inside it in "
        "agreement (default true) -- after renaming a class/enum/struct/record, a y/n to rename the file after it; "
        "after renaming the file, a y/n to rename the type. Both fire only when the file was demonstrably named "
        "after that type a moment ago and no longer is, and never when the file holds more than one top-level type "
        "-- whether a file *should* be named after its type is a per-project question this does not try to answer. "
        "Turning it off stops the offers only: rename-file-to-match-type and rename-type-to-match-file keep working "
        "when you ask for them, and are more permissive than the offers are.");
    env.Register<&NedSetRenameReview>(
        "ned", "set-rename-review",
        "Enable/disable handing a rename's edits to an editable review multibuffer before they land (default true) "
        "-- one excerpt per occurrence, M-n/M-p to step, M-a to include an excerpt, M-r to exclude it, C-c C-c to "
        "commit the lot as one undo transaction. The review is also the only place the comment and string "
        "occurrences a rename deliberately skipped are visible, each excluded until opted into. Turn it off to "
        "apply a rename immediately, as rename-symbol and lsp-rename did before. A rename that also creates, "
        "deletes or renames files is applied directly either way -- a multibuffer cannot represent that.");
    env.Register<&NedSetSearchEverywhereGesture>(
        "ned", "set-search-everywhere-gesture",
        "Enable/disable the double-tap-Shift gesture that opens search-everywhere (default true) -- an escape "
        "hatch, not the only way in: M-s always works regardless of this setting. The gesture itself only fires "
        "under the Kitty keyboard protocol, which Notcurses negotiates on its own with no way for ned to check in "
        "advance whether a given terminal/multiplexer supports it; turn this off if an untested one produces a "
        "false trigger.");
    env.Register<&NedSetSearchEverywhereTextSearch>(
        "ned", "set-search-everywhere-text-search",
        "Enable/disable search-everywhere's Text category (default true) -- a debounced, backgrounded full-corpus "
        "regex-escaped-literal scan of the project. This is the one part of search-everywhere backed by new "
        "background-threading code rather than reuse of an already-shipped subsystem; the Actions/Macros/Files/"
        "Buffers/Symbols categories are unaffected by this setting.");
    env.Register<&NedSetImportFixup>(
        "ned", "set-import-fixup",
        "Enable/disable rewriting imports when a file is renamed or moved (default true) -- both the imports in "
        "other files that named it and the relative imports the moved file wrote itself. Resolved with the same "
        "tree-sitter import queries go-to-file-at-point uses, and handed to the same editable review multibuffer a "
        "rename is (C-c C-c to commit, M-r to drop an excerpt), so nothing lands unseen. A language server that "
        "answered workspace/willRenameFiles with edits of its own wins outright -- this is the no-server path. An "
        "import whose own style cannot express the new location (an angle-form include, a PHP namespace, a Rust "
        "mod declaration, a target that left the root its specifier counts from) is reported as declined rather "
        "than guessed at.");
    env.Register<&NedSetImportFixupMaxFiles>(
        "ned", "set-import-fixup-max-files",
        "How many project files one rename's import scan will consider (default 20000; 0 means unlimited). Only "
        "files whose language has an import query are counted at all. Raise it for a very large repository, lower "
        "it if a rename feels slow.");
    env.Register<&NedSetStickyScrollEnabled>(
        "ned", "set-sticky-scroll-enabled",
        "Enable/disable pinned namespace/class/method breadcrumb rows at the top of a pane while scrolled into "
        "their body (default true). Only meaningful for a mode with a tags query (symbolKind) -- see "
        "set-sticky-scroll-max-rows for capping how many rows it can reserve.");
    env.Register<&NedSetStickyScrollMaxRows>(
        "ned", "set-sticky-scroll-max-rows",
        "How many pinned breadcrumb rows a pane will ever reserve, regardless of how deep the actual enclosing "
        "namespace/class/method chain is (default 3). 0 effectively disables the rows without touching "
        "set-sticky-scroll-enabled.");
    env.Register<&NedSetRelativeLineNumbers>(
        "ned", "set-relative-line-numbers",
        "Enable/disable relative line numbers in the gutter (default false): the current line keeps its real "
        "number, every other visible line shows its distance from it, Vim's 'relativenumber' convention.");
    env.Register<&NedSetUnsavedChangeSwatch>(
        "ned", "set-unsaved-change-swatch",
        "Enable/disable the status-column swatch marking every line edited since the buffer was last loaded or "
        "saved (default true). Never shown on a read-only buffer either way -- nothing there is an edit of "
        "yours; see set-unseen-content-marker for what that column says instead.");
    env.Register<&NedSetUnseenContentMarker>(
        "ned", "set-unseen-content-marker",
        "Enable/disable the status-column marker for content appended to a read-only buffer (*lsp log*, task "
        "output, test results) since you last looked away from it (default true). Needs a buffer you have "
        "visited and left at least once, so a one-shot generated report never marks itself. Retune its colour "
        "with the unseen_content_indicator theme key.");
    env.Register<&NedSetUnseenContentMarkerStyle>(
        "ned", "set-unseen-content-marker-style",
        "How set-unseen-content-marker draws: \"band\" (default) marks every unseen line, \"boundary\" marks "
        "only the first -- a \"you left off here\" rule, for a busy log where the band would be most of the "
        "screen.");
    env.Register<&NedSetColorSwatches>(
        "ned", "set-color-swatches",
        "Enable/disable inline colour swatches -- a cell painted in the colour named by every colour literal in "
        "view (#ff00aa, rgb(...), hsl(...), and in a stylesheet also #f0a and tomato; default true). Found by "
        "ned itself, so it works with no language server running; a server that answers "
        "textDocument/documentColor adds whatever else it knows about. Turning this off also stops that "
        "request. pick-color (C-c #) adjusts the literal under point interactively (R/G/B, H/S/L, "
        "alpha) or inserts a new one where point is; color-at-point rewrites one in another notation.");
    env.Register<&NedSetColorSwatchStyle>(
        "ned", "set-color-swatch-style",
        "How a colour swatch draws: \"block\" (default) puts a filled cell before the literal, costing one "
        "column the way an inlay hint does; \"underlay\" washes the literal's own characters in the colour "
        "instead, shifting nothing on screen.");
    env.Register<&NedSetWhichKeyEnabled>(
        "ned", "set-which-key-enabled",
        "Enable/disable the which-key popup listing possible next chords while a prefix key (C-x, C-c, ...) is "
        "pending (default true). The echo area's own \"C-x-\" pending-sequence text is unaffected either way.");
    env.Register<&NedSetRecencyGlow>(
        "ned", "set-recency-glow",
        "Enable/disable the recency glow -- a brief accent wash over text that was just edited, fading out over "
        "~200ms (default true). Tints the edited characters rather than washing the line behind them, which is "
        "what keeps it cheap. Retune its colour with "
        "(ned/theme-surface \"buffer.recency\" \"fill\" ...).");
    env.Register<&NedSetInlineDiagnostics>(
        "ned", "set-inline-diagnostics",
        "Enable/disable inline diagnostics (the LSP message shown against the line it flags; default true).");
    env.Register<&NedSetInlineDebugValues>(
        "ned", "set-inline-debug-values",
        "Enable/disable the debugger's inline values (each stopped frame's own locals shown after the lines that "
        "mention them, in the file the debuggee is stopped in; default true). Display only -- the same values are "
        "in the debug panel's Variables section either way.");
    env.Register<&NedSetInlineDiagnosticStyle>(
        "ned", "set-inline-diagnostic-style",
        "How an inline diagnostic is drawn: \"end-of-line\" (default) puts the message after the line's own text, "
        "on a row the line already occupies, so a diagnostic appearing or clearing never shifts anything else on "
        "screen; \"callout\" is the original block below the line with carets under the flagged span, which points "
        "at exact columns but costs a screen row that comes and goes as you type.");
    env.Register<&NedSetOrgTodoKeywords>(
        "ned", "set-org-todo-keywords",
        "Set Org's TODO keyword sequence: (keywords), e.g. (ned/set-org-todo-keywords [\"TODO\" \"IN-PROGRESS\" "
        "\"DONE\"]). The LAST keyword is the done state (the standard single-sequence Org convention) -- what "
        "org-cycle-todo cycles through and what headline highlighting colors as TodoKeyword vs DoneKeyword (via "
        "the bundled org.keyword.candidate capture classifier, which a user registration can replace). An empty "
        "tuple restores the built-in default sequence.");
    env.Register<&NedOrgTodoKeywords>(
        "ned", "org-todo-keywords",
        "Return Org's configured TODO keyword sequence as a tuple, last keyword = the done state -- what the "
        "bundled headline classifier (Plugins/languages.janet) compares a headline's first word against. See "
        "ned/set-org-todo-keywords.");
    env.Register<&NedRegisterCaptureClassifier>(
        "ned", "register-capture-classifier",
        "Classify a capture's spans from their TEXT, where a static query cannot say: (language capture fn). fn "
        "receives one captured node's text and returns a :syntax-class keyword (the names ned/set-syntax-foreground "
        "accepts, e.g. :headline-level1), false to suppress that span entirely (it contributes nothing, rather than "
        "a :default span that would clobber an underlying wash), or nil to fall through to the normal "
        "capture-name resolution. Consulted by the highlight pipeline for that language's captures of that name -- "
        "batched internally, one Janet call per name per repaint, so keep fn pure and fast. This is the escape "
        "hatch for classes no query predicate can express: arithmetic over the text (a headline level from counted "
        "stars) or comparison against runtime-configured state (Org's TODO keywords). Pair with a definition's "
        ":capture-spans {\"name\" :line-end} to widen the classified span declaratively. Re-registering replaces; "
        "a nil fn clears.");
    env.Register<&NedRegisterLanguage>(
        "ned", "register-language",
        "Register a language from a directory holding its language.janet -- the exact layout ned's own bundled "
        "languages use (Source/Languages/`<name>`/), so everything a definition can say works: extensions and "
        "filenames (claimed automatically, no separate set-mode-for-extension call needed), comment syntax, "
        "keymap, query files discovered beside it as `<kind>`.janet with an upstream/ subdirectory checked first, "
        "escapes, LSP root markers, import resolution, injection aliases, snippets, and :color-literals "
        "(a tuple of :short-hex and/or :named, widening which colour-literal spellings earn a swatch beyond the "
        "two every language gets). The directory's basename is "
        "the language name and its mode is named `<name>`-mode; a registered name shadows a bundled one, so "
        "redefining a bundled language is expected use, as is re-registering. Two keys exist for exactly this "
        "path: a directory with its own grammar.janet (or the `tables` ned --compile-language writes from it) brings "
        "its own grammar, with :scanner-library naming the shared library exporting ned_scanner_`<grammar>` when the "
        "grammar has external tokens (omit both to use a bundled grammar), and :queries-dir names a foreign "
        "tree-sitter-layout directory (e.g. "
        "/usr/share/tree-sitter/queries/`<lang>`) scanned per kind as `<kind>`.janet or `<kind>`.scm for whatever "
        "discovery didn't find. Throws with a file:line message on a malformed definition. Directories under "
        "$XDG_CONFIG_HOME/ned/languages/ and a trusted project's .ned/languages/ load automatically at startup "
        "through this same path.");
    env.Register<&NedSetModeForExtension>(
        "ned", "set-mode-for-extension",
        "Map a file extension (with or without a leading '.') to a mode name -- either a registered language's "
        "`<name>`-mode (ned/register-language), or one of ned's own built-in mode names (e.g. \"php-mode\", "
        "\"python-mode\"). "
        "Checked before ned's own built-in extension table, so this can override a bundled mapping too, not just add "
        "a new one.");
    env.Register<&NedSetModeForFilename>(
        "ned", "set-mode-for-filename",
        "Map an exact, full filename (e.g. \"CMakeLists.txt\", not a pattern/glob) to a mode name, the same way "
        "ned/set-mode-for-extension does for an extension -- checked first, before any extension mapping, for files "
        "identified by name rather than by a distinguishing extension.");
    env.Register<&NedSetWrapForExtension>(
        "ned", "set-wrap-for-extension",
        "Map a file extension (with or without a leading '.') to whether BufferView should soft-wrap long lines at "
        "word boundaries instead of scrolling horizontally, overriding whichever Mode::wrapLines default would "
        "otherwise apply (e.g. (ned/set-wrap-for-extension \"md\" false) to opt markdown-mode's own wrap-on default "
        "back out).");
    env.Register<&NedSetWrapForFilename>(
        "ned", "set-wrap-for-filename",
        "Map an exact, full filename to a wrap-lines override, the same way ned/set-wrap-for-extension does for an "
        "extension -- checked first, before any extension mapping.");
    env.Register<&NedSetLspCommand>(
        "ned", "set-lsp-command",
        "Set the command used to launch a language's LSP server: (language argv), e.g. (ned/set-lsp-command \"c\" "
        "[\"clangd\"]). argv is an array or tuple of strings -- argv[0] the executable (resolved against $PATH), the "
        "rest its arguments. ned never installs or updates a language server itself; this only configures which "
        "already-installed one to run. An empty argv clears the configured command for language.");
    env.Register<&NedSetLspRootMarkers>(
        "ned", "set-lsp-root-markers",
        "Override the root-marker filenames ned looks for when resolving which directory to initialize a "
        "language's LSP server against: (language markers), e.g. (ned/set-lsp-root-markers \"rust\" "
        "[\"Cargo.toml\"]). Walks upward from an opened buffer's own directory for the nearest ancestor "
        "containing one of these as an immediate child; falls back to the ordinary project root when none match "
        "(or markers is empty and the language has no built-in default) -- this is what lets a monorepo "
        "subpackage (its own package.json/pyproject.toml/Cargo.toml/compile_commands.json, ...) get its own "
        "LSP root distinct from the outer repo's single .git. An empty markers list clears the override, "
        "reverting to the language's own built-in default (most bundled languages carry one) rather than to "
        "no markers at all.");
    env.Register<&NedSetDapAdapter>(
        "ned", "set-dap-adapter",
        "Set the command used to launch a language's DAP debug adapter: (language argv), e.g. (ned/set-dap-adapter "
        "\"cpp\" [\"lldb-dap\"]) or (ned/set-dap-adapter \"python\" [\"python\" \"-m\" \"debugpy.adapter\"]). Same "
        "argv shape and $PATH resolution as ned/set-lsp-command; an empty argv clears it.");
    env.Register<&NedSetDapLaunch>(
        "ned", "set-dap-launch",
        "Set the DAP launch configuration for a language: (language json), e.g. (ned/set-dap-launch \"cpp\" "
        "`{\"program\": \"./build/ned\"}`). The string is passed verbatim as the launch request's own "
        "adapter-specific arguments object -- see your adapter's documentation for its keys. An empty string clears "
        "it. Both this and ned/set-dap-adapter must be configured before dap-continue (F5) can start a session.");
    env.Register<&NedSetDapAttach>(
        "ned", "set-dap-attach",
        "Set the DAP attach configuration for a language: (language json), ned/set-dap-launch's own shape but for "
        "the dap-attach command's `attach` request instead of `launch` -- see your adapter's documentation for its "
        "attach-specific keys (commonly a process id or a connection host/port). An empty string clears it. Both "
        "this and ned/set-dap-adapter must be configured before dap-attach can start a session.");
    env.Register<&NedSetTaskCommand>(
        "ned", "set-task-command",
        "Set the command run by run-task for a task name: (name argv), e.g. (ned/set-task-command \"build\" "
        "[\"cmake\" \"--build\" \".\"]). argv is an array or tuple of strings -- argv[0] the executable (resolved "
        "against $PATH), the rest its arguments. An empty argv clears the configured command for name.");
    env.Register<&NedSetReplCommand>(
        "ned", "set-repl-command",
        "Set the command run-repl spawns (on a real pty, its own interactive CLI REPL shown as-is) for a REPL name: "
        "(name argv), e.g. (ned/set-repl-command \"python\" [\"python3\" \"-i\"]) or (ned/set-repl-command \"php\" "
        "[\"php\" \"-a\"]). Same argv shape as ned/set-task-command; an empty argv clears the configured command "
        "for name. The built-in Janet REPL (toggle-janet-repl, C-c j) needs no configuration -- it evaluates "
        "in-process against the running editor's own environment, not a subprocess.");
    env.Register<&NedSetProjectOpenCommand>(
        "ned", "set-project-open-command",
        "Set the command switch-project/open-project run to open another project in a new tab/window when no "
        "built-in terminal/multiplexer is auto-detected (or to override auto-detection with your own preferred "
        "invocation): (argv), e.g. (ned/set-project-open-command [\"tmux\" \"new-window\" \"-c\" \"{root}\" \"ned\" "
        "\"{root}\"]). argv is an array or tuple of strings; every \"{root}\" occurrence in every element is "
        "replaced with the project's own path -- never through a shell. An empty argv clears it. Auto-detection "
        "already covers tmux, GNU screen, Konsole, GNOME Terminal, WezTerm, Ghostty, and kitty -- this is for "
        "anything else, or a different invocation than the built-in one (e.g. a tmux pane split instead of a new "
        "window). Two of the auto-detected terminals need a one-time setting change of their own before they'll "
        "actually run anything (opening the tab still works either way, but the command inside it won't launch "
        "until this is done): Konsole requires `EnableSecuritySensitiveDBusAPI=true` under the `[KonsoleWindow]` "
        "section of konsolerc, plus restarting Konsole; kitty requires `allow_remote_control` (and usually "
        "`listen_on`) set in kitty.conf. Neither is ever changed by ned itself -- both hand a running terminal "
        "instance the ability to type arbitrary commands into it via IPC, a real security-relevant choice that's "
        "the user's own to make.");
    env.Register<&NedSetTestCommand>(
        "ned", "set-test-command",
        "Set the project's test command and output format: (argv format), e.g. (ned/set-test-command "
        "[\"ctest\" \"--test-dir\" \"build\"] \"ctest\"). argv is an array or tuple of strings -- argv[0] the "
        "executable (resolved against $PATH). format names a built-in parser (\"ctest\", \"catch2\", \"pytest\", "
        "\"go-json\", \"cargo\", \"junit-xml\", \"phpunit\") or one registered via ned/register-test-parser (a "
        "registered name wins over a built-in). run-tests (C-c T t) streams raw output into *test output* and, on "
        "exit, parses it into the *test results* buffer and the per-test gutter marks. An empty argv clears the "
        "configured command.");
    env.Register<&NedSetTestFilterCommand>(
        "ned", "set-test-filter-command",
        "Set the argv template run-test-at-point and rerun-failed-tests use to run a single test: (argv-template), "
        "where each element may contain {test} and/or {file} placeholders substituted per element (never through a "
        "shell), e.g. (ned/set-test-filter-command [\"ctest\" \"--test-dir\" \"build\" \"-R\" \"^{test}$\"]) or "
        "(ned/set-test-filter-command [\"pytest\" \"-v\" \"-k\" \"{test}\"]). Output parses with the same format "
        "ned/set-test-command configured. An empty argv clears it.");
    env.Register<&NedSetTestResultsFile>(
        "ned", "set-test-results-file",
        "Parse this file's contents after a test run exits instead of the run's own stdout/stderr -- for formats "
        "written to a file, e.g. JUnit XML: (ned/set-test-results-file \"/tmp/results.xml\") paired with "
        "(ned/set-test-command [\"pytest\" \"--junitxml\" \"/tmp/results.xml\"] \"junit-xml\"). An empty string "
        "clears it.");
    env.Register<&NedSetCoverageFile>(
        "ned", "set-coverage-file",
        "Set the path load-coverage-report (C-c T c) reads: an lcov .info file, the common export target for lcov "
        "itself, `llvm-cov export -format=lcov`, and `gcovr --lcov` -- (ned/set-coverage-file \"coverage.info\"). "
        "Parses into the per-line covered/uncovered/partial-branch gutter marks, and (when a VCS diff is available) "
        "flags an uncovered line that's also newly added/modified with its own \"untested new code\" mark. An empty "
        "string clears the configured path.");
    env.Register<&NedRegisterTestParser>(
        "ned", "register-test-parser",
        "Register a Janet function as the parser for a test output format: (name fn). fn receives the run's raw "
        "combined output (or the results file's contents, see ned/set-test-results-file) as one string and returns "
        "an array of result tables {:name \"...\" :status :passed|:failed|:skipped :file \"...\" :line n :message "
        "\"...\"} -- only :name and :status are required -- or a table {:results [...] :failures-only true :passed "
        "n} when the format only names failures. The name is then usable as ned/set-test-command's format; "
        "registering a built-in format's name deliberately overrides it. A nil fn clears the registration.");
    env.Register<&NedOrgCaptureRegisterTemplate>(
        "ned", "org-capture-register-template",
        "Register an org-capture template: (key name target-file template headline), e.g. "
        "(ned/org-capture-register-template \"t\" \"Todo\" \"~/org/todo.org\" \"* TODO %?\\n\" \"\"). key is exactly "
        "one character (org-capture, C-c k, reads it to pick this template); template's first \"%?\" marks where "
        "point lands after capture (omit it to land at the end of the inserted text). headline, if non-empty, files "
        "the capture as the last child of the exactly-titled headline in target-file; empty files at the end of "
        "target-file instead. Re-registering an existing key overwrites it.");
    env.Register<&NedRegisterSnippet>(
        "ned", "register-snippet",
        "Register a snippet: (language-key trigger body), e.g. (ned/register-snippet \"cpp\" \"for\" \"for (int "
        "${1:i} = 0; $1 < ${2:n}; ++$1) {\\n    $0\\n}\"). A registered trigger also appears in the completion "
        "popup (marked snippet) once its first characters are typed, accepted the same way any other candidate is. "
        "Typing the trigger word then TAB (or M-x "
        "expand-snippet in modes whose keymap claims TAB) expands the body: ${n:placeholder}/$n are tabstop fields "
        "TAB/S-TAB hop between (a repeated index mirrors typing live), $0 is where point lands at the end, \\$ "
        "escapes a literal dollar. language-key matches ned/set-lsp-command's (\"cpp\", \"python\", ...); \"\" "
        "registers for every mode. An empty body clears the trigger; re-registering overwrites it.");
    env.Register<&NedSnippetTriggers>(
        "ned", "snippet-triggers",
        "Return the snippet trigger words visible to a language key -- its own registrations merged with the "
        "\"\"-global tier, sorted. (ned/snippet-triggers \"cpp\")");
    env.Register<&NedRegisterMacro>(
        "ned", "register-macro",
        "Register a named keyboard macro: (name chords), e.g. (ned/register-macro \"save-and-format\" [\"C-c C-f\" "
        "\"C-x C-s\"]) -- chords is a list with one Emacs kbd-style chord per element, not one sequence string. "
        "search-everywhere can find and run a named macro; kmacro-insert-macro-definition writes one of these "
        "calls for you from an already-recorded, already-named macro. An empty chords list clears the name; "
        "re-registering overwrites it.");
    env.Register<&NedMacroNames>("ned", "macro-names", "Return every registered macro name, sorted.");
    env.Register<&NedSetAcpAgent>(
        "ned", "set-acp-agent",
        "Set the command used to launch an Agent Client Protocol (ACP) coding agent: (name argv), e.g. "
        "(ned/set-acp-agent \"claude-code\" [\"claude-code-acp\"]). Same argv shape and $PATH resolution as "
        "ned/set-lsp-command; an empty argv clears the configured command for name. acp-send-prompt (C-c a p) is "
        "the entry point that spawns and talks to whichever agent name it's given.");
    env.Register<&NedSetAcpPanelDock>(
        "ned", "set-acp-panel-dock",
        "Dock the ACP chat panel at the \"bottom\" (default) or \"right\" edge. Any other value is ignored. Takes "
        "effect on the next resize or panel show.");
    env.Register<&NedSetAcpPanelSizePercent>(
        "ned", "set-acp-panel-size-percent",
        "Set how much of the screen the ACP chat panel covers, as a percentage (default 30, clamped to 15-70) -- "
        "height when docked at the bottom, width when docked at the right.");
    env.Register<&NedSetLspAutoComplete>(
        "ned", "set-lsp-auto-complete",
        "Enable or disable the automatic completion popup while typing (default true). Manual completion "
        "(lsp-complete, bound to C-M-i) works regardless of this setting.\n\nThe popup is never one source: a "
        "language server's items, snippet triggers for the buffer's language, the buffer's own words, and -- in a "
        "Janet buffer -- every live ned/* binding name all rank in one list against what you typed. A candidate's "
        "source only breaks a tie, in the order snippet, server, Janet binding, buffer word; what you typed decides "
        "first. A label a higher-ranked source already offered suppresses the same label from a lower one, so a "
        "buffer word never echoes a symbol the server just named. The local sources need a typed word to rank "
        "against, so a bare trigger character (\".\", \"::\") still draws members alone.");
    env.Register<&NedSetLspCompletionDebounce>(
        "ned", "set-lsp-completion-debounce",
        "Set the delay, in milliseconds, after the last relevant keystroke before an automatic completion request "
        "is sent (default 500). Non-positive values are clamped to 1.");
    env.Register<&NedSetLspSignatureHelpAutoTrigger>(
        "ned", "set-lsp-signature-help-auto-trigger",
        "Enable or disable automatically requesting signature help after typing ( or , inside a call (default "
        "true). Manual invocation (lsp-signature-help) works regardless of this setting.");
    env.Register<&NedSetLspCommitCharacters>(
        "ned", "set-lsp-commit-characters",
        "Enable or disable accepting the selected completion when a character the server declared as one of that "
        "item's commitCharacters is typed (default true; the character itself is still inserted afterwards). Inert "
        "against a server that declares none -- no default set is ever assumed. Turn it off if typing ';' or ',' to "
        "end a statement keeps accepting the suggestion that happened to be showing.");
    env.Register<&NedSetLspHoverOnMouseMove>(
        "ned", "set-lsp-hover-on-mouse-move",
        "Enable or disable showing an lsp-hover tooltip when the mouse rests over a symbol (default true). "
        "Disabling this skips the debounce/request entirely, not just the popup. Manual invocation (lsp-hover, "
        "C-c C-j) works regardless of this setting.");
    env.Register<&NedSetLspFormatOnSave>(
        "ned", "set-lsp-format-on-save",
        "Enable or disable formatting the buffer via the language server on save (default false). Ignored "
        "whenever ned/set-format-command has an external formatter configured -- that always takes precedence.");
    env.Register<&NedSetLspFormatBuffer>(
        "ned", "set-lsp-format-buffer",
        "Whether format-buffer hands the given language to its language server instead of ned's own format rules "
        "(default true). An empty language sets the process-wide default. Set it false for a language whose style "
        "you configure with ned/set-format-* or format.janet -- a server's formatter and ned's own rules both "
        "rewrite the whole buffer, so only one can win. nil clears: a language back to the default, the default "
        "back to true.");
    env.Register<&NedLspFormatBuffer>(
        "ned", "lsp-format-buffer",
        "Whether format-buffer defers to the language server for the given language (an empty language reads the "
        "process-wide default).");
    env.Register<&NedSetLspOnTypeFormatting>(
        "ned", "set-lsp-on-type-formatting",
        "Enable or disable automatically formatting via the language server after typing one of its declared "
        "trigger characters (e.g. a closing brace or newline; default false). A server that doesn't advertise "
        "documentOnTypeFormattingProvider never triggers regardless of this setting.");
    env.Register<&NedSetLspPullDiagnostics>(
        "ned", "set-lsp-pull-diagnostics",
        "Enable or disable requesting diagnostics via textDocument/diagnostic on every content sync (default "
        "false). Only useful for a server that never sends its own publishDiagnostics notifications -- a server "
        "that proves it doesn't support pull either is never asked again for that connection's lifetime.");
    env.Register<&NedSetProjectDiagnostics>(
        "ned", "set-project-diagnostics",
        "Include files with no buffer open in the *diagnostics* problem list (default true) -- for a server that "
        "checks the whole project, most of what it reports is about files nobody has opened. Off scopes the list "
        "to open buffers. Purely a display switch: the records are kept either way, so turning it back on needs no "
        "server round trip.");
    env.Register<&NedSetLspSemanticHighlighting>(
        "ned", "set-lsp-semantic-highlighting",
        "Enable or disable server-informed syntax highlighting (textDocument/semanticTokens/full), layered on top "
        "of tree-sitter's own highlighting rather than replacing it (default true). A server with no "
        "semanticTokensProvider legend never sends a request regardless of this setting.");
    env.Register<&NedSetLspWorkspaceFolders>(
        "ned", "set-lsp-workspace-folders",
        "Enable or disable letting a buffer whose LSP root differs from an already-running same-language server "
        "join that server as an extra workspace folder instead of spawning its own process (default true) -- one "
        "server for a whole monorepo rather than one per subpackage. A server that doesn't advertise "
        "workspaceFolders support is never asked, and falls back to a separate process per root. Turn this off "
        "when isolation matters more than footprint: joined roots share one process, so one crash takes them all "
        "down together.");
    env.Register<&NedSetLspInlayHints>(
        "ned", "set-lsp-inlay-hints",
        "Enable or disable inline parameter-name/type hints (textDocument/inlayHint), rendered as dim virtual text "
        "the language server supplies (default true). A server that proves it doesn't support the method is never "
        "asked again for that connection's lifetime.");
    env.Register<&NedSetLspCodeLens>(
        "ned", "set-lsp-code-lens",
        "Enable or disable code lens annotations (textDocument/codeLens), rendered as a dim line above the code "
        "they annotate (default true). Run the lens at point with lsp-run-code-lens-at-point (M-x, unbound by "
        "default). A lens the server sends without a command carries no title yet; those are resolved "
        "(codeLens/resolve) as the viewport reaches them, once each, and the row appears when the answer lands. "
        "A server that proves it doesn't support the method is never asked again for that connection's "
        "lifetime.");
    env.Register<&NedSetLspCodeActionHints>(
        "ned", "set-lsp-code-action-hints",
        "Enable or disable the quick-fix gutter marker (default true) -- a glyph beside the diagnostic glyph on "
        "every line the language server says it has a fix for, applied with lsp-quick-fix or picked from "
        "lsp-code-action (C-c C-a), or by clicking the marker. Turning this off also stops the viewport-scoped "
        "textDocument/codeAction request behind it, which is the reason to: a server that answers that request "
        "slowly pays for it every time the view settles. A server that proves it doesn't support the method is "
        "never asked again for that connection's lifetime.");
    env.Register<&NedSetLspDiagnosticsDebounce>(
        "ned", "set-lsp-diagnostics-debounce",
        "Set the delay, in milliseconds, after the LSP server's most recently received diagnostics publish for a "
        "buffer before it's actually applied (default 500) -- keeps inline diagnostics from repainting on nearly "
        "every keystroke while typing, settling in only once the server goes quiet for this long. Non-positive "
        "values are clamped to 1.");
    env.Register<&NedSetLspSyncDebounce>(
        "ned", "set-lsp-sync-debounce",
        "Set the delay, in milliseconds, after an edit before ned actually sends textDocument/didChange to a "
        "buffer's LSP servers (default 150) -- prevents a full-document sync on every single keystroke, which can "
        "block the UI if a server can't drain its input fast enough. Keep this shorter than "
        "set-lsp-completion-debounce (default 500) or completion/hover/etc. requests may race ahead of a server "
        "that doesn't have the latest content yet. Non-positive values are clamped to 1.");
    env.Register<&NedSetLspRequestIdle>(
        "ned", "set-lsp-request-idle",
        "Set the shortest gap, in milliseconds, between two rounds of semantic-token, inlay-hint and code-lens "
        "requests for one buffer (default 150). A discrete move -- a PageDown, opening a file -- still asks "
        "immediately; only a visible range that keeps changing inside the window is held back, and then one request "
        "covers wherever it ended up, so a held scroll costs a round trip per window instead of one per frame. Raise "
        "it for a slow server, lower it for highlighting that keeps up mid-scroll. Non-positive values are clamped to 1.");
    env.Register<&NedSetIncludePathCacheTtlSeconds>(
        "ned", "set-include-path-cache-ttl-seconds",
        "Set how long (in seconds) a compiler-derived default include-path result stays cached before "
        "open-link-at-point/LSP resolution re-probes the real toolchain (default 86400, i.e. 24h). 0 or negative "
        "disables caching outright -- every lookup re-probes. See also refresh-toolchain-include-paths for a "
        "manual, immediate cache clear.");
    env.Register<&NedSetProseCheckerCommand>(
        "ned", "set-prose-checker-command",
        "Set the command used to launch the prose/spell/grammar checker: (argv), e.g. "
        "(ned/set-prose-checker-command [\"ltex-ls\"]) to use something other than the default. Same argv shape as "
        "ned/set-lsp-command. With no override configured, ned auto-wires harper-ls if it's found on $PATH -- an "
        "empty argv clears an explicit override and reverts to that auto-detection rather than disabling the "
        "checker; use ned/set-prose-checker-enabled false to actually turn it off.");
    env.Register<&NedSetProseCheckerEnabled>(
        "ned", "set-prose-checker-enabled",
        "Enable or disable prose/spell/grammar checking as a whole (default true). Diagnostics from it merge "
        "alongside the buffer's primary language server's own diagnostics rather than replacing them.");

    env.Register<&NedSetClipboardCopyCommand>(
        "ned", "set-clipboard-copy-command",
        "Set the command kill-line/kill-region/kill-ring-save/kill-word/yank pipe killed text into to reach the "
        "system clipboard: (argv), e.g. (ned/set-clipboard-copy-command [\"wl-copy\"]). With no override "
        "configured, ned auto-detects wl-copy (Wayland), xclip/xsel (X11), pbcopy (macOS), or clip.exe (WSL) on "
        "$PATH, in that order -- an empty argv clears an explicit override and reverts to that auto-detection. "
        "ned also always writes an OSC 52 escape sequence directly to the terminal regardless of this setting, "
        "since it's the only thing that reaches a *local* clipboard over an SSH session with no tool installed on "
        "the remote host; use ned/set-clipboard-enabled false to turn off both mechanisms at once.");
    env.Register<&NedSetClipboardPasteCommand>(
        "ned", "set-clipboard-paste-command",
        "Set the command yank reads the system clipboard from when it differs from the kill ring's own most "
        "recent entry: (argv), e.g. (ned/set-clipboard-paste-command [\"wl-paste\" \"-n\"]). Same auto-detection/"
        "empty-clears convention as ned/set-clipboard-copy-command, resolved independently of it. There is no OSC "
        "52 read-back fallback for paste -- most terminals don't answer that query at all, so paste always goes "
        "through the configured command instead.");
    env.Register<&NedSetClipboardEnabled>(
        "ned", "set-clipboard-enabled",
        "Enable or disable system-clipboard integration as a whole (default true) -- both the shelled-out CLI "
        "tool and the OSC 52 write/read paths.");

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

    env.Register<&NedSetCaptureForeground>(
        "ned", "set-capture-foreground",
        "Override one tree-sitter capture name's foreground color as \"#rrggbb\" (e.g. \"function.builtin\", no "
        "leading @) -- empty string clears. More specific dotted names inherit from less specific ones "
        "(\"function.builtin.static\" falls back through \"function.builtin\" to \"function\"), then from the "
        "capture's syntax class (ned/set-syntax-*). See ned/capture-names for every known name.");
    env.Register<&NedSetCaptureBackground>(
        "ned", "set-capture-background",
        "Override one capture name's background color as \"#rrggbb\" -- empty string clears; inherits like "
        "ned/set-capture-foreground.");
    env.Register<&NedSetCaptureBold>(
        "ned", "set-capture-bold", "Override one capture name's bold trait (true/false) -- nil clears.");
    env.Register<&NedSetCaptureItalic>(
        "ned", "set-capture-italic", "Override one capture name's italic trait (true/false) -- nil clears.");
    env.Register<&NedSetCaptureUnderlined>(
        "ned", "set-capture-underlined", "Override one capture name's underlined trait (true/false) -- nil clears.");
    env.Register<&NedSetCaptureStrikethrough>(
        "ned", "set-capture-strikethrough",
        "Override one capture name's strikethrough trait (true/false) -- nil clears.");
    env.Register<&NedCaptureForeground>(
        "ned", "capture-foreground", "The capture name's own overridden foreground color, or nil if unset (no inheritance walk).");
    env.Register<&NedCaptureBackground>(
        "ned", "capture-background", "The capture name's own overridden background color, or nil if unset.");
    env.Register<&NedCaptureBold>("ned", "capture-bold", "The capture name's own overridden bold trait, or nil if unset.");
    env.Register<&NedCaptureItalic>("ned", "capture-italic", "The capture name's own overridden italic trait, or nil if unset.");
    env.Register<&NedCaptureUnderlined>(
        "ned", "capture-underlined", "The capture name's own overridden underlined trait, or nil if unset.");
    env.Register<&NedCaptureStrikethrough>(
        "ned", "capture-strikethrough", "The capture name's own overridden strikethrough trait, or nil if unset.");
    env.Register<&NedSetCaptureClass>(
        "ned", "set-capture-class",
        "Remap a capture name to a syntax class (e.g. (ned/set-capture-class \"tag.error\" \"control-keyword\")) -- "
        "the capture then inherits that class's whole built-in style. Applies at every dotted level, so remapping "
        "\"keyword\" also re-bases unlisted specific names that fall back to it. Empty class name restores the "
        "built-in mapping.");
    env.Register<&NedCaptureClass>(
        "ned", "capture-class", "The capture name's remapped syntax class name, or nil if using the built-in mapping.");
    env.Register<&NedCaptureNames>(
        "ned", "capture-names",
        "Every known tree-sitter capture name, sorted: the built-in defaults table merged with every name seen from "
        "a loaded grammar's query or configured via ned/set-capture-*.");

    env.Register<&NedVcsRegisterProvider>(
        "ned", "vcs-register-provider",
        "Register a VCS-agnostic plugin: (name callbacks), where callbacks is a struct/table keyed by keyword. "
        ":detect (required) takes a root path and returns true if it's a repository this plugin handles. The "
        "*-argv callbacks each return an argv array/tuple of strings for the external command to run; the parse-* "
        "callbacks each take that command's captured stdout and return an array of tables. Optional keys, by "
        "operation: :blame-argv/:parse-blame and :log-argv/:parse-log (entries have :hash :author :date :summary), "
        ":diff-argv/:parse-diff (:old-start :old-count :new-start :new-count per hunk), :status-argv/:parse-status "
        "(:state :path per changed file, path relative to the root), :stage-argv/:unstage-argv (take the file's "
        "path; success is exit code 0, no parse half), :staged-diff-argv (the index-vs-comparison-point diff, for "
        "selecting a hunk to unstage), :stage-patch-argv/:unstage-patch-argv (take root and a patch file's path, "
        "applying it to the staging area forward/reverse), :commit-argv (takes root and the commit message), "
        ":branch-list-argv/:parse-branch-list (:name :current per branch), and :branch-switch-argv/"
        ":branch-create-argv (take root and the branch name). An operation whose callbacks are absent reports "
        "'not supported by this provider' when invoked. The actual subprocess is run by ned itself, never by the "
        "plugin -- these callbacks only build argv and parse already-captured output. Re-registering name replaces "
        "the previous provider.");
}

} // namespace ned::janet
