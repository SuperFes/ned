#include "LspManager.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>

#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSettings.h"
#include "LspBrokerConnect.h"
#include "LspPosition.h"
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

    Json DiagnosticToLsp(const text::Buffer::Diagnostic& diagnostic, const text::Rope& content) {
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
           {{"completion", {{"completionItem", {{"snippetSupport", true}}}}},
            {"codeAction",
             {{"codeActionLiteralSupport",
               {{"codeActionKind",
                 {{"valueSet", Json::array({"", "quickfix", "refactor", "refactor.extract", "refactor.inline", "refactor.rewrite",
                                            "source", "source.organizeImports", "source.fixAll"})}}}}},
              {"dataSupport", true},
              {"resolveSupport", {{"properties", Json::array({"edit"})}}}}}}},
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

void LspManager::WireNotificationHandlers(LspClient& client, const std::string& language, const Json& workspaceConfiguration) {
    client.SetNotificationHandler("textDocument/publishDiagnostics",
                                  [this, language](const Json& params) { HandlePublishDiagnostics(params, language); });
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
    client.SetNotificationHandler("$/progress", [this, language](const Json& params) { HandleProgress(language, params); });
    client.SetOnDisconnected([this, language](std::string reason) {
        LogError(language, "server disconnected: " + reason);
        disconnectDetail_[language] = reason;
        ClientDisconnected(language);
    });
}

LspClient* LspManager::ClientForLanguage(const std::string& language) {
    if (LspClient* existing = ExistingClientForLanguage(language)) {
        return existing;
    }

    // prose-checking follow-up: the one place kProseLanguageKey is treated
    // differently from a real language -- its command comes from
    // ProseCheckerCommand()'s auto-detect/override/enabled-toggle
    // resolution instead of the plain per-language table.
    const std::optional<std::vector<std::string>> command =
        (language == kProseLanguageKey) ? ProseCheckerCommand() : LspServerCommand(language);
    if (!command) {
        return nullptr;
    }

    // error-visibility follow-up: don't retry (or re-log) a command that
    // already failed to spawn on a previous frame -- SyncBuffer calls this
    // every Paint() for the active buffer, and a real subprocess-spawn
    // failure is not transient. Erasing on a *different* command lets a
    // user's own SetLspServerCommand reconfiguration get one fresh attempt.
    if (const auto failed = failedCommands_.find(language); failed != failedCommands_.end()) {
        if (failed->second == *command) {
            return nullptr;
        }
        failedCommands_.erase(failed);
        spawnFailureDetail_.erase(language);
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
    if (const auto lastDisconnect = lastDisconnectAt_.find(language); lastDisconnect != lastDisconnectAt_.end()) {
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
    // spawned process.
    std::unique_ptr<LspClient> client =
        TryConnectToBroker(editor::ProjectRoot(), language, *command, eventLoop_, brokerSocketPathOverrideForTesting_);
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
            failedCommands_[language]     = *command;
            spawnFailureDetail_[language] = e.what();
            LogError(language, e.what());
            return nullptr;
        }
    }
    const editor::ProjectSettings projectSettings = editor::LoadProjectSettings(editor::ProjectRoot());
    WireNotificationHandlers(*client, language, projectSettings.lspWorkspaceConfiguration);

    LspClient* rawClient = client.get();
    // project-settings-lsp-init-options follow-up: initializationOptions
    // covers servers that only read config at handshake time;
    // workspace/didChangeConfiguration (sent right after "initialized",
    // only when lspWorkspaceConfiguration is non-empty) covers the "push"
    // model some servers expect instead -- see ProjectSettings.h's own doc
    // comment on lspWorkspaceConfiguration for why both exist side by side.
    rawClient->SendRequest(
        "initialize",
        BuildInitializeParams(editor::ProjectRoot(), editor::LspInitializationOptionsForLanguage(projectSettings, language)),
        [this, rawClient, language, workspaceConfiguration = projectSettings.lspWorkspaceConfiguration](
            std::optional<Json>, std::optional<Json> error) {
            // hang-on-timed-out-initialize follow-up: ExpireStaleRequests
            // invokes this with (nullopt, a synthesized timeout error) if
            // the server never responds -- previously this branch was
            // unreachable in practice because both parameters were ignored,
            // so a timed-out handshake still opened the queued-notification
            // gate and flushed everything queued behind it (including a
            // full-document textDocument/didChange) into a server that had
            // already proven unresponsive, wedging the write.
            if (error) {
                LogError(language, "initialize failed: " + ExtractErrorMessage(*error));
                disconnectDetail_[language] = ExtractErrorMessage(*error);
                ClientDisconnected(language);
                return;
            }
            rawClient->SendNotification("initialized", Json::object());
            if (!workspaceConfiguration.empty()) {
                rawClient->SendNotification("workspace/didChangeConfiguration", Json{{"settings", workspaceConfiguration}});
            }
        });

    clients_.emplace(language, std::move(client));
    // mode-line-lsp-status-round-2 follow-up: a successful (re)spawn
    // resolves any prior disconnect -- StatusForLanguage should report
    // Running now, not a stale Disconnected from before this attempt.
    disconnectedLanguages_.erase(language);
    disconnectDetail_.erase(language);
    lastDisconnectAt_.erase(language); // respawn-debounce follow-up -- a stale cooldown must not outlive a real respawn
    return rawClient;
}

