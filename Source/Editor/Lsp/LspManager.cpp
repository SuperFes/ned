#include "LspManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>

#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSettings.h"
#include "Editor/TabWidth.h"
#include "LspBrokerConnect.h"
#include "LspPosition.h"
#include "LspRootResolver.h"
#include "LspServerConfig.h"
#include "ProseChecker.h"
#include "Text/BinaryDetect.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::lsp {

namespace {

    // crash-loop-respawn-guard follow-up: see LspManager.h's disconnectBurst_
    // doc comment for why this exists at all. Thresholds, not Janet-
    // configurable -- proportionate to closing a real resource-exhaustion
    // bug, not a tunable feature.
    constexpr std::chrono::milliseconds kCrashLoopWindow{3000};
    constexpr int                       kCrashLoopThreshold = 3;

    // respawn-debounce follow-up: see ClientForLanguage's own doc comment on
    // lastDisconnectAt_ for why this exists alongside (not instead of) the
    // crash-loop guard above -- breathing room for each individual retry,
    // not just a cap on the total. 3 * kRespawnCooldown comfortably fits
    // inside kCrashLoopWindow, so a real crash-looping server still reaches
    // the giveup threshold, just no longer within the same video frame.
    constexpr std::chrono::milliseconds kRespawnCooldown{1000};

    // v1: no percent-encoding of special characters in the path -- every
    // path this touches (an open Buffer's own Path(), editor::ProjectRoot())
    // is already a real filesystem path this process itself resolved, not
    // untrusted input, so the common case (no space/unicode-heavy path)
    // round-trips correctly; a path containing characters that need real
    // percent-encoding is a known, documented gap, not silently assumed away.
    std::string PathToUri(const std::filesystem::path& path) {
        // Absolutized here rather than assumed: a buffer opened via a
        // relative CLI argument (`ned demo.cpp`) keeps that relative Path(),
        // and "file://demo.cpp" is unresolvable to a server -- clangd
        // rejected every request for such a buffer ("failed to decode ...
        // unresolvable URI"), found live while verifying the
        // codeActionLiteralSupport fix, not in review. The error_code
        // overload, not the throwing one: this runs under Paint() with no
        // catch anywhere above it, and absolute() throws for an empty path
        // (and when the cwd is gone) -- a degraded URI beats aborting the
        // whole editor (a real SIGABRT from a core dump, not hypothetical).
        std::error_code             ec;
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        return "file://" + (ec ? path : absolute).lexically_normal().string();
    }

    std::optional<std::filesystem::path> UriToPath(const std::string& uri) {
        constexpr std::string_view kPrefix = "file://";
        if (uri.rfind(kPrefix, 0) != 0) {
            return std::nullopt;
        }
        return std::filesystem::path(uri.substr(kPrefix.size()));
    }

    // project-settings-lsp-init-options follow-up. Resolves a dotted "section"
    // path (e.g. "phpactor", "intelephense.environment" -- exactly the shape a
    // real workspace/configuration request item's own "section" field takes) into
    // tree, walking one object level per '.'-separated segment. Returns JSON null
    // ("no client-side override, use your own defaults") the instant a segment is
    // missing or the tree isn't an object at that point -- the same fallback
    // WireNotificationHandlers' workspace/configuration handler already used
    // before any lspWorkspaceConfiguration existed to resolve against.
    Json ResolveConfigurationSection(const Json& tree, const std::string& section) {
        const Json* current = &tree;
        std::size_t start   = 0;
        while (true) {
            const std::size_t dot     = section.find('.', start);
            const std::string segment = section.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!current->is_object() || !current->contains(segment)) {
                return Json(nullptr);
            }
            current = &current->at(segment);
            if (dot == std::string::npos) {
                return *current;
            }
            start = dot + 1;
        }
    }

    text::Buffer::Diagnostic::Severity SeverityFromLsp(int severity) {
        switch (severity) {
            case 1:
                return text::Buffer::Diagnostic::Severity::Error;
            case 2:
                return text::Buffer::Diagnostic::Severity::Warning;
            case 3:
                return text::Buffer::Diagnostic::Severity::Information;
            case 4:
                return text::Buffer::Diagnostic::Severity::Hint;
            default:
                return text::Buffer::Diagnostic::Severity::Information; // an unrecognized/missing severity -- a safe, visible-but-not-alarming default
        }
    }

    // code-actions follow-up: the reverse of SeverityFromLsp, for building a
    // textDocument/codeAction request's own "context.diagnostics" -- the
    // server expects real LSP Diagnostic shapes back, not this codebase's
    // internal Buffer::Diagnostic::Severity enum.
    int SeverityToLsp(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return 1;
            case text::Buffer::Diagnostic::Severity::Warning:
                return 2;
            case text::Buffer::Diagnostic::Severity::Information:
                return 3;
            case text::Buffer::Diagnostic::Severity::Hint:
                return 4;
        }
        return 3; // unreachable for a real enum value -- Information is the same safe default SeverityFromLsp uses
    }

    // semanticTokens follow-up. Maps one of the LSP spec's standard
    // SemanticTokenTypes strings onto an existing editor::SyntaxClass --
    // reusing the same curated set tree-sitter highlighting already
    // populates rather than adding a new axis to HighlightSpan (see
    // ROADMAP.md/the plan this follows for why). nullopt for a token type
    // with no sensible existing class (dropped by the caller, not
    // force-fit) -- "event" and "unknown" (a real type clangd itself
    // emits) are the two standard-or-observed-in-practice types left
    // unmapped. Deliberately ignores tokenModifiers -- a v1 scope cut, not
    // a bug: a "readonly"/"static"/etc. refinement is a real future
    // improvement, not required for the base feature to be useful.
    std::optional<editor::SyntaxClass> SyntaxClassForSemanticTokenType(const std::string& tokenType) {
        static const std::unordered_map<std::string, editor::SyntaxClass> kMapping = {
            {"namespace", editor::SyntaxClass::Namespace},
            {"class", editor::SyntaxClass::Type},
            {"enum", editor::SyntaxClass::Type},
            {"interface", editor::SyntaxClass::Type},
            {"struct", editor::SyntaxClass::Type},
            {"type", editor::SyntaxClass::Type},
            {"typeParameter", editor::SyntaxClass::Type},
            {"parameter", editor::SyntaxClass::Parameter},
            {"variable", editor::SyntaxClass::Variable},
            {"property", editor::SyntaxClass::Property},
            {"enumMember", editor::SyntaxClass::Constant},
            {"function", editor::SyntaxClass::Function},
            {"method", editor::SyntaxClass::Method},
            {"macro", editor::SyntaxClass::FunctionBuiltin},
            {"keyword", editor::SyntaxClass::Keyword},
            {"modifier", editor::SyntaxClass::KeywordModifier},
            {"comment", editor::SyntaxClass::Comment},
            {"string", editor::SyntaxClass::String},
            {"number", editor::SyntaxClass::Number},
            {"regexp", editor::SyntaxClass::String},
            {"operator", editor::SyntaxClass::Operator},
            {"decorator", editor::SyntaxClass::Attribute},
            {"label", editor::SyntaxClass::Label},
        };
        const auto it = kMapping.find(tokenType);
        return it != kMapping.end() ? std::optional(it->second) : std::nullopt;
    }

    // error-visibility follow-up. No existing timestamp-formatting
    // convention exists anywhere else in this codebase (confirmed via
    // search) -- this is a small, self-contained, file-local helper, not
    // something sharing a home with anything else.
    std::string FormatLogLine(std::string_view language, std::string_view message) {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm           localNow{};
        localtime_r(&now, &localNow);
        char timestamp[16];
        std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &localNow);
        return "[" + std::string(timestamp) + "] " + std::string(language) + ": " + std::string(message) + "\n";
    }

    // error-visibility follow-up. Extracts a human-readable string from a
    // JSON-RPC error object -- "message" per the spec if present, else the
    // raw JSON so nothing is ever silently dropped even for a
    // spec-noncompliant server.
    std::string ExtractErrorMessage(const Json& error) {
        return error.value("message", error.dump());
    }

    // background-activity-spinner follow-up: same std::string materialization
    // of the shared constant LspClient.cpp's own copy makes.
    const std::string kLspActivity{kLspActivityName};

    Json DiagnosticToLsp(const text::Buffer::Diagnostic& diagnostic, const text::ITextStorage& content) {
        const LspPosition start = BytePositionToLsp(content, diagnostic.startByte);
        const LspPosition end   = BytePositionToLsp(content, diagnostic.endByte);
        return Json{
            {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
            {"severity", SeverityToLsp(diagnostic.severity)},
            {"message", diagnostic.message},
        };
    }

} // namespace

