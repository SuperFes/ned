#include "LspManager.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>

#include <unistd.h>

#include "Editor/ProjectRoot.h"
#include "LspPosition.h"
#include "LspServerConfig.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::lsp {

namespace {

    // v1: no percent-encoding of special characters in the path -- every
    // path this touches (an open Buffer's own Path(), editor::ProjectRoot())
    // is already a real filesystem path this process itself resolved, not
    // untrusted input, so the common case (no space/unicode-heavy path)
    // round-trips correctly; a path containing characters that need real
    // percent-encoding is a known, documented gap, not silently assumed away.
    std::string PathToUri(const std::filesystem::path& path) {
        return "file://" + path.string();
    }

    std::optional<std::filesystem::path> UriToPath(const std::string& uri) {
        constexpr std::string_view kPrefix = "file://";
        if (uri.rfind(kPrefix, 0) != 0) {
            return std::nullopt;
        }
        return std::filesystem::path(uri.substr(kPrefix.size()));
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

LspManager::LspManager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

LspClient* LspManager::ExistingClientForLanguage(const std::string& language) const {
    const auto it = clients_.find(language);
    return it != clients_.end() ? it->second.get() : nullptr;
}

void LspManager::WireNotificationHandlers(LspClient& client, const std::string& language) {
    client.SetNotificationHandler("textDocument/publishDiagnostics",
                                  [this](const Json& params) { HandlePublishDiagnostics(params); });
    client.SetOnDisconnected([this, language](std::string reason) {
        LogError(language, "server disconnected: " + reason);
        ClientDisconnected(language);
    });
}

LspClient* LspManager::ClientForLanguage(const std::string& language) {
    if (LspClient* existing = ExistingClientForLanguage(language)) {
        return existing;
    }

    const std::optional<std::vector<std::string>> command = LspServerCommand(language);
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
    }

    std::unique_ptr<LspClient> client;
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
        failedCommands_[language] = *command;
        LogError(language, e.what());
        return nullptr;
    }
    WireNotificationHandlers(*client, language);

    // code-actions-resolve follow-up: advertises that this client will call
    // codeAction/resolve for a CodeAction the server sent back without an
    // "edit" -- without this, a resolveProvider server (clangd included)
    // has no signal the client can actually follow up, though in practice
    // most servers offer resolve unconditionally once they declare
    // resolveProvider regardless of what the client advertises here; sent
    // anyway since it's what the spec actually asks a resolve-capable
    // client to declare.
    const Json initializeParams = {
        {"processId", static_cast<std::int64_t>(::getpid())},
        {"rootUri", PathToUri(editor::ProjectRoot())},
        {"capabilities",
         {{"textDocument", {{"codeAction", {{"dataSupport", true}, {"resolveSupport", {{"properties", Json::array({"edit"})}}}}}}}}},
    };
    LspClient* rawClient = client.get();
    rawClient->SendRequest("initialize", initializeParams,
                           [rawClient](std::optional<Json>, std::optional<Json>) { rawClient->SendNotification("initialized", Json::object()); });

    clients_.emplace(language, std::move(client));
    return rawClient;
}

void LspManager::SyncBuffer(text::Buffer& buffer, const std::string& language) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    LspClient* client = ClientForLanguage(language);
    if (!client) {
        return; // nothing configured for this language
    }

    BufferSyncState& state = bufferState_[&buffer];

    if (!state.opened) {
        state.language = language;
        state.uri      = PathToUri(*buffer.Path());
        state.version  = 1;
        client->SendNotification("textDocument/didOpen", {
                                                             {"textDocument",
                                                              {
                                                                  {"uri", state.uri},
                                                                  {"languageId", language},
                                                                  {"version", state.version},
                                                                  {"text", buffer.Text()},
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
                                                           {"contentChanges", Json::array({{{"text", buffer.Text()}}})},
                                                       });
    state.lastSyncedGeneration = buffer.ContentGeneration();
}

LspClient& LspManager::SetClientForTesting(std::string language, std::unique_ptr<LspClient> client) {
    WireNotificationHandlers(*client, language); // same wiring ClientForLanguage's real spawn path applies
    LspClient& ref                = *client;
    clients_[std::move(language)] = std::move(client);
    return ref;
}

void LspManager::ClientDisconnected(const std::string& language) {
    // language may be a reference into the very LspClient (and its
    // OnDisconnected closure) that erase() below destroys -- copy it first
    // so the rest of this function isn't reading freed memory.
    const std::string languageCopy = language;
    clients_.erase(languageCopy);
    for (auto it = bufferState_.begin(); it != bufferState_.end();) {
        if (it->second.language == languageCopy) {
            it = bufferState_.erase(it);
        }
        else {
            ++it;
        }
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

void LspManager::NotifyBufferClosed(text::Buffer& buffer) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end()) {
        return;
    }
    if (it->second.opened) {
        if (LspClient* client = ExistingClientForLanguage(it->second.language)) {
            client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", it->second.uri}}}});
        }
    }
    bufferState_.erase(it);
}

void LspManager::HandlePublishDiagnostics(const Json& params) {
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
                .message   = item.value("message", std::string()),
            });
        }
    }
    buffer->SetDiagnostics(std::move(diagnostics));
}

void LspManager::RequestHover(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback(std::nullopt); // never synced to a server -- nothing to ask
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = it->second.language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", it->second.uri}}},
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

void LspManager::RequestCompletion(text::Buffer& buffer, std::size_t byteOffset, CompletionCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = it->second.language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", it->second.uri}}},
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

void LspManager::RequestCodeActions(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte, CodeActionCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
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

    const std::string language = it->second.language;
    const std::string uri      = it->second.uri;
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

void LspManager::ResolveCodeAction(text::Buffer& buffer, const CodeAction& action, ResolveCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = it->second.language;
    const std::string uri      = it->second.uri;
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

void LspManager::RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback({});
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = it->second.language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", it->second.uri}}},
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

void LspManager::RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback) {
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end() || !it->second.opened) {
        callback(std::nullopt);
        return;
    }
    LspClient* client = ExistingClientForLanguage(it->second.language);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = it->second.language;
    const LspPosition position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", it->second.uri}}},
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