void LspManager::SyncBuffer(text::Buffer& buffer, const std::string& language) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    primaryServerKey_[&buffer] = language;                          // see PrimarySyncState's own doc comment
    SyncToServer(buffer, language, language);                       // primary language server
    SyncToServer(buffer, std::string(kProseLanguageKey), language); // prose checker, independent of the above
}

void LspManager::SyncEmbeddedDocuments(text::Buffer& buffer, const std::vector<EmbeddedDocumentSync>& documents) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    std::unordered_set<std::string> desiredKeys;
    for (const EmbeddedDocumentSync& document : documents) {
        desiredKeys.insert(document.language);
        embeddedOwnedRanges_[&buffer][document.language] = document.ownedRanges;
        SyncTextToServer(buffer, document.language, document.language, document.documentText);
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
                    if (LspClient* client = ExistingClientForLanguage(key)) {
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

void LspManager::SyncToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId) {
    SyncTextToServer(buffer, serverKey, languageId, buffer.Text());
}

void LspManager::SyncTextToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                                  const std::string& documentText) {
    LspClient* client = ClientForLanguage(serverKey);
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
        state.language = serverKey;
        state.uri      = PathToUri(*buffer.Path());
        state.version  = 1;
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
        return;
    }

    if (buffer.ContentGeneration() == state.lastSyncedGeneration) {
        return; // nothing changed since the last sync
    }

    ++state.version;
    client->SendNotification("textDocument/didChange", {
                                                           {"textDocument", {{"uri", state.uri}, {"version", state.version}}},
                                                           {"contentChanges", Json::array({{{"text", documentText}}})},
                                                       });
    state.lastSyncedGeneration = buffer.ContentGeneration();
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
                                           const Json& workspaceConfiguration) {
    WireNotificationHandlers(*client, language, workspaceConfiguration); // same wiring ClientForLanguage's real spawn path applies
    disconnectedLanguages_.erase(language);                              // an injected client is "running," same as a real successful spawn
    disconnectDetail_.erase(language);
    lastDisconnectAt_.erase(language); // respawn-debounce follow-up -- ditto
    LspClient& ref                = *client;
    clients_[std::move(language)] = std::move(client);
    return ref;
}

void LspManager::ClientDisconnected(const std::string& language) {
    // language may be a reference into the very LspClient (and its
    // OnDisconnected closure) this function destroys below -- copy it first
    // so the rest of this function isn't reading freed memory.
    const std::string languageCopy = language;
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
    clients_.erase(languageCopy);
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

void LspManager::NotifyBufferClosed(text::Buffer& buffer) {
    const auto it = bufferState_.find(&buffer);
    if (it != bufferState_.end()) {
        // prose-checking follow-up: buffer may have up to two sync states
        // (primary + prose) -- notify every server it was ever opened with,
        // not just one.
        for (const auto& perServer : it->second) {
            const BufferSyncState& state = perServer.second;
            if (state.opened) {
                if (LspClient* client = ExistingClientForLanguage(state.language)) {
                    client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", state.uri}}}});
                }
            }
        }
        bufferState_.erase(it);
    }
    diagnosticsBySource_.erase(&buffer);
    diagnosticsDebounceTimers_.erase(&buffer); // cancels a pending timer before it can fire against a dead buffer
    primaryServerKey_.erase(&buffer);
    embeddedServerKeys_.erase(&buffer);
    embeddedOwnedRanges_.erase(&buffer);
}

void LspManager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    for (const auto& entry : clients_) {
        entry.second->ExpireStaleRequests(maxAge);
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
        const text::Rope& content = buffer->Content();
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
    // embedded-language-documents follow-up: an embedded server (one with an
    // owned-ranges record) only ever legitimately reports within its own
    // owned regions -- a padded/blanked region should tokenize as inert
    // whitespace, so a diagnostic starting outside every owned range is
    // either a rare parser edge case at a padding boundary or a server
    // ignoring content it wasn't asked about. Dropped defensively rather
    // than surfaced against the wrong language's chrome. No effect on the
    // primary language or kProseLanguageKey, neither of which ever has an
    // owned-ranges entry (they own the whole buffer).
    if (const auto ownedIt = embeddedOwnedRanges_.find(buffer); ownedIt != embeddedOwnedRanges_.end()) {
        if (const auto rangeIt = ownedIt->second.find(language); rangeIt != ownedIt->second.end()) {
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
    }

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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->language;
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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback({});
        return;
    }

    const text::Rope& content = buffer.Content();
    const LspPosition start   = BytePositionToLsp(content, rangeStartByte);
    const LspPosition end     = BytePositionToLsp(content, rangeEndByte);

    Json diagnostics = Json::array();
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.endByte <= rangeStartByte || diagnostic.startByte >= rangeEndByte) {
            continue; // doesn't overlap the requested range
        }
        diagnostics.push_back(DiagnosticToLsp(diagnostic, content));
    }

    const std::string language = state->language;
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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->language;
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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback(false);
        return;
    }

    const std::string language = state->language;
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

void LspManager::RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/definition", params,
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

void LspManager::RequestSwitchSourceHeader(text::Buffer& buffer, SwitchHeaderCallback callback) {
    BufferSyncState* state = PrimarySyncState(buffer);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->language;
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
    LspClient* client = ExistingClientForLanguage(state->language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->language;
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

} // namespace ned::editor::lsp
