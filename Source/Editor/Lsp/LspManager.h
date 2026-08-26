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
// The active buffer is synced once per frame from BufferView::Paint(), the
// same place Buffer::ContentGeneration() is already polled for the
// highlight cache (see SyncBuffer's own doc comment). LSP-deliberate-cuts
// follow-up: every *other* open buffer is now also synced, on a periodic
// background tick instead of per-frame -- see LspBackgroundSync.h's
// SyncBackgroundBuffers, wired from WindowManager's existing auto-save
// timer. Both paths funnel through this same SyncBuffer method.
//

#ifndef NED_EDITOR_LSP_LSPMANAGER_H
#define NED_EDITOR_LSP_LSPMANAGER_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "Editor/ProcessTimeouts.h"
#include "UI/EventLoop.h"

#include "LspClient.h"
#include "LspContent.h"
// prose-checking follow-up: diagnosticsBySource_ stores
// std::vector<text::Buffer::Diagnostic> directly, which needs Buffer's full
// definition to name that nested type -- a forward declaration is no longer
// enough, unlike the rest of this header's Buffer/BufferList usage (all
// by-reference parameters).
#include "Text/Buffer.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor::lsp {

// error-visibility follow-up. Name of the read-only, live-appended buffer
// every LogError call streams into -- shared between LspManager::LogError
// (which finds-or-creates it) and BufferView's lsp-show-log command
// (Commands.cpp/BufferView.cpp), which must resolve to the exact same
// buffer rather than duplicating the literal.
inline constexpr std::string_view kLspLogBufferName = "*lsp log*";

// prose-checking follow-up. A reserved language key, never a real value
// LanguageKeyForMode(mode) can produce (that always comes from stripping a
// Mode's own "-mode" suffix -- no bundled or dynamic mode is named "prose"),
// used to slot the prose-checker connection into every one of this class's
// otherwise per-real-language maps (clients_, bufferState_,
// failedCommands_, disconnectedLanguages_, ...) as if it were just another
// language. SyncBuffer syncs this key's server independently of whatever
// buffer.Path()'s real primary language server is doing -- see its own doc
// comment -- and it is only ever asked for diagnostics (didOpen/didChange/
// publishDiagnostics), never hover/completion/code actions/definition/
// rename.
inline constexpr std::string_view kProseLanguageKey = "prose";