Json BuildInitializeParams(const std::filesystem::path& projectRoot, const Json& initializationOptions) {
    // codeActionLiteralSupport is load-bearing, not boilerplate: per the LSP
    // spec a server may only return edit-carrying CodeAction literals to a
    // client that advertises it, and must fall back to bare Command objects
    // otherwise (executeCommand follow-up: now runnable via
    // LspManager::ExecuteCommand/workspace/executeCommand, but a client
    // that doesn't advertise this still gets the plain-Command fallback
    // form regardless). clangd honors codeActionLiteralSupport exactly --
    // without it, its "fix available" quickfixes (e.g. "remove #include
    // directive") arrived as Commands with no "edit", and applying one
    // reported "has no edit to apply". Confirmed against a real clangd 22
    // session both ways, not inferred from the spec alone.
    //
    // dataSupport/resolveSupport (code-actions-resolve follow-up) advertise
    // that this client will call codeAction/resolve for a CodeAction sent
    // back without an "edit" -- see ResolveCodeAction.
    //
    // window.workDoneProgress (workDoneProgress-support follow-up) invites
    // "$/progress" reporting -- server-side busy state (clangd's background
    // indexing) for the mode-line spinner; see HandleProgress.
    // An empty root becomes rootUri: null (explicitly allowed by the LSP
    // spec -- "rootUri: DocumentUri | null") rather than a nonsense
    // "file://" URI: ProjectRoot() should never be empty anymore
    // (DetectProjectRoot absolutizes now), but this handshake runs under
    // Paint() with no catch above it, so it must stay total regardless.
    Json params = Json{
        {"processId", static_cast<std::int64_t>(::getpid())},
        {"rootUri", projectRoot.empty() ? Json(nullptr) : Json(PathToUri(projectRoot))},
        {"capabilities",
         {{"textDocument",
           // completionItem.snippetSupport (snippet-expansion follow-up) is
           // load-bearing the same way codeActionLiteralSupport below is:
           // per the spec a server may only send insertTextFormat: 2
           // (snippet-syntax) items to a client advertising this, so
           // without it a conforming server (clangd's function-argument
           // completions, rust-analyzer, tsserver) never sends the snippet
           // form at all -- the accept path expands them via
           // Editor/Snippet.h into a real tabstop session.
           //
           // capabilities-hygiene follow-up: hover/definition/declaration/
           // typeDefinition/implementation/references/rename/signatureHelp/
           // publishDiagnostics are all requests or notifications this
           // client already sends/handles (see LspManager's own Request*
           // methods and HandlePublishDiagnostics) but never previously
           // declared -- bare {} advertises plain support with no optional
           // refinement (no prepareSupport on rename since PrepareRename is
           // never sent, no tagSupport/relatedInformation on
           // publishDiagnostics since neither is parsed). Harmless against a
           // permissive server (confirmed live against clangd either way),
           // but a capability-strict one is entitled to assume a client that
           // never declares e.g. "rename" doesn't want rename requests at
           // all.
           {{"completion", {{"completionItem", {{"snippetSupport", true}}}}},
            {"hover", Json::object()},
            {"signatureHelp", Json::object()},
            {"declaration", Json::object()},
            {"definition", Json::object()},
            {"typeDefinition", Json::object()},
            {"implementation", Json::object()},
            {"references", Json::object()},
            {"documentHighlight", Json::object()},
            {"rename", Json::object()},
            {"formatting", Json::object()},
            {"rangeFormatting", Json::object()},
            {"onTypeFormatting", Json::object()},
            // semanticTokens follow-up: tokenTypes/tokenModifiers here are
            // spec-required but purely informational (the client's own
            // decode is index-based against the server's own legend, not
            // filtered against this list) -- declares every standard type
            // SyntaxClassForSemanticTokenType actually maps, so a
            // capability-strict server has no reason to omit any of them
            // from its own legend. requests.full only (no range/delta yet
            // -- see ROADMAP.md); formats always ["relative"], the only
            // value the spec defines.
            {"semanticTokens",
             {{"requests", {{"full", true}}},
              {"tokenTypes", Json::array({"namespace", "class", "enum", "interface", "struct", "type", "typeParameter",
                                          "parameter", "variable", "property", "enumMember", "function", "method", "macro",
                                          "keyword", "modifier", "comment", "string", "number", "regexp", "operator",
                                          "decorator", "label"})},
              {"tokenModifiers", Json::array()},
              {"formats", Json::array({"relative"})}}},
            {"inlayHint", Json::object()},
            {"codeLens", Json::object()},
            {"publishDiagnostics", Json::object()},
            {"codeAction",
             {{"codeActionLiteralSupport",
               {{"codeActionKind",
                 {{"valueSet", Json::array({"", "quickfix", "refactor", "refactor.extract", "refactor.inline", "refactor.rewrite",
                                            "source", "source.organizeImports", "source.fixAll"})}}}}},
              {"dataSupport", true},
              {"resolveSupport", {{"properties", Json::array({"edit"})}}}}},
            // call/type-hierarchy follow-up: both are bare {} like every
            // other capabilities-hygiene entry above -- no optional
            // refinement (no "dynamicRegistration") this client needs.
            {"callHierarchy", Json::object()},
            {"typeHierarchy", Json::object()}}},
          // capabilities-hygiene follow-up: workspace/configuration and
          // workspace/executeCommand are both handled/sent (see
          // WireNotificationHandlers/ExecuteCommand) but this object
          // previously had no "workspace" key at all -- "configuration" is a
          // bare boolean per spec, unlike every textDocument.* entry above.
          {"workspace", {{"configuration", true}, {"didChangeConfiguration", Json::object()}, {"executeCommand", Json::object()}}},
          {"window", {{"workDoneProgress", true}}}}},
    };
    if (!initializationOptions.empty()) {
        params["initializationOptions"] = initializationOptions;
    }
    return params;
}

LspManager::LspManager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

LspClient* LspManager::ExistingClientForLanguage(const std::string& language) const {
    const auto it = clients_.find(language);
    return it != clients_.end() ? it->second.get() : nullptr;
}

void LspManager::WireNotificationHandlers(LspClient& client, const std::string& serverKey, const std::string& connectionKey,
                                          const Json& workspaceConfiguration) {
    client.SetNotificationHandler("textDocument/publishDiagnostics",
                                  [this, serverKey](const Json& params) { HandlePublishDiagnostics(params, serverKey); });
    // workDoneProgress-support follow-up: the create request just
    // establishes a token the following "$/progress" notifications carry --
    // there's nothing to decide, its result is null by spec; HandleProgress
    // tracks the token itself from the begin/end notifications rather than
    // from here, so an unsolicited-progress server (the spec explicitly
    // allows initiating progress without create) works identically.
    client.SetRequestHandler("window/workDoneProgress/create", [](const Json&) { return Json(nullptr); });
    // prose-checking follow-up, found live against a real harper-ls: a
    // config-pull server sends this right after "initialized" and, per spec,
    // expects one result array entry per requested "items" scope -- unlike
    // window/workDoneProgress/create (whose result is genuinely unused),
    // harper-ls does not proceed to check any document at all until this
    // gets a real (non-error) response. Before this handler existed, every
    // such request fell through to DispatchFrame's generic "method not
    // found" error response, and harper-ls silently never published a
    // single diagnostic for the rest of the connection's lifetime -- no
    // crash, no log entry, just permanent silence.
    //
    // project-settings-lsp-init-options follow-up: each requested item's
    // "section" is now resolved against workspaceConfiguration
    // (Editor/ProjectSettings.h's lspWorkspaceConfiguration) -- still null
    // ("no client-side override, use your own defaults", the standard
    // response VS Code's own client sends too) for any section nothing was
    // configured for, or when a request item carries no section at all.
    client.SetRequestHandler("workspace/configuration", [workspaceConfiguration](const Json& params) {
        if (!params.contains("items") || !params["items"].is_array()) {
            return Json(std::vector<Json>(1, Json(nullptr)));
        }
        std::vector<Json> results;
        results.reserve(params["items"].size());
        for (const Json& item : params["items"]) {
            const bool hasSection = item.contains("section") && item["section"].is_string();
            results.push_back(hasSection ? ResolveConfigurationSection(workspaceConfiguration, item["section"].get<std::string>())
                                         : Json(nullptr));
        }
        return Json(results);
    });
    client.SetNotificationHandler("$/progress", [this, serverKey](const Json& params) { HandleProgress(serverKey, params); });
    client.SetOnDisconnected([this, serverKey, connectionKey](std::string reason) {
        LogError(serverKey, "server disconnected: " + reason);
        disconnectDetail_[serverKey] = reason;
        ClientDisconnected(serverKey, connectionKey);
    });
}

std::string LspManager::ConnectionKey(const std::filesystem::path& root, const std::string& serverKey) const {
    if (root == editor::ProjectRoot()) {
        return serverKey;
    }
    return root.string() + '\x1f' + serverKey;
}

std::filesystem::path LspManager::ResolveCachedRoot(const std::filesystem::path& bufferPath, const std::string& language) {
    const std::string cacheKey = bufferPath.parent_path().string() + '\x1f' + language;
    if (const auto it = resolvedRootCache_.find(cacheKey); it != resolvedRootCache_.end()) {
        return it->second;
    }
    return resolvedRootCache_.emplace(cacheKey, ResolveLspRoot(bufferPath, language)).first->second;
}

