#include "LspManager.h"

#include <cstdint>
#include <filesystem>

#include <unistd.h>

#include <ftxui/component/screen_interactive.hpp>

#include "Editor/ProjectRoot.h"
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

    // LSP ranges are UTF-16-code-unit offsets within a line, not byte
    // offsets -- a real, easy-to-get-wrong correctness detail, not a
    // simplification this skips. Walks codepoints from lineStart via
    // Rope::CodepointAt (already-tested, shared with Buffer's own
    // tab-aware-positioning code), counting 2 UTF-16 code units for a
    // codepoint outside the Basic Multilingual Plane (a surrogate pair) and
    // 1 for everything else, until utf16Offset code units have been
    // consumed. Bounded by lineEndExclusive so a malformed/out-of-range
    // server-reported offset can't walk off the end of the line.
    std::size_t Utf16OffsetToByteOffset(const text::Rope& content, std::size_t lineStart, std::size_t lineEndExclusive,
                                        std::size_t utf16Offset) {
        std::size_t byteOffset = lineStart;
        std::size_t utf16Count = 0;
        while (byteOffset < lineEndExclusive && utf16Count < utf16Offset) {
            const text::Rope::DecodedCodepoint decoded = content.CodepointAt(byteOffset);
            utf16Count += (decoded.codepoint > 0xFFFF) ? 2 : 1;
            byteOffset += decoded.byteLength;
        }
        return byteOffset;
    }

    std::size_t LineByteRangeEnd(const text::Rope& content, std::size_t line) {
        return (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
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

} // namespace

LspManager::LspManager(text::BufferList& bufferList, ftxui::ScreenInteractive& screen) : bufferList_(bufferList), screen_(screen) {
}

LspClient* LspManager::ExistingClientForLanguage(const std::string& language) const {
    const auto it = clients_.find(language);
    return it != clients_.end() ? it->second.get() : nullptr;
}

LspClient* LspManager::ClientForLanguage(const std::string& language) {
    if (LspClient* existing = ExistingClientForLanguage(language)) {
        return existing;
    }

    const std::optional<std::vector<std::string>> command = LspServerCommand(language);
    if (!command) {
        return nullptr;
    }

    auto client = std::make_unique<LspClient>(*command, screen_);

    client->SetNotificationHandler("textDocument/publishDiagnostics",
                                   [this](const Json& params) { HandlePublishDiagnostics(params); });

    const Json initializeParams = {
        {"processId", static_cast<std::int64_t>(::getpid())},
        {"rootUri", PathToUri(editor::ProjectRoot())},
        {"capabilities", Json::object()},
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

            const std::size_t startLine = start.value("line", static_cast<std::size_t>(0));
            const std::size_t endLine   = end.value("line", static_cast<std::size_t>(0));

            const std::size_t startByte = Utf16OffsetToByteOffset(content, content.LineToByteOffset(startLine), LineByteRangeEnd(content, startLine),
                                                                  start.value("character", static_cast<std::size_t>(0)));
            const std::size_t endByte   = Utf16OffsetToByteOffset(content, content.LineToByteOffset(endLine), LineByteRangeEnd(content, endLine),
                                                                  end.value("character", static_cast<std::size_t>(0)));

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

} // namespace ned::editor::lsp
