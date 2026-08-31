#include "EditorBindings.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Acp/AcpConfig.h"
#include "Editor/Acp/AcpPanelConfig.h"
#include "Editor/AutoMerge.h"
#include "Editor/AutoPair.h"
#include "Editor/AutoRevert.h"
#include "Editor/Backup.h"
#include "Editor/Clipboard.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/Dap/DapConfig.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/DiffRefreshSettings.h"
#include "Editor/FileWatch.h"
#include "Editor/FinalNewline.h"
#include "Editor/FormatOnSave.h"
#include "Editor/HighlightSettings.h"
#include "Editor/HugeStructuralWindow.h"
#include "Editor/InlineDiagnostics.h"
#include "Editor/LineEndingPolicy.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspBackgroundSync.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Lsp/ProseChecker.h"
#include "Editor/MinimapSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/OrgCapture.h"
#include "Editor/PageScroll.h"
#include "Editor/PersistentUndo.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSession.h"
#include "Editor/ProjectTrust.h"
#include "Editor/RelativeLineNumberSettings.h"
#include "Editor/ScratchPad.h"
#include "Editor/ScriptingSession.h"
#include "Editor/SearchSettings.h"
#include "Editor/Session.h"
#include "Editor/SnippetRegistry.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/TabWidth.h"
#include "Editor/Tasks/TaskConfig.h"
#include "Editor/Terminal/Config.h"
#include "Editor/TestRun/TestOutputParser.h"
#include "Editor/TestRun/TestRunConfig.h"
#include "Editor/ThemeSetting.h"
#include "Editor/ToolchainIncludePaths.h"
#include "Editor/TrimOnSave.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vim/VimSettings.h"
#include "Editor/WhichKeySettings.h"
#include "Editor/WhitespaceSettings.h"
#include "Editor/WrapOverrides.h"
#include "JanetVcsProvider.h"
#include "Text/BufferList.h"
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

    // Vim-mode follow-up: same process-wide-bool-toggle shape as
    // NedSetLspAutoComplete -- default false, see Editor/Vim/VimSettings.h.
    void NedSetVimMode(bool enabled) {
        editor::vim::SetVimModeEnabled(enabled);
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

    void NedSetProtocolStallTimeoutMs(std::int64_t milliseconds) {
        editor::SetProtocolStallTimeoutMs(static_cast<int>(milliseconds));
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

    void NedSetMinimapEnabled(bool enabled) {
        editor::SetMinimapEnabled(enabled);
    }

    void NedSetMinimapWidth(std::int64_t columns) {
        editor::SetMinimapWidth(static_cast<int>(columns));
    }

    void NedSetMinimapCharsPerDot(double columns) {
        editor::SetMinimapCharsPerDot(columns);
    }

    void NedSetTrailingWhitespaceHighlightEnabled(bool enabled) {
        editor::SetTrailingWhitespaceHighlightEnabled(enabled);
    }

    void NedSetIndentGuidesEnabled(bool enabled) {
        editor::SetIndentGuidesEnabled(enabled);
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

    void NedSetRelativeLineNumbers(bool enabled) {
        editor::SetRelativeLineNumbersEnabled(enabled);
    }

    void NedSetWhichKeyEnabled(bool enabled) {
        editor::SetWhichKeyEnabled(enabled);
    }

    void NedSetInlineDiagnostics(bool enabled) {
        editor::SetInlineDiagnosticsEnabled(enabled);
    }

    void NedRegisterLanguageGrammar(std::string name, std::string libraryPath, std::string queriesDir) {
        editor::RegisterDynamicMode(name, libraryPath, queriesDir);
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

    // DAP client slice 1: same argv shape as NedSetLspCommand for the
    // adapter subprocess; the launch config stays an opaque JSON string on
    // purpose -- it's the DAP `launch` request's own adapter-specific
    // arguments object, whose keys differ per adapter (see
    // Editor/Dap/DapConfig.h).
    void NedSetDapAdapter(std::string language, std::vector<std::string> argv) {
        editor::dap::SetDapAdapterCommand(language, std::move(argv));
    }

    void NedSetDapLaunch(std::string language, std::string launchConfigJson) {
        editor::dap::SetDapLaunchConfig(language, std::move(launchConfigJson));
    }

    // task-runner follow-up: argv[0] is the task's executable, remaining
    // elements its arguments, e.g. (ned/set-task-command "build" ["cmake"
    // "--build" "."]). An empty argv clears any existing registration for
    // name, mirroring NedSetLspCommand's own empty-clears convention.
    void NedSetTaskCommand(std::string name, std::vector<std::string> argv) {
        editor::tasks::SetTaskCommand(name, std::move(argv));
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

    // Converts a Janet test parser's return value into a TestRunOutcome --
    // either a bare array of result tables, or a table {:results [...]
    // :failures-only true :passed n} for the metadata a failures-only
    // format needs. Field reading follows JanetVcsProvider.cpp's
    // "degrade, don't crash" convention: a malformed entry is skipped, a
    // missing field defaults, never a panic.
    editor::testrun::TestRunOutcome JanetTestOutcome(Janet value, const std::string& formatName) {
        using editor::testrun::TestResult;
        editor::testrun::TestRunOutcome outcome;
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

    // ACP chat panel: which edge the dock hugs and how much of the screen it
    // covers, mirroring NedSetTerminalHeightPercent's own shape.
    void NedSetAcpPanelDock(std::string side) {
        editor::acp::SetAcpPanelDock(side);
    }

    void NedSetAcpPanelSizePercent(std::int64_t percent) {
        editor::acp::SetAcpPanelSizePercent(static_cast<int>(percent));
    }

    // hover/completion follow-up: the only way LspServerConfig.h's own
    // process-wide auto-complete toggle/debounce ever get configured away
    // from their defaults (enabled, 350ms), same "just forward to the
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

    // lsp-format-on-save follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspAutoComplete above.
    void NedSetLspFormatOnSave(bool enabled) {
        editor::lsp::SetLspFormatOnSaveEnabled(enabled);
    }

    // diagnostics-debounce follow-up: same "just forward to the process-wide
    // setter" shape as NedSetLspCompletionDebounce, for how long a buffer's
    // inline diagnostics wait after the server's most recent publish before
    // actually updating (see LspManager::HandlePublishDiagnostics).
    void NedSetLspDiagnosticsDebounce(std::int64_t milliseconds) {
        editor::lsp::SetLspDiagnosticsDebounceMs(static_cast<int>(milliseconds));
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
    env.Register<&NedSetProtocolStallTimeoutMs>(
        "ned", "set-protocol-stall-timeout-ms",
        "Set how long, in milliseconds, silence after an LSP/DAP/ACP frame or message has started arriving is "
        "tolerated before the connection is treated as stalled and disconnected -- idle time between messages "
        "stays unbounded regardless (default 30000; non-positive values are clamped to 1).");
    env.Register<&NedSetProtocolRequestTimeoutMs>(
        "ned", "set-protocol-request-timeout-ms",
        "Set how long, in milliseconds, a sent LSP/DAP/ACP request is kept pending before it's resolved with a "
        "synthetic timeout failure (default 30000; non-positive values are clamped to 1).");
    env.Register<&NedSetTheme>(
        "ned", "set-theme",
        "Select the startup theme by name (e.g. \"dark\", \"light\", \"ansi-dark\"). Overrides a saved --detect-theme "
        "file; an unknown name is reported at startup and falls back. Empty string clears the preference.");
    env.Register<&NedThemeSet>(
        "ned", "theme-set",
        "Override one theme color or Brush trait by key (e.g. (ned/theme-set \"keyword_foreground\" \"#f042d6\") or "
        "(ned/theme-set \"active_tab_bold\" \"false\")) on top of the startup theme -- keys match the theme file's "
        "own, trait values are \"true\"/\"false\"; the save-theme command writes a full theme.janet of these calls "
        "for hand-editing, loaded via (dofile ...) from init.janet.");
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
    env.Register<&NedSetTrailingWhitespaceHighlightEnabled>(
        "ned", "set-trailing-whitespace-highlight-enabled",
        "Enable/disable a subtle background highlight on trailing whitespace (spaces/tabs after the last "
        "non-whitespace character on a line). Default false, matching Emacs' own opt-in "
        "show-trailing-whitespace precedent rather than VSCode/Sublime's forced-on default.");
    env.Register<&NedSetIndentGuidesEnabled>(
        "ned", "set-indent-guides-enabled",
        "Enable/disable vertical indentation guide glyphs at each indent-width column within a line's own "
        "leading whitespace. Default false, same opt-in reasoning as set-trailing-whitespace-highlight-enabled.");
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
    env.Register<&NedSetTrimTrailingWhitespaceOnSave>(
        "ned", "set-trim-trailing-whitespace-on-save",
        "Enable/disable stripping trailing spaces/tabs from every line and collapsing trailing blank lines at "
        "end-of-file, applied to a file's written content on save (default true). Disk-only, same as "
        "set-ensure-final-newline -- the buffer's own live content is never touched.");
    env.Register<&NedSetLineEndingPolicy>(
        "ned", "set-line-ending-policy",
        "\"preserve\" (default) keeps each buffer's own detected/converted line ending on save; \"lf\"/\"crlf\"/"
        "\"cr\" force every save to that ending regardless of what was detected. Per-buffer "
        "convert-line-endings-to-lf/-crlf/-cr override a single buffer's own ending independent of this "
        "process-wide policy.");
    env.Register<&NedSetCodeFoldingEnabled>(
        "ned", "set-code-folding-enabled",
        "Enable/disable the gutter code-folding affordance for modes with a fold query (default true).");
    env.Register<&NedSetRelativeLineNumbers>(
        "ned", "set-relative-line-numbers",
        "Enable/disable relative line numbers in the gutter (default false): the current line keeps its real "
        "number, every other visible line shows its distance from it, Vim's 'relativenumber' convention.");
    env.Register<&NedSetWhichKeyEnabled>(
        "ned", "set-which-key-enabled",
        "Enable/disable the which-key popup listing possible next chords while a prefix key (C-x, C-c, ...) is "
        "pending (default true). The echo area's own \"C-x-\" pending-sequence text is unaffected either way.");
    env.Register<&NedSetInlineDiagnostics>(
        "ned", "set-inline-diagnostics",
        "Enable/disable inline diagnostic annotation rows (carets + message under a line with an LSP diagnostic; "
        "default true).");
    env.Register<&NedRegisterLanguageGrammar>(
        "ned", "register-language-grammar",
        "Load a tree-sitter grammar at runtime: (name library-path queries-dir). library-path is a shared library "
        "exporting tree_sitter_<name>; queries-dir is a directory scanned for conventional query filenames -- "
        "\"highlights.scm\", \"folds.scm\" (a \"@fold\"-capture query), and \"imports.scm\" (an "
        "\"@import.target\"/\"@import.module\"/\"@import.statement\"-capture query backing open-link-at-point's "
        "go-to-file-at-point for this grammar's own import/include syntax) -- whichever of the three aren't present "
        "in the directory are simply skipped (a grammar with no highlights.scm, no folds.scm, or no imports.scm is "
        "fine), so a file added to queries-dir later (e.g. by a system package update) takes effect on the next "
        "registration with no init.janet change needed. Pass \"\" for queries-dir to register the grammar for its "
        "parser alone. Re-registering the same name replaces it. The registered name can then be used as the "
        "mode-name argument to ned/set-mode-for-extension or ned/set-mode-for-filename.");
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
    env.Register<&NedSetTaskCommand>(
        "ned", "set-task-command",
        "Set the command run by run-task for a task name: (name argv), e.g. (ned/set-task-command \"build\" "
        "[\"cmake\" \"--build\" \".\"]). argv is an array or tuple of strings -- argv[0] the executable (resolved "
        "against $PATH), the rest its arguments. An empty argv clears the configured command for name.");
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
        "${1:i} = 0; $1 < ${2:n}; ++$1) {\\n    $0\\n}\"). Typing the trigger word then TAB (or M-x "
        "expand-snippet in modes whose keymap claims TAB) expands the body: ${n:placeholder}/$n are tabstop fields "
        "TAB/S-TAB hop between (a repeated index mirrors typing live), $0 is where point lands at the end, \\$ "
        "escapes a literal dollar. language-key matches ned/set-lsp-command's (\"cpp\", \"python\", ...); \"\" "
        "registers for every mode. An empty body clears the trigger; re-registering overwrites it.");
    env.Register<&NedSnippetTriggers>(
        "ned", "snippet-triggers",
        "Return the snippet trigger words visible to a language key -- its own registrations merged with the "
        "\"\"-global tier, sorted. (ned/snippet-triggers \"cpp\")");
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
        "Enable or disable automatic LSP completion ghost text while typing (default true). Manual completion "
        "(lsp-complete, bound to C-M-i) works regardless of this setting.");
    env.Register<&NedSetLspCompletionDebounce>(
        "ned", "set-lsp-completion-debounce",
        "Set the delay, in milliseconds, after the last relevant keystroke before an automatic completion request "
        "is sent (default 350). Non-positive values are clamped to 1.");
    env.Register<&NedSetLspSignatureHelpAutoTrigger>(
        "ned", "set-lsp-signature-help-auto-trigger",
        "Enable or disable automatically requesting signature help after typing ( or , inside a call (default "
        "true). Manual invocation (lsp-signature-help) works regardless of this setting.");
    env.Register<&NedSetLspFormatOnSave>(
        "ned", "set-lsp-format-on-save",
        "Enable or disable formatting the buffer via the language server on save (default false). Ignored "
        "whenever ned/set-format-command has an external formatter configured -- that always takes precedence.");
    env.Register<&NedSetLspDiagnosticsDebounce>(
        "ned", "set-lsp-diagnostics-debounce",
        "Set the delay, in milliseconds, after the LSP server's most recently received diagnostics publish for a "
        "buffer before it's actually applied (default 500) -- keeps inline diagnostics from repainting on nearly "
        "every keystroke while typing, settling in only once the server goes quiet for this long. Non-positive "
        "values are clamped to 1.");
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
        "52 read-back fallback for paste -- see Editor/Clipboard.h's own comment for why.");
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