LspClient* LspManager::ClientForLanguage(const std::string& serverKey, const std::filesystem::path& root) {
    const std::string connectionKey = ConnectionKey(root, serverKey);
    if (LspClient* existing = ExistingClientForLanguage(connectionKey)) {
        return existing;
    }

    // prose-checking follow-up: the one place kProseLanguageKey is treated
    // differently from a real language -- its command comes from
    // ProseCheckerCommand()'s auto-detect/override/enabled-toggle
    // resolution instead of the plain per-language table.
    const std::optional<std::vector<std::string>> command =
        (serverKey == kProseLanguageKey) ? ProseCheckerCommand() : LspServerCommand(serverKey);
    if (!command) {
        return nullptr;
    }

    // error-visibility follow-up. don't retry (or re-log) a command that
    // already failed to spawn on a previous frame -- SyncBuffer calls this
    // every Paint() for the active buffer, and a real subprocess-spawn
    // failure is not transient. Erasing on a *different* command lets a
    // user's own SetLspServerCommand reconfiguration get one fresh attempt.
    // LSP multi-root follow-up: keyed by serverKey, not connectionKey -- see
    // this class's own header comment on the deliberately-still-plain-keyed
    // status-latch group.
    if (const auto failed = failedCommands_.find(serverKey); failed != failedCommands_.end()) {
        if (failed->second == *command) {
            return nullptr;
        }
        failedCommands_.erase(failed);
        spawnFailureDetail_.erase(serverKey);
    }

    // respawn-debounce follow-up (requested alongside the crash-loop guard
    // above): even below kCrashLoopThreshold, a disconnect used to be
    // followed by a fresh spawn attempt on the very next SyncBuffer call --
    // the next Paint() frame, tens of milliseconds later, no breathing room
    // at all. A real server that stumbles once (a slow-starting language
    // server racing its own config file, a transient resource hiccup) gets
    // hammered immediately rather than given a moment to actually recover.
    // Silent no-op while cooling down -- not worth a log line every frame
    // for what's an ordinary, expected wait.
    if (const auto lastDisconnect = lastDisconnectAt_.find(serverKey); lastDisconnect != lastDisconnectAt_.end()) {
        if (std::chrono::steady_clock::now() - lastDisconnect->second < kRespawnCooldown) {
            return nullptr;
        }
    }

    // lsp-broker follow-up. Try attaching to an already-running LSP broker
    // daemon before spawning our own subprocess -- see LspBrokerConnect.h's
    // own header comment for exactly why this is always safe to attempt
    // (nullptr on any failure, never throws) and why nothing downstream of
    // this needs to know the difference: the returned LspClient still
    // genuinely performs the real initialize/initialized handshake below,
    // just over a socket to the broker instead of a pipe to a directly-
    // spawned process. LSP multi-root follow-up: root (the buffer's own
    // resolved root, not unconditionally editor::ProjectRoot() anymore) is
    // what actually exercises the broker's own pre-existing (root,
    // language) keying -- see LspBrokerConnect.h.
    std::unique_ptr<LspClient> client =
        TryConnectToBroker(root, serverKey, *command, eventLoop_, brokerSocketPathOverrideForTesting_);
    // graceful-lsp-shutdown follow-up: stamped here, the one place this
    // distinction is actually made -- see brokerBackedLanguages_'s own doc
    // comment in LspManager.h.
    const bool brokerBacked = (client != nullptr);
    if (!client) {
        try {
            client = std::make_unique<LspClient>(*command, eventLoop_);
        }
        catch (const std::exception& e) {
            // Previously uncaught -- Transport's constructor throws
            // std::runtime_error for a missing executable, a pipe() failure, or
            // a posix_spawn failure, and this call chain (SyncBuffer <-
            // BufferView::Paint()) had no catch anywhere above it, crashing the
            // whole running editor the instant a buffer of a misconfigured-LSP
            // language was displayed. Report instead of crashing.
            failedCommands_[serverKey]     = *command;
            spawnFailureDetail_[serverKey] = e.what();
            LogError(serverKey, e.what());
            return nullptr;
        }
    }
    // LSP multi-root follow-up: root, not unconditionally editor::ProjectRoot()
    // -- a subpackage with its own <root>/.ned/settings.json gets its own
    // settings, same as it would opening that subdirectory as its own
    // top-level project.
    const editor::ProjectSettings projectSettings = editor::LoadProjectSettings(root);
    WireNotificationHandlers(*client, serverKey, connectionKey, projectSettings.lspWorkspaceConfiguration);

    LspClient* rawClient = client.get();
    // project-settings-lsp-init-options follow-up: initializationOptions
    // covers servers that only read config at handshake time;
    // workspace/didChangeConfiguration (sent right after "initialized",
    // only when lspWorkspaceConfiguration is non-empty) covers the "push"
    // model some servers expect instead -- see ProjectSettings.h's own doc
    // comment on lspWorkspaceConfiguration for why both exist side by side.
    rawClient->SendRequest(
        "initialize", BuildInitializeParams(root, editor::LspInitializationOptionsForLanguage(projectSettings, serverKey)),
        [this, rawClient, serverKey, connectionKey, workspaceConfiguration = projectSettings.lspWorkspaceConfiguration](
            std::optional<Json> result, std::optional<Json> error) {
            // hang-on-timed-out-initialize follow-up: ExpireStaleRequests
            // invokes this with (nullopt, a synthesized timeout error) if
            // the server never responds -- previously this branch was
            // unreachable in practice because both parameters were ignored,
            // so a timed-out handshake still opened the queued-notification
            // gate and flushed everything queued behind it (including a
            // full-document textDocument/didChange) into a server that had
            // already proven unresponsive, wedging the write.
            if (error) {
                LogError(serverKey, "initialize failed: " + ExtractErrorMessage(*error));
                disconnectDetail_[serverKey] = ExtractErrorMessage(*error);
                ClientDisconnected(serverKey, connectionKey);
                return;
            }
            // semantic-tokens/on-type-formatting follow-up: the only two
            // pieces of this response this class keeps -- see
            // SemanticTokensLegendFor/OnTypeFormattingTriggersFor's own doc
            // comment in LspManager.h for why. Absent means whatever result
            // is present, the provider just isn't advertised.
            if (result) {
                if (const auto legend = ExtractSemanticTokensLegend(*result)) {
                    semanticTokensLegend_[serverKey] = *legend;
                }
                if (const auto triggers = ExtractOnTypeFormattingTriggers(*result)) {
                    onTypeFormattingTriggers_[serverKey] = *triggers;
                }
                if (const auto syncKind = ExtractTextDocumentSyncKind(*result)) {
                    textDocumentSyncKind_[serverKey] = *syncKind;
                }
            }
            rawClient->SendNotification("initialized", Json::object());
            if (!workspaceConfiguration.empty()) {
                rawClient->SendNotification("workspace/didChangeConfiguration", Json{{"settings", workspaceConfiguration}});
            }
        });

    clients_.emplace(connectionKey, std::move(client));
    if (brokerBacked) {
        brokerBackedLanguages_.insert(connectionKey);
    }
    else {
        brokerBackedLanguages_.erase(connectionKey); // a stale mark from a prior broker-backed client must not outlive a direct respawn
    }
    // mode-line-lsp-status-round-2 follow-up: a successful (re)spawn
    // resolves any prior disconnect -- StatusForLanguage should report
    // Running now, not a stale Disconnected from before this attempt.
    disconnectedLanguages_.erase(serverKey);
    disconnectDetail_.erase(serverKey);
    lastDisconnectAt_.erase(serverKey); // respawn-debounce follow-up -- a stale cooldown must not outlive a real respawn
    return rawClient;
}

void LspManager::SyncBuffer(text::Buffer& buffer, const std::string& language) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    primaryServerKey_[&buffer] = language; // see PrimarySyncState's own doc comment

    // huge-file-lsp-gate follow-up: see this method's own header comment --
    // a huge buffer gets neither sync, ever, rather than paying a
    // buffer.Text() materialization just to discover no server can sanely
    // use the result.
    if (buffer.Content().IsHuge()) {
        if (hugeSyncSkipNotified_.insert(&buffer).second) {
            LogError(language, "\"" + buffer.Name() +
                                   "\" is too large for LSP/prose-checker sync -- diagnostics, completion, "
                                   "and spell-checking are unavailable on this buffer");
        }
        return;
    }

    // LSP multi-root follow-up: resolved once per buffer, reused for both
    // syncs below and (if SyncEmbeddedDocuments follows) every embedded key
    // too -- see this method's own doc comment.
    const std::filesystem::path root = ResolveCachedRoot(*buffer.Path(), language);
    bufferResolvedRoot_[&buffer]     = root;

    SyncToServer(buffer, language, language, root);                       // primary language server
    SyncToServer(buffer, std::string(kProseLanguageKey), language, root); // prose checker, independent of the above
}

void LspManager::SyncEmbeddedDocuments(text::Buffer& buffer, const std::vector<EmbeddedDocumentSync>& documents) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    // LSP multi-root follow-up: an embedded document shares its host
    // buffer's own resolved root, never a root resolved from its own
    // embedded language -- one buffer has exactly one LSP root. Stamped by
    // SyncBuffer, which always precedes this call for the same buffer
    // within a frame (BufferView::Paint()); the editor::ProjectRoot()
    // fallback below should never actually trigger in practice, but keeps
    // this method total regardless of call order.
    const std::filesystem::path root =
        bufferResolvedRoot_.contains(&buffer) ? bufferResolvedRoot_[&buffer] : editor::ProjectRoot();

    std::unordered_set<std::string> desiredKeys;
    for (const EmbeddedDocumentSync& document : documents) {
        desiredKeys.insert(document.language);
        embeddedOwnedRanges_[&buffer][document.language] = document.ownedRanges;
        SyncTextToServer(buffer, document.language, document.language, document.documentText, root);
    }

    // Tear down any server key this buffer was previously embedded-synced
    // to but that documents no longer contains -- its only region was
    // deleted (or the buffer switched to a mode/state with none at all).
    // Left running, it would keep reporting stale diagnostics for content
    // that no longer exists in the buffer.
    bool diagnosticsChanged = false;
    for (const std::string& key : embeddedServerKeys_[&buffer]) {
        if (desiredKeys.contains(key)) {
            continue; // still wanted -- SyncTextToServer above already handled it
        }
        if (const auto bufferIt = bufferState_.find(&buffer); bufferIt != bufferState_.end()) {
            if (const auto stateIt = bufferIt->second.find(key); stateIt != bufferIt->second.end()) {
                if (stateIt->second.opened) {
                    if (LspClient* client = ExistingClientForLanguage(stateIt->second.connectionKey)) {
                        client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", stateIt->second.uri}}}});
                    }
                }
                bufferIt->second.erase(stateIt);
                if (bufferIt->second.empty()) {
                    bufferState_.erase(bufferIt);
                }
            }
        }
        if (const auto ownedIt = embeddedOwnedRanges_.find(&buffer); ownedIt != embeddedOwnedRanges_.end()) {
            ownedIt->second.erase(key);
        }
        if (const auto diagIt = diagnosticsBySource_.find(&buffer); diagIt != diagnosticsBySource_.end()) {
            if (diagIt->second.erase(key) > 0) {
                diagnosticsChanged = true;
            }
        }
    }
    embeddedServerKeys_[&buffer] = std::move(desiredKeys);

    if (diagnosticsChanged) {
        PushMergedDiagnostics(buffer);
    }
}

std::vector<std::string> LspManager::ActiveServerKeysForBuffer(const text::Buffer& buffer) const {
    std::vector<std::string> keys;
    const auto               it = bufferState_.find(const_cast<text::Buffer*>(&buffer));
    if (it == bufferState_.end()) {
        return keys;
    }
    keys.reserve(it->second.size());
    for (const auto& [serverKey, state] : it->second) {
        keys.push_back(serverKey);
    }
    return keys;
}

void LspManager::SyncToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                              const std::filesystem::path& root) {
    // progressive-huge-file-load follow-up: checked here, ahead of the
    // buffer.Text() argument below, rather than relying solely on
    // SyncTextToServer's own (still-kept, still-needed-by-
    // SyncEmbeddedDocuments) client check -- buffer.Text() unconditionally
    // materializes the whole document (Storage_->ToString()), which for a
    // huge buffer with no server configured for its language was a real,
    // reproduced live hang: every SyncBackgroundBuffers tick paid two full
    // multi-GB copies (primary + prose-checker) just to discover there was
    // nothing to send them to, and once that cost exceeds the tick
    // interval the EventLoop::Post queue backs up forever. A no-op buffer
    // argument is never worth evaluating eagerly.
    if (!ClientForLanguage(serverKey, root)) {
        return;
    }

    // per-frame-sync-materialize follow-up: SyncTextToServer's own
    // "nothing changed since the last sync" check happens too late to help
    // here -- buffer.Text() below is a function argument, evaluated
    // unconditionally before SyncTextToServer's body ever runs. Called
    // every Paint() for the focused buffer, that meant a buffer already
    // synced and unchanged still paid a full ITextStorage::ToString() on
    // every repaint, forever -- for a several-GB buffer, a reproduced live
    // hang (see this file's own history). Duplicating the same check here,
    // ahead of buffer.Text(), is what actually makes the "second call is a
    // no-op" claim true.
    BufferSyncState* existingState = nullptr;
    if (const auto bufferIt = bufferState_.find(&buffer); bufferIt != bufferState_.end()) {
        if (const auto stateIt = bufferIt->second.find(serverKey); stateIt != bufferIt->second.end()) {
            existingState = &stateIt->second;
            if (existingState->opened && existingState->lastSyncedGeneration == buffer.ContentGeneration()) {
                return; // nothing changed since the last sync
            }
        }
    }

    if (!existingState || !existingState->opened) {
        // Not yet opened -- didOpen must stay immediate, never debounced (a
        // newly visible buffer needs diagnostics/highlighting right away,
        // not after an arbitrary delay); SyncTextToServer's own
        // !state.opened branch is what actually sends it.
        SyncTextToServer(buffer, serverKey, languageId, buffer.Text(), root);
        return;
    }

    // sync-debounce follow-up: already open, content changed -- debounce
    // the actual textDocument/didChange send instead of materializing
    // buffer.Text() and sending synchronously right here. A real,
    // gdb-confirmed live freeze traced to exactly this send happening on
    // every single keystroke (see LspServerConfig.h's LspSyncDebounceMs
    // doc comment): the main thread blocked inside ChildProcess::WriteAll,
    // stuck writing a full-document sync to a server whose stdin pipe
    // couldn't drain fast enough. See BufferSyncState::pendingSyncGeneration's
    // own doc comment for why this guards against re-arming on every
    // Paint(), not just on a genuine new edit.
    if (existingState->pendingSyncGeneration && *existingState->pendingSyncGeneration == buffer.ContentGeneration()) {
        return; // already debounced for this exact generation -- let it run its course
    }
    existingState->pendingSyncGeneration = buffer.ContentGeneration();

    text::Buffer* const bufferPtr = &buffer;
    syncDebounceTimers_[&buffer][serverKey].Arm(
        eventLoop_, std::chrono::milliseconds(LspSyncDebounceMs()), [this, bufferPtr, serverKey, languageId, root] {
            // Re-reads buffer.Text() fresh here, not at arm time -- more
            // edits may have landed during the debounce window, and this
            // must send the *latest* content, not a stale snapshot.
            SyncTextToServer(*bufferPtr, serverKey, languageId, bufferPtr->Text(), root);
        });
}

