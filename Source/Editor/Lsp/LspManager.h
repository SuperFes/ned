//
// LSP client follow-up. Owns every running language-server connection
// (LspClient), keyed by language name -- one server process per language,
// matching editor::ProjectRoot()'s own existing single-root, process-wide
// model (no multi-root workspace support anywhere in this codebase; see this
// subsystem's own ROADMAP.md entry for why that's a documented v1 scope cut,
// not an oversight).
//
// Constructed once, alongside bufferList/killRing/registers, and passed by
// reference the same way -- LSP state is shared editor-wide state, not
// something that belongs to one BufferView/window pane.
//
// Only the *active* buffer is synced (see SyncBuffer's own doc comment) --
// called once per frame from BufferView::Paint(), the same place
// Buffer::ContentGeneration() is already polled for the highlight cache.
//

#ifndef NED_EDITOR_LSP_LSPMANAGER_H
#define NED_EDITOR_LSP_LSPMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>

#include <ftxui/component/screen_interactive.hpp>
#include <nlohmann/json.hpp>

#include "LspClient.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::lsp {

class LspManager {
  public:
    // bufferList (for resolving a publishDiagnostics notification's URI back
    // to an open Buffer, via BufferList::FindByPath) and screen must both
    // outlive this LspManager. See LspClient.h's own header comment for why
    // that's the same requirement its background-thread-marshaling already
    // has.
    LspManager(text::BufferList& bufferList, ftxui::ScreenInteractive& screen);
    ~LspManager() = default;

    LspManager(const LspManager&)            = delete;
    LspManager& operator=(const LspManager&) = delete;

    // Lazily spawns (+ initialize/initialized-handshakes) a server for
    // language if LspServerCommand(language) is configured and none is
    // running yet; a no-op if nothing is configured for language, or if
    // buffer has no associated path (a scratch buffer has no URI to tell a
    // server about). Sends textDocument/didOpen the first time a given
    // buffer is seen, textDocument/didChange (whole-document sync) whenever
    // buffer.ContentGeneration() has advanced since the last sync -- mirrors
    // exactly how BufferView's own highlight cache already decides "has this
    // changed since I last looked."
    //
    // Deliberately not called for every open buffer, only whichever is
    // currently active/visible -- a background buffer won't get live
    // diagnostics until it's viewed again. A real "sync every open buffer"
    // version would need a BufferList-level hook instead of a
    // BufferView::Paint()-level one; deferred as a straightforward widening
    // of this same mechanism, not a design change.
    void SyncBuffer(text::Buffer& buffer, const std::string& language);

    // Called just before buffer is actually closed (BufferView's existing
    // SetOnBufferClosed hook, already wired for window-splitting's own
    // per-pane retargeting -- reused here rather than adding new
    // close-lifecycle plumbing). Sends textDocument/didClose if buffer was
    // ever opened with a server, then forgets its sync state. A no-op if
    // buffer was never synced (e.g. it had no LSP support configured, or no
    // path).
    void NotifyBufferClosed(text::Buffer& buffer);

  private:
    // Returns the already-running client for language, or nullptr if none
    // is running and none is configured -- never spawns one. Used by
    // NotifyBufferClosed, which has no reason to spawn a server just to
    // immediately tell it to close something.
    [[nodiscard]] LspClient* ExistingClientForLanguage(const std::string& language) const;

    // Returns the running (lazily spawning one if needed) client for
    // language, or nullptr if nothing is configured for it.
    LspClient* ClientForLanguage(const std::string& language);

    void HandlePublishDiagnostics(const nlohmann::json& params);

    struct BufferSyncState {
        std::string language;
        std::string uri;
        std::size_t lastSyncedGeneration = 0;
        int         version              = 0;
        bool        opened               = false;
    };

    text::BufferList&         bufferList_;
    ftxui::ScreenInteractive& screen_;

    std::unordered_map<std::string, std::unique_ptr<LspClient>> clients_; // keyed by language
    std::unordered_map<text::Buffer*, BufferSyncState>          bufferState_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPMANAGER_H
