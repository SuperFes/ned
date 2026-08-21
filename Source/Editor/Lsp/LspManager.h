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

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include "UI/EventLoop.h"

#include "LspClient.h"
#include "LspContent.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::lsp {

// error-visibility follow-up. Name of the read-only, live-appended buffer
// every LogError call streams into -- shared between LspManager::LogError
// (which finds-or-creates it) and BufferView's lsp-show-log command
// (Commands.cpp/BufferView.cpp), which must resolve to the exact same
// buffer rather than duplicating the literal.
inline constexpr std::string_view kLspLogBufferName = "*lsp log*";

// The initialize request params sent to every newly spawned server. A free
// function (rather than inline in ClientForLanguage) so tests can assert on
// the advertised capabilities without spawning a real server process --
// SetClientForTesting bypasses the spawn path entirely, which is how the
// missing codeActionLiteralSupport capability below went untested and
// unnoticed: without it a spec-following server (clangd included) must
// return bare Command objects instead of edit-carrying CodeAction literals,
// which made every "fix available" diagnostic unapplyable ("has no edit to
// apply") despite listing fine.
[[nodiscard]] Json BuildInitializeParams(const std::filesystem::path& projectRoot);

class LspManager {
  public:
    // bufferList (for resolving a publishDiagnostics notification's URI back
    // to an open Buffer, via BufferList::FindByPath) and screen must both
    // outlive this LspManager. See LspClient.h's own header comment for why
    // that's the same requirement its background-thread-marshaling already
    // has.
    LspManager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop);
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

    // hover/completion follow-up. Both resolve buffer's server/URI purely
    // from bufferState_ (populated by SyncBuffer's prior didOpen) rather
    // than taking a language param -- a buffer with no sync state yet, or
    // no running client, resolves to "no results" immediately rather than
    // spawning a server just to answer one request. callback always runs on
    // the main thread (see LspClient.h's own threading note) and is simply
    // never invoked if this LspManager -- or the LspClient it was routed
    // through -- is destroyed with the request still in flight, matching
    // LspClient::SendRequest's own documented "abandoned at shutdown"
    // convention.
    using HoverCallback = std::function<void(std::optional<std::string> text)>;
    void RequestHover(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback);

    // CompletionItem itself lives in LspContent.h, not nested here, so its
    // parsing (ExtractCompletionItems) can be unit-tested directly against
    // crafted JSON without needing an LspManager/live client at all.
    using CompletionCallback = std::function<void(std::vector<CompletionItem> items)>;
    void RequestCompletion(text::Buffer& buffer, std::size_t byteOffset, CompletionCallback callback);

    // code-actions follow-up. Same "resolve purely from bufferState_" shape
    // as RequestHover/RequestCompletion. rangeStartByte/rangeEndByte become
    // the request's own "range" (typically the diagnostic covering point, or
    // a zero-length range at point -- the caller's choice); every
    // Buffer::Diagnostic overlapping that range is sent as "context.
    // diagnostics", the same information a real editor's own quick-fix menu
    // would show the server.
    using CodeActionCallback = std::function<void(std::vector<CodeAction> actions)>;
    void RequestCodeActions(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte, CodeActionCallback callback);

    // code-actions-resolve follow-up. Sends codeAction/resolve with
    // action.raw verbatim (the LSP spec requires round-tripping the exact
    // original item back, including any opaque "data" it carried) --
    // called only when action.resolvable is true (see CodeAction's own doc
    // comment in LspContent.h). callback receives the resolved CodeAction
    // (hasEdit true if the server actually filled it in) or nullopt on any
    // failure (buffer never synced, no running client, or an error
    // response).
    using ResolveCallback = std::function<void(std::optional<CodeAction> resolved)>;
    void ResolveCodeAction(text::Buffer& buffer, const CodeAction& action, ResolveCallback callback);

    // go-to-definition follow-up. A DefinitionLocation (LspContent.h) with
    // its uri already resolved to a real filesystem path -- BufferView has
    // no reason to know about URIs at all, the same "LspManager owns the
    // uri<->path boundary" split HandlePublishDiagnostics already
    // established for diagnostics. A location whose uri doesn't parse as a
    // file:// URI (UriToPath returning nullopt) is silently dropped rather
    // than surfaced as a partial/malformed result -- matches
    // ExtractDefinitionLocations' own "skip a malformed entry" convention.
    struct ResolvedLocation {
        std::filesystem::path path;
        LspPosition           position;
    };
    using DefinitionCallback = std::function<void(std::vector<ResolvedLocation> locations)>;
    // Sent for lsp-goto-definition. Same "resolve purely from bufferState_"
    // shape as RequestHover/RequestCompletion/RequestCodeActions.
    void RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback);

    // rename follow-up. One URI's worth of edits, uri already resolved to a
    // real filesystem path -- mirrors ResolvedLocation's own reasoning
    // above. A RenameEdit (LspContent.h) whose uri doesn't resolve is
    // dropped from the result entirely (not just that one entry silently
    // missing edits) -- ApplyRename (BufferView.cpp) needs every touched
    // file to be genuinely applicable before it applies any of them (see
    // that method's own doc comment for why a rename is refused wholesale
    // rather than partially applied), so a path this layer already
    // couldn't resolve must not be silently treated as "resolved, zero
    // edits" by the caller.
    struct ResolvedRenameEdit {
        std::filesystem::path          path;
        std::vector<WorkspaceTextEdit> edits;
    };
    struct ResolvedRename {
        std::vector<ResolvedRenameEdit> edits;
        bool                            touchesUnsupportedForm = false; // see RenameResult's own doc comment in LspContent.h
        bool                            hasEdit                = false;
    };
    using RenameCallback = std::function<void(std::optional<ResolvedRename> result)>;
    // Sent for lsp-rename. nullopt on any failure (buffer never synced, no
    // running client, or an error response) -- mirrors ResolveCallback's
    // own nullopt-on-failure convention.
    void RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback);

    // Public primarily for tests -- mirrors LspClient::DispatchFrame's own
    // "public primarily for tests" precedent (see that method's doc comment
    // in LspClient.h). Registers an already-constructed LspClient for
    // language directly, bypassing ClientForLanguage's normal subprocess-
    // spawn path, so a test can drive a fake server through the same
    // Transport-based LspClient constructor (a raw pipe pair, no real
    // subprocess) LspClientTest.cpp already uses, then call the returned
    // reference's own DispatchFrame directly to deliver a canned response --
    // the same "no running ScreenInteractive::Loop() needed" reasoning
    // DispatchFrame's own doc comment explains. Production code
    // (ClientForLanguage) never calls this. Replaces any existing client
    // already registered for language.
    LspClient& SetClientForTesting(std::string language, std::unique_ptr<LspClient> client);

    // error-visibility follow-up. Finds (or, on the very first call in this
    // process's lifetime, creates) kLspLogBufferName and appends one
    // timestamped, language-tagged line to its end -- "[HH:MM:SS] language:
    // message". Never throws. Every call site is already established to run
    // on the main thread (see this subsystem's own threading doc comments);
    // this method does not itself Post -- every real call site already runs
    // from inside a Post-drained callback (LspClient's onDisconnected_/
    // ResponseCallback, both only ever invoked that way), and
    // ned::ui::EventLoop::Run already repaints unconditionally once that
    // callback returns (see EventLoop.cpp's own needsRepaint comment), so
    // there is nothing left here to explicitly request. Public (not just
    // called internally) so a test can call it directly without going
    // through a real spawn/disconnect/error-response path.
    void LogError(std::string_view language, std::string_view message);

    // True once LogError has been called and no BufferView has yet
    // acknowledged it via AcknowledgeLogEntry -- a single, process-wide
    // "something happened" flag, not a per-error/per-pane unread count
    // (deliberately simple, see BufferView::Paint's own use of this).
    [[nodiscard]] bool HasUnseenLogEntry() const;
    void               AcknowledgeLogEntry();

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

    // workDoneProgress-support follow-up. Handles a "$/progress"
    // notification: begin/end drive the shared "LSP" BackgroundActivity
    // count, begin/report refresh its detail text ("indexing (45%)") -- how
    // server-side busy state (clangd's background indexing, mainly) reaches
    // the mode-line spinner with something more informative than a pulse.
    void HandleProgress(const std::string& language, const nlohmann::json& params);

    // Shared by ClientForLanguage's real spawn path and
    // SetClientForTesting's injection path, so an injected test client
    // behaves identically to a real one -- was previously inlined only into
    // ClientForLanguage, which silently left an injected client with no
    // publishDiagnostics routing. language (error-visibility follow-up) is
    // threaded through to the disconnect handler wired here, since a client
    // has no notion of its own language name -- both call sites already
    // have it in scope.
    void WireNotificationHandlers(LspClient& client, const std::string& language);

    // error-visibility follow-up. Called (on the main thread, via
    // LspClient::SetOnDisconnected's own Post-marshaled callback) the
    // moment a running server's connection ends for any reason. Erases the
    // client (a crash/disconnect is transient, unlike a permanently-missing
    // binary -- worth respawning on the next SyncBuffer, unlike
    // ClientForLanguage's own failedCommands_ latch below) and every
    // bufferState_ entry for language, so SyncBuffer's own `!state.opened`
    // branch re-fires a fresh didOpen against the respawned client instead
    // of silently believing a server that no longer exists already knows
    // about these buffers.
    void ClientDisconnected(const std::string& language);

    struct BufferSyncState {
        std::string language;
        std::string uri;
        std::size_t lastSyncedGeneration = 0;
        int         version              = 0;
        bool        opened               = false;
    };

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<LspClient>> clients_; // keyed by language
    std::unordered_map<text::Buffer*, BufferSyncState>          bufferState_;

    // error-visibility follow-up. A process-lifetime latch, keyed by
    // language, on the exact argv that last failed to spawn -- lets
    // ClientForLanguage stop retrying (and re-logging) a known-bad command
    // every single frame, while still trying again once the user
    // reconfigures LspServerCommand(language) to something different. No
    // auto-retry/backoff beyond that: a binary that becomes available on
    // $PATH mid-session, with no reconfiguration, is not retried -- a known,
    // documented v1 limitation, matching this subsystem's existing "static
    // config, no auto-retry" model (see LspServerConfig.h).
    std::unordered_map<std::string, std::vector<std::string>> failedCommands_;

    // workDoneProgress-support follow-up. Every progress session currently
    // between its "begin" and "end", keyed by language + '\x1f' + the
    // token's own JSON dump (tokens are string-or-integer per spec; dump()
    // normalizes both); the value is the begin's title, reused when a
    // "report" refreshes the detail text without repeating it. Guards the
    // Begin/End pairing against a confused server (duplicate begin, end
    // without begin), and lets ClientDisconnected End whatever a dying
    // server left open so the spinner can't run forever.
    std::unordered_map<std::string, std::string> activeProgress_;

    bool hasUnseenLogEntry_ = false; // see HasUnseenLogEntry/AcknowledgeLogEntry
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPMANAGER_H