void LspManager::SyncTextToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                                  const std::string& documentText, const std::filesystem::path& root) {
    LspClient* client = ClientForLanguage(serverKey, root);
    if (!client) {
        return; // nothing configured/running for this server
    }

    BufferSyncState& state = bufferState_[&buffer][serverKey];

    if (!state.opened) {
        // prose-checking follow-up: never runs prose checking against a
        // binary buffer -- keyed off the same LooksBinary heuristic
        // Buffer::FromFile/ProjectSearch already use, not a new one.
        // Scoped to the prose checker specifically (not the primary
        // language server, whose own behavior here predates this feature
        // and is out of its scope) -- sending harper-ls raw binary content
        // as "text" is pure waste at best. Checked only on the not-yet-
        // opened path, not every sync, to avoid a disk read every frame.
        //
        // std::filesystem::exists is checked first: LooksBinary treats an
        // unreadable path as binary too (a sensible default for its own
        // original "read the file" callers), but buffer.Path() naming a
        // file that doesn't exist on disk yet just means an unsaved new
        // buffer -- there's no on-disk content to be binary, and that must
        // not be conflated with an actually-binary file.
        if (serverKey == kProseLanguageKey && std::filesystem::exists(*buffer.Path()) && text::LooksBinary(*buffer.Path())) {
            return;
        }
        state.connectionKey = ConnectionKey(root, serverKey);
        state.uri           = PathToUri(*buffer.Path());
        state.version       = 1;
        client->SendNotification("textDocument/didOpen", {
                                                             {"textDocument",
                                                              {
                                                                  {"uri", state.uri},
                                                                  {"languageId", languageId},
                                                                  {"version", state.version},
                                                                  {"text", documentText},
                                                              }},
                                                         });
        state.opened               = true;
        state.lastSyncedGeneration = buffer.ContentGeneration();
        state.lastSyncedText       = documentText; // incremental-sync follow-up: baseline for the first didChange's diff
        // pull-diagnostics follow-up: same cadence as didOpen/didChange
        // itself, no separate debounce timer -- see RequestPullDiagnostics'
        // own doc comment in LspManager.h. Opt-in (LspPullDiagnosticsEnabled,
        // default false): unconditionally, this would mean one extra
        // request per content sync for every server, forever, whether or
        // not it actually needs pull diagnostics at all.
        if (LspPullDiagnosticsEnabled()) {
            RequestPullDiagnostics(buffer, serverKey);
        }
        return;
    }

    if (buffer.ContentGeneration() == state.lastSyncedGeneration) {
        return; // nothing changed since the last sync
    }

    ++state.version;
    if (TextDocumentSyncKindFor(serverKey) == TextDocumentSyncKind::Incremental) {
        // incremental-sync follow-up: common-prefix/common-suffix byte diff,
        // the same shape as IncrementalParseCache::Update's own diff
        // (Editor/TreeSitter/IncrementalParse.cpp) -- "correct, if not
        // always maximally minimal" is enough here too: a multi-cursor edit
        // or a large external revert just widens to one outer span, which
        // is spec-legal for a single contentChanges[0] entry. oldText may
        // also be a differently-padded rebuild of the same embedded region
        // rather than a minimal edit of it (SyncEmbeddedDocuments
        // periodically rebuilds virtual-document text with shifted
        // whitespace padding) -- this walk handles that the same as any
        // other "far apart" edit, no special-casing needed: it just finds a
        // smaller common prefix/suffix and sends a wider span.
        const std::string& oldText   = state.lastSyncedText;
        const std::size_t  maxCommon = std::min(oldText.size(), documentText.size());
        std::size_t        prefix    = 0;
        while (prefix < maxCommon && oldText[prefix] == documentText[prefix]) {
            ++prefix;
        }
        const std::size_t maxSuffix = maxCommon - prefix;
        std::size_t       suffix    = 0;
        while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == documentText[documentText.size() - 1 - suffix]) {
            ++suffix;
        }
        const std::size_t oldStartByte = prefix;
        const std::size_t oldEndByte   = oldText.size() - suffix;
        const std::size_t newStartByte = prefix;
        const std::size_t newEndByte   = documentText.size() - suffix;

        const LspRange    range       = ByteRangeToLspRange(oldText, oldStartByte, oldEndByte);
        const std::size_t rangeLength = Utf16LengthOfByteRange(oldText, oldStartByte, oldEndByte);
        const std::string changedText = documentText.substr(newStartByte, newEndByte - newStartByte);

        client->SendNotification(
            "textDocument/didChange",
            {
                {"textDocument", {{"uri", state.uri}, {"version", state.version}}},
                {"contentChanges", Json::array({{
                                       {"range",
                                        {{"start", {{"line", range.start.line}, {"character", range.start.character}}},
                                         {"end", {{"line", range.end.line}, {"character", range.end.character}}}}},
                                       {"rangeLength", rangeLength},
                                       {"text", changedText},
                                   }})},
            });
    }
    else {
        client->SendNotification("textDocument/didChange", {
                                                               {"textDocument", {{"uri", state.uri}, {"version", state.version}}},
                                                               {"contentChanges", Json::array({{{"text", documentText}}})},
                                                           });
    }
    state.lastSyncedGeneration = buffer.ContentGeneration();
    state.lastSyncedText       = documentText;
    if (LspPullDiagnosticsEnabled()) {
        RequestPullDiagnostics(buffer, serverKey);
    }
}

LspManager::BufferSyncState* LspManager::PrimarySyncState(text::Buffer& buffer) {
    const auto keyIt = primaryServerKey_.find(&buffer);
    if (keyIt == primaryServerKey_.end()) {
        return nullptr;
    }
    const auto bufferIt = bufferState_.find(&buffer);
    if (bufferIt == bufferState_.end()) {
        return nullptr;
    }
    const auto stateIt = bufferIt->second.find(keyIt->second);
    return stateIt != bufferIt->second.end() ? &stateIt->second : nullptr;
}

LspManager::BufferSyncState* LspManager::ResolveSyncState(text::Buffer& buffer, const std::string& serverKey) {
    if (serverKey.empty()) {
        return PrimarySyncState(buffer);
    }
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end()) {
        return nullptr;
    }
    const auto stateIt = it->second.find(serverKey);
    return stateIt != it->second.end() ? &stateIt->second : nullptr;
}

LspClient& LspManager::SetClientForTesting(std::string language, std::unique_ptr<LspClient> client,
                                           const Json& workspaceConfiguration, bool brokerBacked,
                                           std::optional<std::string> connectionKeyOverride) {
    // LSP multi-root follow-up: nullopt (every pre-existing call site)
    // registers under language itself, unchanged -- see this method's own
    // doc comment in LspManager.h.
    const std::string connectionKey = connectionKeyOverride.value_or(language);
    WireNotificationHandlers(*client, language, connectionKey, workspaceConfiguration); // same wiring ClientForLanguage's real spawn path applies
    disconnectedLanguages_.erase(language);                                             // an injected client is "running," same as a real successful spawn
    disconnectDetail_.erase(language);
    lastDisconnectAt_.erase(language); // respawn-debounce follow-up -- ditto
    if (brokerBacked) {
        brokerBackedLanguages_.insert(connectionKey);
    }
    else {
        brokerBackedLanguages_.erase(connectionKey);
    }
    LspClient& ref          = *client;
    clients_[connectionKey] = std::move(client);
    return ref;
}