// The initialize request params sent to every newly spawned server. A free
// function (rather than inline in ClientForLanguage) so tests can assert on
// the advertised capabilities without spawning a real server process --
// SetClientForTesting bypasses the spawn path entirely, which is how the
// missing codeActionLiteralSupport capability below went untested and
// unnoticed: without it a spec-following server (clangd included) must
// return bare Command objects instead of edit-carrying CodeAction literals,
// which made every "fix available" diagnostic unapplyable ("has no edit to
// apply") despite listing fine.
//
// initializationOptions (project-settings-lsp-init-options follow-up) is
// merged in verbatim as the request's own "initializationOptions" field when
// non-empty -- Editor/ProjectSettings.h's per-language
// lspInitializationOptionsByLanguage is what ClientForLanguage actually
// passes here; left as Json::object() (the default) for a language with
// nothing configured, in which case the field is omitted entirely rather
// than sent as an empty object.
[[nodiscard]] Json BuildInitializeParams(const std::filesystem::path& projectRoot,
                                         const Json&                  initializationOptions = Json::object());

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
    // prose-checking follow-up: also syncs buffer to the prose-checker
    // connection (kProseLanguageKey), completely independently -- whether
    // the primary language server above is configured, running, or missing
    // has no bearing on whether prose checking runs, and vice versa. Both
    // publish diagnostics that get merged, not wholesale-replaced; see
    // HandlePublishDiagnostics/PushMergedDiagnostics.
    //
    // Called per-frame for the pane-active buffer (BufferView::Paint()) and,
    // separately, per-tick for every other open buffer
    // (LspBackgroundSync.h's SyncBackgroundBuffers) -- either caller passes
    // whatever language it already resolved, this method has no opinion
    // about how that resolution happened. The per-buffer ContentGeneration()
    // gate above is what makes calling this twice for the same buffer (once
    // from each path) cheap: the second call is a no-op.
    void SyncBuffer(text::Buffer& buffer, const std::string& language);

    // embedded-language-documents follow-up. One embedded language's
    // synthesized virtual document, ready to sync -- Editor/EmbeddedDocuments.h's
    // EmbeddedDocument, translated at the one BufferView.cpp call site into
    // this small, Mode-agnostic struct so LspManager keeps its existing
    // "plain language-key strings only" character rather than gaining a
    // dependency on Mode.h/tree-sitter. documentText is what actually gets
    // sent as didOpen/didChange's own "text" -- see EmbeddedDocument's own
    // doc comment for why its byte length and per-line UTF-16 widths are
    // guaranteed identical to the host buffer's real text, which is what
    // lets every position computed elsewhere in this file
    // (BytePositionToLsp(buffer.Content(), ...)) stay correct against this
    // server with zero remapping. ownedRanges (host-buffer byte coordinates)
    // is used only for dropping a diagnostic whose start falls in a padded,
    // non-owned region -- see HandlePublishDiagnostics.
    struct EmbeddedDocumentSync {
        std::string                                      language;
        std::string                                      documentText;
        std::vector<std::pair<std::size_t, std::size_t>> ownedRanges;
    };

    // Syncs each entry of documents to its own server (didOpen/didChange,
    // same generation-gated logic SyncBuffer's primary/prose sync already
    // uses -- see SyncTextToServer), keyed by its own language. Any server
    // key this method previously synced for buffer but that documents no
    // longer contains (e.g. buffer's only <script> block was deleted) is
    // torn down: textDocument/didClose sent, its sync state and owned-range
    // record erased, and its diagnostics slice dropped (re-pushing merged
    // diagnostics so they don't linger). A no-op if buffer has no associated
    // path, same guard as SyncBuffer.
    void SyncEmbeddedDocuments(text::Buffer& buffer, const std::vector<EmbeddedDocumentSync>& documents);

    // embedded-language-documents follow-up. Every server key currently
    // synced for buffer -- its primary language, kProseLanguageKey if that's
    // synced too, and any embedded keys from SyncEmbeddedDocuments above.
    // Unordered; ModeLine sorts/labels as it renders. Empty if buffer has
    // never been synced at all.
    [[nodiscard]] std::vector<std::string> ActiveServerKeysForBuffer(const text::Buffer& buffer) const;

    // Called just before buffer is actually closed (BufferView's existing
    // SetOnBufferClosed hook, already wired for window-splitting's own
    // per-pane retargeting -- reused here rather than adding new
    // close-lifecycle plumbing). Sends textDocument/didClose if buffer was
    // ever opened with a server, then forgets its sync state. A no-op if
    // buffer was never synced (e.g. it had no LSP support configured, or no
    // path).
    void NotifyBufferClosed(text::Buffer& buffer);

    // subprocess-hang-protection follow-up. Sweeps every running client's
    // own ExpireStaleRequests -- meant to be wired into a periodic
    // background tick (WindowManager::StartAutoSaveTimer), not called
    // per-frame. See LspClient::ExpireStaleRequests's own doc comment for
    // what "stale" means and why. maxAge is forwarded as-is, defaulted the
    // same way, purely so tests can shorten it.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

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
    //
    // embedded-language-documents follow-up: serverKey routes to a specific
    // connection (bufferState_'s own per-server key), same meaning as
    // RequestCodeActions' own serverKey -- empty (the default) keeps the
    // original PrimarySyncState-only behavior. BufferView passes an embedded
    // language key here when point sits inside that language's region (e.g.
    // an HTML <script> block), so hover/completion inside it hits the real
    // javascript server, not html's.
    using HoverCallback = std::function<void(std::optional<std::string> text)>;
    void RequestHover(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey = {});

    // CompletionItem itself lives in LspContent.h, not nested here, so its
    // parsing (ExtractCompletionItems) can be unit-tested directly against
    // crafted JSON without needing an LspManager/live client at all.
    // serverKey: see RequestHover's own doc comment above.
    using CompletionCallback = std::function<void(std::vector<CompletionItem> items)>;
    void RequestCompletion(text::Buffer& buffer, std::size_t byteOffset, CompletionCallback callback, const std::string& serverKey = {});

    // code-actions follow-up. Same "resolve purely from bufferState_" shape
    // as RequestHover/RequestCompletion. rangeStartByte/rangeEndByte become
    // the request's own "range" (typically the diagnostic covering point, or
    // a zero-length range at point -- the caller's choice); every
    // Buffer::Diagnostic overlapping that range is sent as "context.
    // diagnostics", the same information a real editor's own quick-fix menu
    // would show the server.
    //
    // executeCommand follow-up: serverKey routes to a specific connection
    // (bufferState_'s own per-server key -- see SyncToServer) instead of
    // always the primary language server; empty (the default) keeps the
    // original PrimarySyncState-only behavior. BufferView passes
    // kProseLanguageKey here when point sits on a Prose-origin diagnostic,
    // so "add to dictionary"/"ignore" actions from the prose checker
    // connection are reachable through this same request.
    using CodeActionCallback = std::function<void(std::vector<CodeAction> actions)>;
    void RequestCodeActions(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte, CodeActionCallback callback,
                            const std::string& serverKey = {});

    // code-actions-resolve follow-up. Sends codeAction/resolve with
    // action.raw verbatim (the LSP spec requires round-tripping the exact
    // original item back, including any opaque "data" it carried) --
    // called only when action.resolvable is true (see CodeAction's own doc
    // comment in LspContent.h). callback receives the resolved CodeAction
    // (hasEdit true if the server actually filled it in) or nullopt on any
    // failure (buffer never synced, no running client, or an error
    // response). serverKey: see RequestCodeActions's own doc comment above.
    using ResolveCallback = std::function<void(std::optional<CodeAction> resolved)>;
    void ResolveCodeAction(text::Buffer& buffer, const CodeAction& action, ResolveCallback callback, const std::string& serverKey = {});

    // executeCommand follow-up. Sends workspace/executeCommand for a
    // CodeAction::CodeActionCommand extracted from a prior code-action
    // response -- what actually runs a bare-Command (or edit-and-command)
    // quick fix, harper-ls's "add to dictionary"/"ignore" among them. No
    // check against the server's own executeCommandProvider capability
    // (this file never gates any other request on the server's advertised
    // capabilities either -- just sends it and handles the error response
    // like everything else here). callback receives false on any failure
    // (buffer never synced, no running client, or an error response), true
    // otherwise; the request's own "result" is discarded -- a server that
    // needs to push an edit back in response is expected to do so via its
    // own workspace/applyEdit request, which this client doesn't handle yet
    // (out of scope: every command this feature targets is confirmed to
    // persist server-side with no such round trip -- see ROADMAP.md history).
    using ExecuteCommandCallback = std::function<void(bool ok)>;
    void ExecuteCommand(text::Buffer& buffer, const std::string& serverKey, const std::string& command, Json arguments,
                        ExecuteCommandCallback callback);

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
    // shape as RequestHover/RequestCompletion/RequestCodeActions. serverKey:
    // see RequestHover's own doc comment above.
    void RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey = {});

    // header-source-switching follow-up. clangd's own custom LSP extension
    // (not in the base spec -- no textDocument/definition-style position
    // needed, just the document itself) for jumping between a C/C++ header
    // and its implementation file. Sent as {"uri": ...} directly, not
    // wrapped in a "textDocument" object -- that's the extension's actual
    // wire shape (a bare TextDocumentIdentifier as params), unlike every
    // other request in this file. Response is a single URI string, or null
    // if the server has no counterpart to offer -- callback receives
    // nullopt in that case (server said no, server doesn't implement the
    // extension at all -- most servers besides clangd -- no client running,
    // or an error), never a distinction the caller needs: BufferView::
    // SwitchHeaderSource falls back to Editor/HeaderSource.h's filesystem
    // heuristic uniformly on nullopt.
    using SwitchHeaderCallback = std::function<void(std::optional<std::filesystem::path> path)>;
    void RequestSwitchSourceHeader(text::Buffer& buffer, SwitchHeaderCallback callback);

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
    // own nullopt-on-failure convention. serverKey: see RequestHover's own
    // doc comment above.
    void RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback,
                       const std::string& serverKey = {});

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
    // already registered for language. workspaceConfiguration mirrors what
    // ClientForLanguage would have loaded from ProjectSettings -- lets a test
    // exercise the workspace/configuration handler's real section-resolution
    // logic without a real .ned/settings.json on disk.
    LspClient& SetClientForTesting(std::string language, std::unique_ptr<LspClient> client,
                                   const Json& workspaceConfiguration = Json::object());

    // LspManagerTest-broker-hermeticity follow-up: routes ClientForLanguage's
    // real spawn path's TryConnectToBroker call at a caller-chosen path
    // instead of the real BrokerSocketPath() -- lets a test that wants a
    // deterministic, synchronous spawn failure point at a path nothing is
    // (or ever will be) listening on, immune to a real broker daemon a
    // previous test run or another `ned` process happened to leave running
    // on this machine's real BrokerSocketPath(). Passing a nonexistent path
    // also skips TryBecomeBrokerSpawner (see LspBrokerConnect.cpp), so this
    // never forks a real daemon process either. Production code
    // (LspManager's own constructor) never calls this.
    void SetBrokerSocketPathOverrideForTesting(std::filesystem::path path) {
        brokerSocketPathOverrideForTesting_ = std::move(path);
    }

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

    // mode-line-lsp-status-round-2 follow-up: a connection-status enum
    // beyond plain running/idle, so the mode line can also tell "the last
    // spawn attempt for this language failed" and "was running, then the
    // server disconnected/crashed" apart from "nothing configured at all" --
    // previously ClientDisconnected erasing the client left those three
    // cases indistinguishable (see this subsystem's own ROADMAP.md entry).
    // Deliberately says nothing about in-flight-request activity -- that's
    // reported separately via the shared BackgroundActivity "LSP" entry
    // (see kLspActivityName); ModeLine draws that on top of, and with
    // priority over, whatever this reports.
    enum class LspStatus {
        NotConfigured, // nothing registered for this language, or a client was never attempted
        Running,       // a client is currently spawned and connected
        SpawnFailed,   // the last spawn attempt for the currently-configured command failed
        Disconnected,  // was running, then the server exited/crashed -- not yet respawned
    };

    // Never spawns a client -- mirrors ExistingClientForLanguage's own "just
    // look, don't act" shape, but public (ModeLine is the intended caller).
    [[nodiscard]] LspStatus StatusForLanguage(const std::string& language) const;

    // mode-line-lsp-status-round-3 follow-up: the detail text behind a
    // SpawnFailed/Disconnected glyph -- the spawn exception's e.what() for
    // the former, the disconnect reason LspClient::SetOnDisconnected
    // reported for the latter. "" when StatusForLanguage doesn't report the
    // matching state (nothing latched yet, or a later event already cleared
    // it) -- ModeLine is expected to call whichever one matches
    // StatusForLanguage's current result.
    [[nodiscard]] std::string SpawnFailureDetail(const std::string& language) const;
    [[nodiscard]] std::string DisconnectReason(const std::string& language) const;

  private:
    // Returns the already-running client for language, or nullptr if none
    // is running and none is configured -- never spawns one. Used by
    // NotifyBufferClosed, which has no reason to spawn a server just to
    // immediately tell it to close something.
    [[nodiscard]] LspClient* ExistingClientForLanguage(const std::string& language) const;

    // Returns the running (lazily spawning one if needed) client for
    // language, or nullptr if nothing is configured for it. language ==
    // kProseLanguageKey resolves its command via ProseCheckerCommand()
    // instead of LspServerCommand(language) -- the only place that
    // distinction is made; everything else here treats it like any other
    // language key.
    LspClient* ClientForLanguage(const std::string& language);

    struct BufferSyncState {
        std::string language;
        std::string uri;
        std::size_t lastSyncedGeneration = 0;
        int         version              = 0;
        bool        opened               = false;
    };

    // prose-checking follow-up: SyncBuffer's actual body, generalized so it
    // can independently target either the primary language server or the
    // prose-checker connection. serverKey selects which entry of
    // clients_/bufferState_ this call operates on (buffer's real language,
    // or kProseLanguageKey); languageId is always the buffer's real
    // language, sent as textDocument/didOpen's own "languageId" regardless
    // of which server this call is talking to -- harper-ls needs the real
    // language to know how to extract comments/strings from the document.
    // A thin wrapper around SyncTextToServer passing buffer.Text() --
    // embedded-language-documents follow-up: SyncEmbeddedDocuments calls
    // SyncTextToServer directly with a virtual document's own padded text
    // instead.
    void SyncToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId);

    // embedded-language-documents follow-up: SyncToServer's actual body,
    // generalized so the text synced isn't always buffer.Text() -- an
    // embedded virtual document has its own padded text, same byte length
    // and line/UTF-16 structure as the host buffer (see EmbeddedDocumentSync's
    // own doc comment) but not the same content.
    void SyncTextToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                          const std::string& documentText);

    // hover/completion/code-actions/definition/rename follow-up: resolves
    // the *primary* language's BufferSyncState for buffer via
    // primaryServerKey_ (stamped by SyncBuffer, which already knows
    // buffer's true host language) -- a direct lookup, not a guess. Before
    // embedded-language-documents this scanned bufferState_[&buffer] for
    // "whichever entry isn't kProseLanguageKey," which was only ever
    // correct while at most one non-prose entry existed; once an embedded
    // key (e.g. "javascript") joins the same map alongside the host
    // language (e.g. "html"), that guess becomes ambiguous. nullptr if
    // buffer has never been synced via SyncBuffer at all.
    [[nodiscard]] BufferSyncState* PrimarySyncState(text::Buffer& buffer);

    // executeCommand follow-up. Generalizes PrimarySyncState for a caller
    // that can name which server it wants: empty serverKey delegates to
    // PrimarySyncState unchanged, a non-empty one looks up that exact
    // bufferState_[&buffer] entry (e.g. kProseLanguageKey) instead of
    // whichever isn't kProseLanguageKey. RequestCodeActions/ResolveCodeAction/
    // ExecuteCommand all resolve through this rather than PrimarySyncState
    // directly, so any of them can be routed to the prose connection.
    [[nodiscard]] BufferSyncState* ResolveSyncState(text::Buffer& buffer, const std::string& serverKey);

    // prose-checking follow-up: flattens every source language's current
    // diagnostics slice for buffer (diagnosticsBySource_[&buffer]) into one
    // vector and pushes it via buffer.SetDiagnostics -- the actual merge
    // point that replaces the old "last publisher wins" wholesale replace.
    // Called after any publish, and after a source's slice is dropped
    // (disconnect) so stale diagnostics from a dead server don't linger.
    void PushMergedDiagnostics(text::Buffer& buffer);

    void HandlePublishDiagnostics(const nlohmann::json& params, const std::string& language);

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
    // have it in scope. workspaceConfiguration (project-settings-lsp-init-
    // options follow-up) is what the workspace/configuration request handler
    // resolves each requested section against -- see ProjectSettings.h's own
    // doc comment on lspWorkspaceConfiguration for why this is a flat,
    // language-agnostic tree rather than keyed by language like
    // initializationOptions is.
    void WireNotificationHandlers(LspClient& client, const std::string& language,
                                  const Json& workspaceConfiguration = Json::object());

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

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    // LspManagerTest-broker-hermeticity follow-up: test-only override for
    // the broker socket path ClientForLanguage's TryConnectToBroker call
    // resolves against -- nullopt (the real default) resolves the real
    // BrokerSocketPath(). Without this, a test asserting a spawn failure
    // surfaces synchronously is at the mercy of whatever broker daemon (if
    // any) happens to already be listening on the machine running the test
    // -- see SetBrokerSocketPathOverrideForTesting's own doc comment.
    std::optional<std::filesystem::path> brokerSocketPathOverrideForTesting_;

    std::unordered_map<std::string, std::unique_ptr<LspClient>> clients_; // keyed by language

    // prose-checking follow-up: outer key is the buffer, inner key is the
    // server ("cpp", kProseLanguageKey, ...) -- a buffer now has up to two
    // concurrent sync states (its primary language server and the prose
    // checker), each tracking its own didOpen/version/lastSyncedGeneration
    // independently. Was a flat unordered_map<Buffer*, BufferSyncState>
    // before this could ever be true.
    std::unordered_map<text::Buffer*, std::unordered_map<std::string, BufferSyncState>> bufferState_;

    // prose-checking follow-up: per-buffer, per-source-language diagnostics
    // -- what makes merging possible instead of each server's own
    // publishDiagnostics wholesale-replacing whatever the other server just
    // reported. PushMergedDiagnostics flattens this into the vector that
    // actually reaches buffer.SetDiagnostics.
    std::unordered_map<text::Buffer*, std::unordered_map<std::string, std::vector<text::Buffer::Diagnostic>>> diagnosticsBySource_;

    // embedded-language-documents follow-up: buffer's own true host
    // language, stamped by SyncBuffer -- what PrimarySyncState looks up
    // directly instead of guessing "whichever bufferState_ entry isn't
    // kProseLanguageKey," which stopped being unambiguous the moment an
    // embedded key could also live in that same map. Erased in
    // NotifyBufferClosed.
    std::unordered_map<text::Buffer*, std::string> primaryServerKey_;

    // embedded-language-documents follow-up: every server key
    // SyncEmbeddedDocuments currently manages for buffer -- what lets a
    // later call notice a key has disappeared (its only region was deleted)
    // and tear it down, rather than leaving a phantom document and stale
    // diagnostics behind forever. Erased in NotifyBufferClosed.
    std::unordered_map<text::Buffer*, std::unordered_set<std::string>> embeddedServerKeys_;

    // embedded-language-documents follow-up: per-buffer, per-embedded-key
    // owned byte ranges (host-buffer coordinates) -- consulted by
    // HandlePublishDiagnostics to drop a diagnostic whose start falls
    // outside every owned range for that key (a padded/blanked region
    // shouldn't be surfacing real diagnostics). Absent for the primary
    // language and kProseLanguageKey, which own the whole buffer. Erased in
    // NotifyBufferClosed.
    std::unordered_map<text::Buffer*, std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>>>
        embeddedOwnedRanges_;

    // diagnostics-debounce follow-up: one debounce timer per buffer with a
    // pending publish -- HandlePublishDiagnostics (re)arms the buffer's
    // entry on every publish instead of pushing to buffer.SetDiagnostics
    // immediately, so a burst of publishes from rapid typing collapses into
    // a single application once LspDiagnosticsDebounceMs() passes with no
    // further publish for that buffer. NotifyBufferClosed erases (and so
    // cancels) a buffer's entry before it can fire against a Buffer* that
    // may no longer be valid.
    std::unordered_map<text::Buffer*, ned::ui::DeadlineTimer> diagnosticsDebounceTimers_;

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

    // mode-line-lsp-status-round-3 follow-up: the exception message from the
    // spawn attempt that populated failedCommands_[language] -- cleared
    // wherever failedCommands_ itself is cleared, so the two never drift
    // apart.
    std::unordered_map<std::string, std::string> spawnFailureDetail_;

    // respawn-debounce follow-up: the steady-clock time of a language's most
    // recent disconnect, keyed by language. ClientForLanguage refuses to
    // even attempt a respawn until kRespawnCooldown has passed since this --
    // breathing room for a single stumble (a slow-starting server racing its
    // own config file, a transient resource hiccup) to actually recover,
    // requested alongside the crash-loop guard below rather than as a
    // replacement for it: this is per-attempt spacing, the crash-loop guard
    // is the total-attempts cap. Cleared the moment ClientForLanguage
    // successfully spawns a fresh client for the language again, same
    // "a stale latch must not outlive a real respawn" precedent
    // disconnectedLanguages_ already follows.
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastDisconnectAt_;

    // crash-loop-respawn-guard follow-up: a server that fails immediately on
    // every launch (confirmed live -- a misconfigured phpantom_lsp produced
    // thousands of respawn attempts within about one second, since
    // ClientDisconnected's own "a crash/disconnect is transient, worth
    // respawning on the next SyncBuffer" policy has no rate limit at all)
    // used to have nothing standing between one disconnect and the very
    // next frame's respawn attempt. Keyed by language: the steady-clock time
    // of the first disconnect in the current rapid-disconnect burst, and how
    // many disconnects have landed in it so far. ClientDisconnected resets
    // the burst once kCrashLoopWindow has passed since it started; once
    // kCrashLoopThreshold disconnects land inside one window, ClientDisconnected
    // latches failedCommands_ itself (ClientForLanguage's own pre-existing
    // "known-bad command, stop retrying until reconfigured" guard), the same
    // outcome a hard spawn failure already gets -- a crash loop is
    // functionally the same "this command doesn't work" signal, just
    // discovered one handshake later.
    std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, int>> disconnectBurst_;

    // mode-line-lsp-status-round-2 follow-up: languages whose client most
    // recently ended via ClientDisconnected rather than a spawn failure --
    // what StatusForLanguage's Disconnected case reads. Cleared the moment
    // ClientForLanguage successfully spawns a fresh client for the language
    // again (see that method's own comment) -- a stale disconnect latch
    // must not outlive a real respawn.
    std::unordered_set<std::string> disconnectedLanguages_;

    // mode-line-lsp-status-round-3 follow-up: the reason string
    // LspClient::SetOnDisconnected reported, cleared wherever
    // disconnectedLanguages_ itself is cleared.
    std::unordered_map<std::string, std::string> disconnectDetail_;

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