void LspManager::ClientDisconnected(const std::string& serverKey, const std::string& connectionKey) {
    // Both may be references into the very LspClient (and its
    // OnDisconnected closure) this function destroys below -- copy them
    // first so the rest of this function isn't reading freed memory.
    const std::string languageCopy      = serverKey;
    const std::string connectionKeyCopy = connectionKey;
    // lsp-use-after-free follow-up: this used to retire into a retired_
    // vector instead of erasing immediately, on the theory that "wait for
    // the next periodic tick before actually freeing" gave any in-flight
    // Post()ed callback time to drain first. Confirmed live via ASan that
    // this isn't actually safe -- LspClient::ExpireStaleRequests's own
    // periodic tick and a client's background thread both Post() against
    // EventLoop independently, with no ordering guarantee between them, so
    // the tick can free a just-retired client while another callback for
    // that exact object is still queued. The real fix now lives in
    // LspClient itself (see its header comment on alive_) -- a stray
    // Post()ed callback safely no-ops instead of touching freed memory
    // regardless of when this destroys the object, so plain immediate
    // erase() is safe again and retired_ is gone.
    clients_.erase(connectionKeyCopy);
    brokerBackedLanguages_.erase(connectionKeyCopy); // graceful-lsp-shutdown follow-up -- must not outlive the client it described
    // semantic-tokens/on-type-formatting follow-up: a respawned server may
    // advertise a different legend/trigger set than the one that just
    // died -- don't let a stale entry outlive this connection.
    semanticTokensLegend_.erase(languageCopy);
    onTypeFormattingTriggers_.erase(languageCopy);
    textDocumentSyncKind_.erase(languageCopy);       // ditto -- a respawned server may advertise a different sync kind
    pullDiagnosticsUnsupported_.erase(languageCopy); // a respawned server gets one fresh attempt
    inlayHintsUnsupported_.erase(languageCopy);       // ditto
    codeLensUnsupported_.erase(languageCopy);         // ditto
    // mode-line-lsp-status-round-2 follow-up: latch the disconnect so
    // StatusForLanguage can report it, distinct from "never configured" --
    // cleared the moment a fresh spawn succeeds (ClientForLanguage) or the
    // reconfigured command fails outright (StatusForLanguage's SpawnFailed
    // case takes priority over this one regardless).
    disconnectedLanguages_.insert(languageCopy);

    // crash-loop-respawn-guard follow-up: see disconnectBurst_'s own doc
    // comment in LspManager.h. Must run before this function returns (every
    // exit path below still respawns on the next SyncBuffer otherwise).
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    lastDisconnectAt_[languageCopy]                 = now; // respawn-debounce follow-up
    auto& burst                                     = disconnectBurst_[languageCopy];
    if (now - burst.first > kCrashLoopWindow) {
        burst = {now, 1};
    }
    else {
        ++burst.second;
    }
    if (burst.second >= kCrashLoopThreshold) {
        // Latches failedCommands_ -- ClientForLanguage's own pre-existing
        // "known-bad command, stop retrying until reconfigured" guard --
        // rather than returning early here, so the ordinary cleanup below
        // (bufferState_/diagnosticsBySource_/activeProgress_) still runs
        // exactly as it would for any other disconnect.
        const std::optional<std::vector<std::string>> command =
            (languageCopy == kProseLanguageKey) ? ProseCheckerCommand() : LspServerCommand(languageCopy);
        if (command) {
            failedCommands_[languageCopy] = *command;
        }
        spawnFailureDetail_[languageCopy] =
            "gave up after " + std::to_string(burst.second) + " immediate disconnects in a row -- reconfigure the command to retry";
        disconnectBurst_.erase(languageCopy);
        LogError(languageCopy, "server crash-looped -- giving up until the command is reconfigured");
    }
    // prose-checking follow-up: erase just this server's own sub-entry, not
    // the whole buffer -- a buffer's other server (primary or prose,
    // whichever languageCopy isn't) must keep its own sync state and
    // diagnostics intact. Drop the outer entry too once it's left empty,
    // and drop + re-flatten this server's now-stale diagnostics slice so
    // Buffer::Diagnostics() doesn't keep reporting from a server that's no
    // longer there.
    for (auto it = bufferState_.begin(); it != bufferState_.end();) {
        it->second.erase(languageCopy);
        if (it->second.empty()) {
            it = bufferState_.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = diagnosticsBySource_.begin(); it != diagnosticsBySource_.end();) {
        if (it->second.erase(languageCopy) > 0) {
            PushMergedDiagnostics(*it->first);
        }
        if (it->second.empty()) {
            it = diagnosticsBySource_.erase(it);
        }
        else {
            ++it;
        }
    }
    // workDoneProgress-support follow-up: a dying server never sends "end"
    // for its live progress sessions -- End them here or the spinner runs
    // forever (the request-count half of the same problem is ~LspClient's
    // own responsibility; see its destructor comment).
    const std::string keyPrefix = languageCopy + '\x1f';
    for (auto it = activeProgress_.begin(); it != activeProgress_.end();) {
        if (it->first.rfind(keyPrefix, 0) == 0) {
            it = activeProgress_.erase(it);
            EndBackgroundActivity(kLspActivity);
        }
        else {
            ++it;
        }
    }
    if (activeProgress_.empty()) {
        SetBackgroundActivityDetail(kLspActivity, std::string()); // same stale-detail rule HandleProgress' own end branch applies
    }
}

void LspManager::LogError(std::string_view language, std::string_view message) {
    text::Buffer* log = bufferList_.Find(std::string(kLspLogBufferName));
    if (!log) {
        log = &bufferList_.CreateBuffer(std::string(kLspLogBufferName));
        log->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }
    log->AppendWhileReadOnly(FormatLogLine(language, message));
    hasUnseenLogEntry_ = true;
}

bool LspManager::HasUnseenLogEntry() const {
    return hasUnseenLogEntry_;
}

void LspManager::AcknowledgeLogEntry() {
    hasUnseenLogEntry_ = false;
}

LspManager::LspStatus LspManager::StatusForLanguage(const std::string& language) const {
    if (ExistingClientForLanguage(language) != nullptr) {
        return LspStatus::Running;
    }
    if (failedCommands_.contains(language)) {
        return LspStatus::SpawnFailed;
    }
    if (disconnectedLanguages_.contains(language)) {
        return LspStatus::Disconnected;
    }
    return LspStatus::NotConfigured;
}

std::string LspManager::SpawnFailureDetail(const std::string& language) const {
    const auto it = spawnFailureDetail_.find(language);
    return it != spawnFailureDetail_.end() ? it->second : std::string();
}

std::string LspManager::DisconnectReason(const std::string& language) const {
    const auto it = disconnectDetail_.find(language);
    return it != disconnectDetail_.end() ? it->second : std::string();
}

std::optional<SemanticTokensLegend> LspManager::SemanticTokensLegendFor(const std::string& serverKey) const {
    const auto it = semanticTokensLegend_.find(serverKey);
    return it != semanticTokensLegend_.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<OnTypeFormattingTriggers> LspManager::OnTypeFormattingTriggersFor(const std::string& serverKey) const {
    const auto it = onTypeFormattingTriggers_.find(serverKey);
    return it != onTypeFormattingTriggers_.end() ? std::optional(it->second) : std::nullopt;
}

TextDocumentSyncKind LspManager::TextDocumentSyncKindFor(const std::string& serverKey) const {
    const auto it = textDocumentSyncKind_.find(serverKey);
    return it != textDocumentSyncKind_.end() ? it->second : TextDocumentSyncKind::Full;
}

void LspManager::NotifyBufferClosed(text::Buffer& buffer) {
    const auto it = bufferState_.find(&buffer);
    if (it != bufferState_.end()) {
        // prose-checking follow-up: buffer may have up to two sync states
        // (primary + prose) -- notify every server it was ever opened with,
        // not just one.
        for (const auto& perServer : it->second) {
            const BufferSyncState& state = perServer.second;
            if (state.opened) {
                if (LspClient* client = ExistingClientForLanguage(state.connectionKey)) {
                    client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", state.uri}}}});
                }
            }
        }
        bufferState_.erase(it);
    }
    diagnosticsBySource_.erase(&buffer);
    diagnosticsDebounceTimers_.erase(&buffer); // cancels a pending timer before it can fire against a dead buffer
    syncDebounceTimers_.erase(&buffer);        // sync-debounce follow-up: same rationale, for a pending didChange send
    primaryServerKey_.erase(&buffer);
    bufferResolvedRoot_.erase(&buffer); // LSP multi-root follow-up
    embeddedServerKeys_.erase(&buffer);
    embeddedOwnedRanges_.erase(&buffer);
    hugeSyncSkipNotified_.erase(&buffer);
    semanticTokensRequestedGeneration_.erase(&buffer);
    semanticTokensRequestCounter_.erase(&buffer);
    semanticTokenSpans_.erase(&buffer);
    semanticTokensGeneration_.erase(&buffer);
    inlayHintsRequestedRange_.erase(&buffer);
    inlayHintsRequestCounter_.erase(&buffer);
    inlayHintSpans_.erase(&buffer);
    codeLensRequestedGeneration_.erase(&buffer);
    codeLensRequestCounter_.erase(&buffer);
    codeLensSpans_.erase(&buffer);
}

void LspManager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // reentrant-expiry-during-iteration follow-up: a stale *initialize*
    // request's synthesized-timeout callback (SpawnClient's own lambda,
    // above) calls ClientDisconnected on error, which erases the client
    // from clients_ immediately (see that function's own comment on why
    // that's correct) -- and that call can happen synchronously, from
    // inside entry.second->ExpireStaleRequests(maxAge) below, while this
    // very range-for loop is iterating clients_. Erasing the element the
    // loop is currently visiting invalidates its iterator; the loop's own
    // ++it (or a later entry sharing a since-invalidated bucket) then reads
    // freed map-node memory -- confirmed live via a real SIGSEGV, and
    // reproduced deterministically under ASan (heap-use-after-free, this
    // exact line) once the fix below was reverted. LspClient::
    // ExpireStaleRequests already guards its own pending_ map this same way
    // (see its own comment); this is that same fix one level up. Snapshot
    // the keys first, then re-resolve each via a fresh find() right before
    // use, so a disconnect cascaded from an earlier language in this same
    // pass is observed as "already gone" instead of dereferencing a stale
    // iterator/pointer.
    std::vector<std::string> languages;
    languages.reserve(clients_.size());
    for (const auto& entry : clients_) {
        languages.push_back(entry.first);
    }
    for (const std::string& language : languages) {
        if (const auto it = clients_.find(language); it != clients_.end()) {
            it->second->ExpireStaleRequests(maxAge);
        }
    }
}

void LspManager::HandlePublishDiagnostics(const Json& params, const std::string& language) {
    if (!params.contains("uri")) {
        return;
    }
    const std::optional<std::filesystem::path> path = UriToPath(params["uri"].get<std::string>());
    if (!path) {
        return;
    }

    text::Buffer* buffer = bufferList_.FindByPath(*path);
    if (!buffer) {
        return; // not an open buffer -- nothing to update
    }

    // prose-diagnostic-callout follow-up: language is the same per-server
    // key HandlePublishDiagnostics is registered under (see the
    // SetNotificationHandler wiring above, capturing `language` per
    // language) -- kProseLanguageKey identifies the reserved prose-checker
    // connection, everything else is a real code language server.
    const text::Buffer::Diagnostic::Origin origin =
        (language == kProseLanguageKey) ? text::Buffer::Diagnostic::Origin::Prose : text::Buffer::Diagnostic::Origin::Code;

    std::vector<text::Buffer::Diagnostic> diagnostics;
    if (params.contains("diagnostics")) {
        const text::ITextStorage& content = buffer->Content();
        for (const Json& item : params["diagnostics"]) {
            const Json& range = item.value("range", Json::object());
            const Json& start = range.value("start", Json::object());
            const Json& end   = range.value("end", Json::object());

            const std::size_t startByte =
                LspPositionToByte(content, LspPosition{.line      = start.value("line", static_cast<std::size_t>(0)),
                                                       .character = start.value("character", static_cast<std::size_t>(0))});
            const std::size_t endByte =
                LspPositionToByte(content, LspPosition{.line      = end.value("line", static_cast<std::size_t>(0)),
                                                       .character = end.value("character", static_cast<std::size_t>(0))});

            diagnostics.push_back(text::Buffer::Diagnostic{
                .startByte = startByte,
                .endByte   = endByte,
                .severity  = SeverityFromLsp(item.value("severity", 3)),
                .origin    = origin,
                .message   = item.value("message", std::string()),
            });
        }
    }
    FilterToOwnedRanges(buffer, language, diagnostics);

    // prose-checking follow-up: this server's own full current diagnostic
    // set for buffer replaces only its own slice -- another server's slice
    // (recorded independently the same way) is untouched. PushMergedDiagnostics
    // is what actually reaches buffer.SetDiagnostics.
    diagnosticsBySource_[buffer][language] = std::move(diagnostics);

    // diagnostics-debounce follow-up: applying this immediately would mean
    // inline diagnostics repaint on essentially every keystroke (a server
    // re-publishes after every didChange, which SyncBuffer sends on every
    // content-generation bump) -- (re)arm buffer's own debounce timer
    // instead, collapsing a rapid-typing burst of publishes into one
    // application once the buffer goes quiet for a beat.
    diagnosticsDebounceTimers_[buffer].Arm(eventLoop_, std::chrono::milliseconds(LspDiagnosticsDebounceMs()),
                                           [this, buffer] { PushMergedDiagnostics(*buffer); });
}

void LspManager::FilterToOwnedRanges(text::Buffer* buffer, const std::string& language,
                                     std::vector<text::Buffer::Diagnostic>& diagnostics) const {
    // embedded-language-documents follow-up: an embedded server (one with an
    // owned-ranges record) only ever legitimately reports within its own
    // owned regions -- a padded/blanked region should tokenize as inert
    // whitespace, so a diagnostic starting outside every owned range is
    // either a rare parser edge case at a padding boundary or a server
    // ignoring content it wasn't asked about. Dropped defensively rather
    // than surfaced against the wrong language's chrome. No effect on the
    // primary language or kProseLanguageKey, neither of which ever has an
    // owned-ranges entry (they own the whole buffer).
    const auto ownedIt = embeddedOwnedRanges_.find(buffer);
    if (ownedIt == embeddedOwnedRanges_.end()) {
        return;
    }
    const auto rangeIt = ownedIt->second.find(language);
    if (rangeIt == ownedIt->second.end()) {
        return;
    }
    const std::vector<std::pair<std::size_t, std::size_t>>& ranges = rangeIt->second;
    std::erase_if(diagnostics, [&ranges](const text::Buffer::Diagnostic& diagnostic) {
        for (const auto& range : ranges) {
            if (diagnostic.startByte >= range.first && diagnostic.startByte < range.second) {
                return false;
            }
        }
        return true;
    });
}

void LspManager::RequestPullDiagnostics(text::Buffer& buffer, const std::string& serverKey) {
    if (pullDiagnosticsUnsupported_.contains(serverKey)) {
        return; // learned once that this server doesn't support textDocument/diagnostic
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    // uri/language captured by value, not the buffer itself: the response
    // arrives after a real async round trip, during which the buffer could
    // legitimately close -- resolved fresh (by uri, via bufferList_) inside
    // the callback below rather than holding a pointer across the gap, the
    // same resolve-fresh-not-capture-stale discipline
    // HandlePublishDiagnostics itself already follows for a notification
    // arriving whenever the server feels like sending it.
    const std::string uri      = state->uri;
    const std::string  language = state->connectionKey;
    const Json         params   = {{"textDocument", {{"uri", uri}}}};
    client->SendRequest(
        "textDocument/diagnostic", params,
        [this, uri, language](std::optional<Json> result, std::optional<Json> error) {
            if (error) {
                // A real error response (as opposed to a legitimate "no
                // diagnostics right now" empty items array) is this
                // server's own proof it doesn't implement the method --
                // stop asking for the rest of this connection's lifetime
                // rather than re-erroring on every sync. LSP multi-root
                // follow-up: latched under `language` (state->connectionKey,
                // == serverKey in the common single-root case) rather than
                // this call's own serverKey parameter -- in a genuine
                // multi-root scenario this just means the latch is learned
                // per-connection instead of per-language, a strictly finer
                // (never incorrect) grain than the top-of-function check
                // above, at worst one avoidable repeat request for a second
                // same-language server against a different root.
                pullDiagnosticsUnsupported_.insert(language);
                return;
            }
            if (!result) {
                return;
            }
            const std::optional<std::vector<PullDiagnosticItem>> items = ExtractPullDiagnosticReport(*result);
            if (!items) {
                return; // an "unchanged" report, or nothing parseable -- leave the existing slice alone
            }
            const std::optional<std::filesystem::path> path   = UriToPath(uri);
            text::Buffer* const                        buffer = path ? bufferList_.FindByPath(*path) : nullptr;
            if (!buffer) {
                return; // buffer closed since this was requested
            }
            const text::ITextStorage&             content = buffer->Content();
            std::vector<text::Buffer::Diagnostic> diagnostics;
            diagnostics.reserve(items->size());
            for (const PullDiagnosticItem& item : *items) {
                diagnostics.push_back(text::Buffer::Diagnostic{
                    .startByte = LspPositionToByte(content, item.start),
                    .endByte   = LspPositionToByte(content, item.end),
                    .severity  = SeverityFromLsp(item.severity),
                    .origin    = (language == kProseLanguageKey) ? text::Buffer::Diagnostic::Origin::Prose
                                                                 : text::Buffer::Diagnostic::Origin::Code,
                    .message   = item.message,
                });
            }
            FilterToOwnedRanges(buffer, language, diagnostics);
            // Same source-key slot HandlePublishDiagnostics writes into --
            // see this method's own doc comment in LspManager.h for why
            // that's the deliberate choice here.
            diagnosticsBySource_[buffer][language] = std::move(diagnostics);
            PushMergedDiagnostics(*buffer);
        });
}

void LspManager::RequestSemanticTokensFull(text::Buffer& buffer, const std::string& serverKey) {
    if (!LspSemanticHighlightingEnabled()) {
        return;
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    // sync-debounce follow-up: the server may not have this generation's
    // content yet -- SyncToServer's own didChange send is now debounced
    // (LspSyncDebounceMs), so a Paint() can reach here before it's landed.
    // Retried on the next Paint() once it does (see SyncToServer's own doc
    // comment for why that's guaranteed to happen without extra plumbing).
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    if (const auto it = semanticTokensRequestedGeneration_.find(&buffer);
        it != semanticTokensRequestedGeneration_.end() && it->second == buffer.ContentGeneration()) {
        return; // already requested for this exact content -- a cursor-blink/scroll-only repaint, not a real change
    }
    // LSP multi-root follow-up: serverKey (plain), not state->connectionKey
    // -- SemanticTokensLegendFor's own store is deliberately still
    // plain-keyed, see this class's own header comment.
    const std::optional<SemanticTokensLegend> legend = SemanticTokensLegendFor(serverKey);
    if (!legend) {
        return; // server never advertised a legend -- the response would be undecodable anyway
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    const std::size_t requestedGeneration       = buffer.ContentGeneration();
    semanticTokensRequestedGeneration_[&buffer] = requestedGeneration;
    const std::size_t requestId                 = ++semanticTokensRequestCounter_[&buffer];

    text::Buffer* const        bufferPtr  = &buffer;
    const Json                 params     = {{"textDocument", {{"uri", state->uri}}}};
    const SemanticTokensLegend legendCopy = *legend;
    client->SendRequest(
        "textDocument/semanticTokens/full", params,
        [this, bufferPtr, requestId, requestedGeneration, legendCopy](std::optional<Json> result, std::optional<Json> error) {
            const auto counterIt = semanticTokensRequestCounter_.find(bufferPtr);
            if (counterIt == semanticTokensRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error || !result) {
                return; // leave whatever spans were already applied in place
            }
            // stale-position-race follow-up: the response's LspPosition
            // values were computed by the server against the document as it
            // stood AT REQUEST TIME -- if a local edit landed while this was
            // in flight, bufferPtr->Content() below is already the EDITED
            // text, and converting the server's now-stale positions against
            // it silently lands on the wrong bytes (off by however much the
            // document shifted), not an out-of-range failure that would be
            // caught some other way. A real, live-reported bug: syntax
            // coloring visibly detached from the characters it belonged to
            // for a moment after every keystroke. Discarding here is safe --
            // RequestSemanticTokensFull's own debounce-aware re-request gate
            // guarantees a fresh request for the new generation follows once
            // SyncToServer's debounced didChange actually lands (see that
            // function's own doc comment), so this is never a permanent gap,
            // just a stale response correctly thrown away instead of misapplied.
            if (bufferPtr->ContentGeneration() != requestedGeneration) {
                return;
            }
            const std::vector<SemanticToken>   tokens  = ExtractSemanticTokens(*result);
            const text::ITextStorage&          content = bufferPtr->Content();
            std::vector<editor::HighlightSpan> spans;
            spans.reserve(tokens.size());
            for (const SemanticToken& token : tokens) {
                if (token.tokenTypeIndex >= legendCopy.tokenTypes.size()) {
                    continue; // out-of-range index -- a malformed/mismatched-legend response, skip rather than crash
                }
                const std::optional<editor::SyntaxClass> syntaxClass =
                    SyntaxClassForSemanticTokenType(legendCopy.tokenTypes[token.tokenTypeIndex]);
                if (!syntaxClass) {
                    continue; // no sensible existing class for this token type -- dropped, not force-fit
                }
                // A semantic token never spans multiple lines (per spec) --
                // its end is always {start.line, start.character + length}.
                const LspPosition end{.line = token.start.line, .character = token.start.character + token.length};
                spans.push_back(editor::HighlightSpan{
                    .startByte   = LspPositionToByte(content, token.start),
                    .endByte     = LspPositionToByte(content, end),
                    .syntaxClass = *syntaxClass,
                });
            }
            semanticTokenSpans_[bufferPtr] = std::move(spans);
            ++semanticTokensGeneration_[bufferPtr];
        });
}

const std::vector<editor::HighlightSpan>& LspManager::SemanticTokenSpans(const text::Buffer& buffer) const {
    static const std::vector<editor::HighlightSpan> kEmpty;
    const auto                                      it = semanticTokenSpans_.find(const_cast<text::Buffer*>(&buffer));
    return it != semanticTokenSpans_.end() ? it->second : kEmpty;
}

std::size_t LspManager::SemanticTokensGeneration(const text::Buffer& buffer) const {
    const auto it = semanticTokensGeneration_.find(const_cast<text::Buffer*>(&buffer));
    return it != semanticTokensGeneration_.end() ? it->second : 0;
}

void LspManager::RequestInlayHints(text::Buffer& buffer, std::size_t viewportStartByte, std::size_t viewportEndByte,
                                   const std::string& serverKey) {
    if (!LspInlayHintsEnabled()) {
        return;
    }
    if (inlayHintsUnsupported_.contains(serverKey)) {
        return; // learned once that this server doesn't support textDocument/inlayHint
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    // sync-debounce follow-up: see RequestSemanticTokensFull's own doc
    // comment for why this guard exists now.
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    const auto requestedRange = std::make_tuple(buffer.ContentGeneration(), viewportStartByte, viewportEndByte);
    if (const auto it = inlayHintsRequestedRange_.find(&buffer); it != inlayHintsRequestedRange_.end() && it->second == requestedRange) {
        return; // already requested for this exact (content, viewport) -- a cursor-blink/scroll-into-the-same-view repaint
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    inlayHintsRequestedRange_[&buffer]    = requestedRange;
    const std::size_t requestId           = ++inlayHintsRequestCounter_[&buffer];
    const std::size_t requestedGeneration = buffer.ContentGeneration();

    const text::ITextStorage& content   = buffer.Content();
    const LspPosition         start     = BytePositionToLsp(content, viewportStartByte);
    const LspPosition         end       = BytePositionToLsp(content, viewportEndByte);
    text::Buffer* const       bufferPtr = &buffer;
    const Json                params    = {
        {"textDocument", {{"uri", state->uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
    };
    client->SendRequest(
        "textDocument/inlayHint", params,
        [this, bufferPtr, requestId, requestedGeneration, serverKey](std::optional<Json> result, std::optional<Json> error) {
            const auto counterIt = inlayHintsRequestCounter_.find(bufferPtr);
            if (counterIt == inlayHintsRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error) {
                // A real error response is this server's own proof it
                // doesn't implement the method -- stop asking for the rest
                // of this connection's lifetime rather than re-erroring on
                // every viewport change.
                inlayHintsUnsupported_.insert(serverKey);
                return;
            }
            if (!result) {
                return;
            }
            // stale-position-race follow-up: see RequestSemanticTokensFull's
            // own doc comment on requestedGeneration -- same race, same fix:
            // a hint position computed against the document as it stood at
            // request time must not be converted against a since-edited
            // bufferPtr->Content() below. A real, live-reported symptom: a
            // parameter-name hint appearing to render one character off from
            // its real argument right after a keystroke on an earlier line.
            if (bufferPtr->ContentGeneration() != requestedGeneration) {
                return;
            }
            const std::vector<InlayHint>   hints   = ExtractInlayHints(*result);
            const text::ITextStorage&      content = bufferPtr->Content();
            std::vector<ResolvedInlayHint> resolved;
            resolved.reserve(hints.size());
            for (const InlayHint& hint : hints) {
                resolved.push_back(ResolvedInlayHint{.byteOffset = LspPositionToByte(content, hint.position), .label = hint.label});
            }
            std::sort(resolved.begin(), resolved.end(),
                      [](const ResolvedInlayHint& a, const ResolvedInlayHint& b) { return a.byteOffset < b.byteOffset; });
            inlayHintSpans_[bufferPtr] = std::move(resolved);
        });
}

const std::vector<LspManager::ResolvedInlayHint>& LspManager::InlayHintSpans(const text::Buffer& buffer) const {
    static const std::vector<ResolvedInlayHint> kEmpty;
    const auto                                  it = inlayHintSpans_.find(const_cast<text::Buffer*>(&buffer));
    return it != inlayHintSpans_.end() ? it->second : kEmpty;
}

void LspManager::RequestCodeLenses(text::Buffer& buffer, const std::string& serverKey) {
    if (!LspCodeLensEnabled()) {
        return;
    }
    if (codeLensUnsupported_.contains(serverKey)) {
        return; // learned once that this server doesn't support textDocument/codeLens
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    // sync-debounce follow-up: see RequestSemanticTokensFull's own doc
    // comment for why this guard exists now.
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    if (const auto it = codeLensRequestedGeneration_.find(&buffer);
        it != codeLensRequestedGeneration_.end() && it->second == buffer.ContentGeneration()) {
        return; // already requested for this exact content
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    codeLensRequestedGeneration_[&buffer] = buffer.ContentGeneration();
    const std::size_t requestId           = ++codeLensRequestCounter_[&buffer];

    text::Buffer* const bufferPtr = &buffer;
    const Json          params    = {{"textDocument", {{"uri", state->uri}}}};
    client->SendRequest(
        "textDocument/codeLens", params,
        [this, bufferPtr, requestId, serverKey](std::optional<Json> result, std::optional<Json> error) {
            const auto counterIt = codeLensRequestCounter_.find(bufferPtr);
            if (counterIt == codeLensRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error) {
                codeLensUnsupported_.insert(serverKey);
                return;
            }
            if (!result) {
                return;
            }
            const std::vector<CodeLens>   lenses  = ExtractCodeLenses(*result);
            const text::ITextStorage&     content = bufferPtr->Content();
            std::vector<ResolvedCodeLens> resolved;
            resolved.reserve(lenses.size());
            for (const CodeLens& lens : lenses) {
                resolved.push_back(ResolvedCodeLens{
                    .startByte        = LspPositionToByte(content, lens.start),
                    .endByte          = LspPositionToByte(content, lens.end),
                    .title            = lens.title,
                    .commandName      = lens.commandName,
                    .commandArguments = lens.commandArguments,
                    .hasCommand       = lens.hasCommand,
                    .raw              = lens.raw,
                });
            }
            std::sort(resolved.begin(), resolved.end(),
                      [](const ResolvedCodeLens& a, const ResolvedCodeLens& b) { return a.startByte < b.startByte; });
            codeLensSpans_[bufferPtr] = std::move(resolved);
        });
}

const std::vector<LspManager::ResolvedCodeLens>& LspManager::CodeLensSpans(const text::Buffer& buffer) const {
    static const std::vector<ResolvedCodeLens> kEmpty;
    const auto                                 it = codeLensSpans_.find(const_cast<text::Buffer*>(&buffer));
    return it != codeLensSpans_.end() ? it->second : kEmpty;
}

void LspManager::ResolveCodeLens(text::Buffer& buffer, const ResolvedCodeLens& lens, ResolveCodeLensCallback callback,
                                 const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::size_t startByte = lens.startByte;
    const std::size_t endByte   = lens.endByte;
    client->SendRequest("codeLens/resolve", lens.raw,
                        [callback = std::move(callback), startByte, endByte](std::optional<Json> result, std::optional<Json> error) {
                            if (error || !result) {
                                callback(std::nullopt);
                                return;
                            }
                            const CodeLens resolved = ExtractSingleCodeLens(*result);
                            callback(ResolvedCodeLens{
                                .startByte        = startByte,
                                .endByte          = endByte,
                                .title            = resolved.title,
                                .commandName      = resolved.commandName,
                                .commandArguments = resolved.commandArguments,
                                .hasCommand       = resolved.hasCommand,
                                .raw              = resolved.raw,
                            });
                        });
}

void LspManager::PushMergedDiagnostics(text::Buffer& buffer) {
    std::vector<text::Buffer::Diagnostic> merged;
    if (const auto it = diagnosticsBySource_.find(&buffer); it != diagnosticsBySource_.end()) {
        for (const auto& perSource : it->second) {
            merged.insert(merged.end(), perSource.second.begin(), perSource.second.end());
        }
    }
    buffer.SetDiagnostics(std::move(merged));
}

void LspManager::HandleProgress(const std::string& language, const Json& params) {
    if (!params.contains("token") || !params.contains("value") || !params["value"].is_object()) {
        return;
    }
    const std::string key   = language + '\x1f' + params["token"].dump();
    const Json&       value = params["value"];
    const std::string kind  = value.value("kind", std::string());

    if (kind == "begin") {
        if (activeProgress_.contains(key)) {
            return; // duplicate begin for a live token -- ignore rather than double-count
        }
        activeProgress_[key] = value.value("title", std::string());
        BeginBackgroundActivity(kLspActivity);
    }

    const auto it = activeProgress_.find(key);
    if (it == activeProgress_.end()) {
        return; // report/end for a token that never began (or already ended)
    }

    if (kind == "end") {
        activeProgress_.erase(it);
        EndBackgroundActivity(kLspActivity);
        if (activeProgress_.empty()) {
            // No live progress session left to describe -- drop the stale
            // detail rather than letting it caption a plain request spinner.
            // A no-op if nothing is active at all (the entry is already gone).
            SetBackgroundActivityDetail(kLspActivity, std::string());
        }
        return;
    }

    // "begin" or "report": refresh the detail text. Percentage beats
    // message when both are present -- it's the more glanceable of the two.
    std::string detail = it->second;
    if (value.contains("percentage") && value["percentage"].is_number()) {
        const std::string percent = std::to_string(value["percentage"].get<int>()) + "%";
        detail += detail.empty() ? percent : " (" + percent + ")";
    }
    else if (const std::string message = value.value("message", std::string()); !message.empty()) {
        detail += detail.empty() ? message : ": " + message;
    }
    SetBackgroundActivityDetail(kLspActivity, std::move(detail));
}

void LspManager::RequestHover(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt); // never synced to a server -- nothing to ask
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/hover", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractHoverText(*result));
                        });
}

void LspManager::RequestCompletion(text::Buffer& buffer, std::size_t byteOffset, CompletionCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    // completion-context follow-up: every caller here is a manual/explicit
    // trigger (M-x lsp-complete, or the debounced auto-trigger timer -- see
    // BufferView::RequestCompletionAtPoint), never a specific trigger
    // character this client tracked, so triggerKind is always Invoked (1);
    // omitting "context" entirely left a strict server with no signal at
    // all for whether to apply its own trigger-character-narrower behavior.
    const Json params = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"context", {{"triggerKind", 1}}},
    };
    client->SendRequest("textDocument/completion", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractCompletionItems(*result));
                        });
}

void LspManager::RequestCodeActions(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte, CodeActionCallback callback,
                                    const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const text::ITextStorage& content = buffer.Content();
    const LspPosition start   = BytePositionToLsp(content, rangeStartByte);
    const LspPosition end     = BytePositionToLsp(content, rangeEndByte);

    Json diagnostics = Json::array();
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.endByte <= rangeStartByte || diagnostic.startByte >= rangeEndByte) {
            continue; // doesn't overlap the requested range
        }
        diagnostics.push_back(DiagnosticToLsp(diagnostic, content));
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    const Json        params   = {
        {"textDocument", {{"uri", uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
        {"context", {{"diagnostics", diagnostics}}},
    };
    client->SendRequest("textDocument/codeAction", params,
                        [this, language, callback = std::move(callback), uri](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractCodeActions(*result, uri));
                        });
}

void LspManager::ResolveCodeAction(text::Buffer& buffer, const CodeAction& action, ResolveCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    client->SendRequest("codeAction/resolve", action.raw,
                        [this, language, callback = std::move(callback), uri](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractSingleCodeAction(*result, uri));
                        });
}

void LspManager::ExecuteCommand(text::Buffer& buffer, const std::string& serverKey, const std::string& command, Json arguments,
                                ExecuteCommandCallback callback) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(false);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(false);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"command", command}, {"arguments", std::move(arguments)}};
    client->SendRequest("workspace/executeCommand", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            (void)result; // discarded -- see this method's own doc comment in LspManager.h
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(false);
                                return;
                            }
                            callback(true);
                        });
}

void LspManager::SendLocationRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset,
                                     DefinitionCallback callback, const std::string& serverKey, const Json& extraParams) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    Json              params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    // find-references follow-up: extraParams merges in textDocument/references'
    // own "context" field ({"includeDeclaration": true}), the one place this
    // request's shape diverges from definition/declaration/typeDefinition/
    // implementation -- empty (every other caller) is a no-op merge.
    if (!extraParams.empty()) {
        params.merge_patch(extraParams);
    }
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedLocation> resolved;
                            for (const DefinitionLocation& location : ExtractDefinitionLocations(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(location.uri)) {
                                    resolved.push_back(ResolvedLocation{.path = *path, .position = location.position});
                                }
                                // an unresolvable uri is dropped -- see ResolvedLocation's own doc comment
                            }
                            callback(std::move(resolved));
                        });
}

void LspManager::RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey) {
    SendLocationRequest("textDocument/definition", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestDeclaration(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                    const std::string& serverKey) {
    SendLocationRequest("textDocument/declaration", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestTypeDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                       const std::string& serverKey) {
    SendLocationRequest("textDocument/typeDefinition", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestImplementation(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                       const std::string& serverKey) {
    SendLocationRequest("textDocument/implementation", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestReferences(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                   const std::string& serverKey) {
    SendLocationRequest("textDocument/references", buffer, byteOffset, std::move(callback), serverKey,
                        Json{{"context", {{"includeDeclaration", true}}}});
}

void LspManager::RequestSignatureHelp(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/signatureHelp", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractSignatureHelp(*result));
                        });
}

void LspManager::RequestDocumentHighlight(text::Buffer& buffer, std::size_t byteOffset, DocumentHighlightCallback callback,
                                          const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/documentHighlight", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractDocumentHighlights(*result));
                        });
}

void LspManager::RequestSwitchSourceHeader(text::Buffer& buffer, SwitchHeaderCallback callback) {
    BufferSyncState* state = PrimarySyncState(buffer);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"uri", state->uri}}; // bare TextDocumentIdentifier -- see this method's own header doc comment
    client->SendRequest("textDocument/switchSourceHeader", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result || !result->is_string()) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(UriToPath(result->get<std::string>()));
                        });
}

void LspManager::RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback,
                               const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"newName", newName},
    };
    client->SendRequest("textDocument/rename", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            const RenameResult parsed = ExtractRenameEdits(*result);
                            ResolvedRename     resolved;
                            resolved.touchesUnsupportedForm = parsed.touchesUnsupportedForm;
                            for (const RenameEdit& edit : parsed.edits) {
                                const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
                                if (!path) {
                                    // See ResolvedRenameEdit's own doc comment: an unresolvable
                                    // uri means the whole result can't be safely applied, not
                                    // just this one file's edits.
                                    callback(std::nullopt);
                                    return;
                                }
                                resolved.edits.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
                            }
                            resolved.hasEdit = !resolved.edits.empty();
                            callback(std::move(resolved));
                        });
}

std::optional<std::vector<LspManager::ResolvedRenameEdit>> LspManager::ResolveCodeActionEdits(const CodeAction& action) {
    if (action.touchesUnsupportedForm || !action.hasEdit) {
        return std::nullopt;
    }
    std::vector<ResolvedRenameEdit> resolved;
    resolved.reserve(action.edits.size());
    for (const RenameEdit& edit : action.edits) {
        const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
        if (!path) {
            return std::nullopt; // see ResolvedRenameEdit's own doc comment -- refused wholesale, not partially
        }
        resolved.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
    }
    return resolved;
}

namespace {
    // formatting follow-up: fixed per plan decision -- this codebase has no
    // per-buffer tabs-vs-spaces concept yet, so insertSpaces is hardcoded
    // true; tabSize mirrors the display-only TabWidth() setting (advisory
    // only anyway -- a server commonly falls back to its own config file,
    // e.g. .clang-format/rustfmt.toml, when present).
    Json FormattingOptionsJson() {
        return Json{{"tabSize", editor::TabWidth()}, {"insertSpaces", true}};
    }
} // namespace

void LspManager::RequestFormatting(text::Buffer& buffer, FormattingCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/formatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void LspManager::RequestRangeFormatting(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte,
                                        FormattingCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const text::ITextStorage& content  = buffer.Content();
    const LspPosition start    = BytePositionToLsp(content, rangeStartByte);
    const LspPosition end      = BytePositionToLsp(content, rangeEndByte);
    const std::string         language = state->connectionKey;
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/rangeFormatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void LspManager::RequestOnTypeFormatting(text::Buffer& buffer, std::size_t byteOffset, const std::string& ch, FormattingCallback callback,
                                         const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"ch", ch},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/onTypeFormatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void LspManager::RequestDocumentSymbols(text::Buffer& buffer, SymbolCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    const Json        params   = {{"textDocument", {{"uri", uri}}}};
    client->SendRequest("textDocument/documentSymbol", params,
                        [this, language, uri, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<SymbolResult> resolved;
                            for (const SymbolEntry& entry : ExtractSymbols(*result, uri)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(entry.uri)) {
                                    resolved.push_back(SymbolResult{.name          = entry.name,
                                                                    .containerName = entry.containerName,
                                                                    .kind          = entry.kind,
                                                                    .path          = *path,
                                                                    .position      = entry.position});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void LspManager::RequestWorkspaceSymbols(text::Buffer& buffer, const std::string& query, SymbolCallback callback,
                                         const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"query", query}};
    client->SendRequest("workspace/symbol", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<SymbolResult> resolved;
                            for (const SymbolEntry& entry : ExtractSymbols(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(entry.uri)) {
                                    resolved.push_back(SymbolResult{.name          = entry.name,
                                                                    .containerName = entry.containerName,
                                                                    .kind          = entry.kind,
                                                                    .path          = *path,
                                                                    .position      = entry.position});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

namespace {

    // call/type-hierarchy follow-up: shared by RequestPrepareCallHierarchy/
    // RequestPrepareTypeHierarchy's own resolve-uri-to-path step and
    // RequestIncomingCalls/RequestOutgoingCalls/RequestSupertypes/
    // RequestSubtypes' own -- SymbolResult's own "drop, don't keep with a
    // nonsense path" convention.
    std::vector<LspManager::ResolvedHierarchyItem> ResolveHierarchyItems(std::vector<HierarchyItem> items) {
        std::vector<LspManager::ResolvedHierarchyItem> resolved;
        resolved.reserve(items.size());
        for (HierarchyItem& item : items) {
            if (const std::optional<std::filesystem::path> path = UriToPath(item.uri)) {
                resolved.push_back(LspManager::ResolvedHierarchyItem{.item = std::move(item), .path = *path});
            }
        }
        return resolved;
    }

} // namespace

void LspManager::SendHierarchyPrepareRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset,
                                             HierarchyItemsCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ResolveHierarchyItems(ExtractHierarchyItems(*result)));
                        });
}

void LspManager::RequestPrepareCallHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                             const std::string& serverKey) {
    SendHierarchyPrepareRequest("textDocument/prepareCallHierarchy", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestPrepareTypeHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                             const std::string& serverKey) {
    SendHierarchyPrepareRequest("textDocument/prepareTypeHierarchy", buffer, byteOffset, std::move(callback), serverKey);
}

void LspManager::RequestIncomingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                                      const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest("callHierarchy/incomingCalls", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedHierarchyCall> resolved;
                            for (HierarchyCall& call : ExtractIncomingCalls(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(call.item.uri)) {
                                    resolved.push_back(ResolvedHierarchyCall{
                                        .item      = ResolvedHierarchyItem{.item = std::move(call.item), .path = *path},
                                        .callSites = std::move(call.callSites)});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void LspManager::RequestOutgoingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                                      const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest("callHierarchy/outgoingCalls", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedHierarchyCall> resolved;
                            for (HierarchyCall& call : ExtractOutgoingCalls(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(call.item.uri)) {
                                    resolved.push_back(ResolvedHierarchyCall{
                                        .item      = ResolvedHierarchyItem{.item = std::move(call.item), .path = *path},
                                        .callSites = std::move(call.callSites)});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void LspManager::SendTypeHierarchyStepRequest(const std::string& method, text::Buffer& buffer, const HierarchyItem& item,
                                              HierarchyItemsCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ResolveHierarchyItems(ExtractHierarchyItems(*result)));
                        });
}

void LspManager::RequestSupertypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                                   const std::string& serverKey) {
    SendTypeHierarchyStepRequest("typeHierarchy/supertypes", buffer, item, std::move(callback), serverKey);
}

void LspManager::RequestSubtypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                                 const std::string& serverKey) {
    SendTypeHierarchyStepRequest("typeHierarchy/subtypes", buffer, item, std::move(callback), serverKey);
}

void LspManager::Shutdown() {
    for (const auto& [language, client] : clients_) {
        if (brokerBackedLanguages_.contains(language)) {
            continue; // broker-owned -- must outlive this process, see brokerBackedLanguages_'s own doc comment
        }
        // Mirrors LspBroker::Shutdown()'s own TearDownEntry pattern exactly
        // -- fire both frames, don't wait for the shutdown response (no
        // live EventLoop::Run() left to wait with; see this method's own
        // doc comment in LspManager.h). The callback is never expected to
        // run; passed only because SendRequest requires one.
        //
        // async-write-queue follow-up: PrepareForGracefulShutdown must be
        // called before these two sends, since the actual write is now
        // async (queued, not synchronous) -- it's what makes ~LspClient()
        // (run moments from now, when clients_ itself is destroyed as part
        // of this process's normal teardown) drain the queue instead of
        // applying its ordinary best-effort/no-drain policy.
        client->PrepareForGracefulShutdown();
        client->SendRequest("shutdown", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
        client->SendNotification("exit", Json::object());
    }
}

} // namespace ned::editor::lsp
