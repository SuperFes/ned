//
// LSP client follow-up. Owns every running language-server connection
// (LspClient), keyed by language name -- one server process per language, per
// resolved root (LSP multi-root follow-up: LspRootResolver.h's
// ResolveLspRoot picks a buffer's own root, which may be more specific than
// editor::ProjectRoot() when the buffer's language has configured root
// markers -- e.g. the nearest package.json for a "javascript" buffer inside
// a monorepo -- and falls back to editor::ProjectRoot() unchanged otherwise).
// The common case -- no markers configured, or none matched -- resolves to
// exactly editor::ProjectRoot() for every buffer, so clients_ stays keyed by
// a bare language string exactly as before (see ConnectionKey's own doc
// comment for why); only a genuinely more-specific resolved root earns its
// own distinct connection. A handful of connection-scoped caches
// (semanticTokensLegend_, onTypeFormattingTriggers_,
// pullDiagnosticsUnsupported_, inlayHintsUnsupported_, codeLensUnsupported_,
// activeProgress_, and the failedCommands_/disconnected* status-latch group)
// stay keyed by the plain language string regardless of root -- a documented
// v1 cut: two *simultaneously running* servers for the same language against
// two different roots share these caches (one's legend/status can shadow the
// other's), which is cosmetic at worst -- every actual request still routes
// through the correct per-root connection via BufferSyncState::connectionKey,
// see ExistingClientForLanguage's own callers. See ROADMAP.md for the
// follow-up that would close this remaining gap.
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
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "Editor/Mode.h"
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
    // LSP multi-root follow-up: resolves (and caches -- see
    // ResolveCachedRoot) buffer's own LSP root via LspRootResolver.h's
    // ResolveLspRoot(*buffer.Path(), language), then uses that same resolved
    // root for both this sync and the prose-checker sync below, and (if
    // SyncEmbeddedDocuments is called for this buffer afterward) every
    // embedded-language sync too -- one buffer always has exactly one LSP
    // root, never a different one per server key.
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
    // gate in SyncToServer is what makes calling this twice for the same
    // buffer (once from each path) cheap: the second call is a no-op
    // *before* buffer.Text() is ever built, not after.
    //
    // huge-file-lsp-gate follow-up: a buffer.Content().IsHuge() buffer never
    // reaches SyncToServer at all -- neither the primary language server nor
    // the prose checker has a sane way to consume a multi-GB didOpen, and
    // buffer.Text() would fully materialize the document just to build one.
    // Logged once per buffer (LogError, surfaced the normal *lsp log*/echo-
    // area way) rather than silently, so missing diagnostics/completion on
    // a huge file has a visible explanation.
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

    // declaration/typeDefinition/implementation follow-up: three more
    // location-shaped requests, identical in every respect to
    // RequestDefinition above except the wire method -- the LSP spec gives
    // textDocument/declaration, /typeDefinition, and /implementation the
    // exact same Location | Location[] | LocationLink[] response shape as
    // /definition, which is why DefinitionLocation/ExtractDefinitionLocations
    // were already written to be reused here (see DefinitionLocation's own
    // doc comment in LspContent.h). All three share RequestDefinition's
    // private SendLocationRequest body.
    void RequestDeclaration(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey = {});
    void RequestTypeDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                               const std::string& serverKey = {});
    void RequestImplementation(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                               const std::string& serverKey = {});

    // find-references follow-up: same ResolvedLocation/DefinitionCallback
    // shape as the four above -- textDocument/references returns a bare
    // Location[] (never LocationLink[]), which ExtractDefinitionLocations
    // already parses uniformly alongside the other two shapes. Always sends
    // "context": {"includeDeclaration": true} -- the declaration/definition
    // site itself is a legitimate "reference" a caller building a full usage
    // list wants included, matching real Emacs xref-find-references/eglot's
    // own default.
    void RequestReferences(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey = {});

    // signature-help follow-up. Same "resolve purely from bufferState_"
    // shape as RequestHover, and the exact same callback shape too --
    // ExtractSignatureHelp (LspContent.h) already reduces the response to
    // one status-line-ready string, the same "already the caller's whole
    // answer" contract ExtractHoverText follows. serverKey: see
    // RequestHover's own doc comment above.
    void RequestSignatureHelp(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey = {});

    // documentHighlight follow-up. Same "resolve purely from bufferState_,
    // single buffer, no URI resolution" shape as RequestSignatureHelp above
    // -- a documentHighlight response is always scoped to the requesting
    // document, unlike RequestReferences' cross-file Location[]. serverKey:
    // see RequestHover's own doc comment above.
    using DocumentHighlightCallback = std::function<void(std::vector<DocumentHighlight> highlights)>;
    void RequestDocumentHighlight(text::Buffer& buffer, std::size_t byteOffset, DocumentHighlightCallback callback,
                                  const std::string& serverKey = {});

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

    // edit-application-gaps follow-up. DocumentChangeOp (LspContent.h) with
    // its uri/oldUri resolved to real filesystem paths -- the same "resolve
    // once, refuse wholesale on any failure" contract ResolvedRenameEdit
    // already establishes. path is the EditFile/CreateFile/DeleteFile target,
    // or RenameFile's destination (newUri); oldPath is set for RenameFile
    // only.
    struct ResolvedDocumentChangeOp {
        DocumentChangeOp::Kind         kind = DocumentChangeOp::Kind::EditFile;
        std::filesystem::path          path;
        std::filesystem::path          oldPath; // RenameFile only
        std::vector<WorkspaceTextEdit> edits;   // EditFile only
        bool                           overwrite         = false;
        bool                           ignoreIfExists    = false;
        bool                           ignoreIfNotExists = false;
    };

    struct ResolvedRename {
        std::vector<ResolvedRenameEdit>       edits;                          // "changes" form; empty when documentChangeOps below is populated instead
        std::vector<ResolvedDocumentChangeOp> documentChangeOps;              // "documentChanges" form, in order
        bool                                  touchesUnsupportedForm = false; // see RenameResult's own doc comment in LspContent.h
        bool                                  hasEdit                = false;
    };
    using RenameCallback = std::function<void(std::optional<ResolvedRename> result)>;

    // project-undo follow-up: resolves a CodeAction's own edits (LspContent.h's
    // CodeAction::edits, one RenameEdit per touched URI) to real filesystem
    // paths -- the same URI resolution just above does for ResolvedRename,
    // exposed here as a pure, synchronous, no-I/O conversion since (unlike a
    // real request) nothing needs to round-trip a server: the edits already
    // arrived with the action itself. Returns nullopt if
    // action.touchesUnsupportedForm, the action has no edit at all, or any
    // one URI fails to resolve -- mirrors ResolvedRename's own
    // resolve-every-file-or-none contract (BufferView::ApplyCodeAction needs
    // every touched file genuinely applicable before applying any of them,
    // never a partial fix across only some of them).
    [[nodiscard]] static std::optional<std::vector<ResolvedRenameEdit>> ResolveCodeActionEdits(const CodeAction& action);

    // edit-application-gaps follow-up: resolves a parsed documentChangeOps
    // list's own uri/oldUri fields to real filesystem paths -- same
    // all-or-nothing contract ResolveCodeActionEdits establishes for the
    // simpler "changes" form. Shared by ApplyCodeAction's own
    // action.documentChangeOps, RequestRename's response handling, and the
    // workspace/applyEdit server-request handler below -- all three consume
    // a parsed documentChangeOps list the identical way. Returns nullopt if
    // ops is empty or any one uri/oldUri fails to resolve.
    [[nodiscard]] static std::optional<std::vector<ResolvedDocumentChangeOp>>
    ResolveDocumentChangeOps(const std::vector<DocumentChangeOp>& ops);

    // Sent for lsp-rename. nullopt on any failure (buffer never synced, no
    // running client, or an error response) -- mirrors ResolveCallback's
    // own nullopt-on-failure convention. serverKey: see RequestHover's own
    // doc comment above.
    void RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback,
                       const std::string& serverKey = {});

    // edit-application-gaps follow-up. workspace/applyEdit is the one
    // direction every request above doesn't cover: a server *pushing* a
    // WorkspaceEdit at the client unprompted (e.g. in response to a
    // workspace/executeCommand the client itself just sent), which per spec
    // the client must answer with {applied: bool}. Parsing/resolving the
    // pushed edit stays here (LspClient::RequestHandler is synchronous-only,
    // and the parse/resolve step already is), but actually applying it needs
    // real buffer mutation plus project-undo bookkeeping that only
    // Source/UI/'s BufferView owns -- applyEditHandler, set via
    // SetApplyEditHandler, is the same "Editor/ stays UI-free, hand the UI
    // layer a callback" seam BufferList::SetOnFileOpened/SetAsyncFileOpener
    // already establish for Text/ -> UI/ wiring. nullopt/unset means "no
    // handler wired up yet" -- reported to the server as applied:false, the
    // same honest failure this client would report for any other
    // unsupported capability, rather than silently claiming success. label
    // is the server's own optional human-readable description of the edit
    // (WorkspaceEdit's sibling "label" field on the request, not on the edit
    // itself), passed through for the handler's own status message.
    using ApplyEditHandler = std::function<bool(const ResolvedRename& edit, const std::string& label)>;
    void SetApplyEditHandler(ApplyEditHandler handler) {
        applyEditHandler_ = std::move(handler);
    }

    // formatting follow-up. Same callback shape for both -- nullopt on any
    // failure (buffer never synced, no running client, or an error
    // response), a (possibly empty) edit list otherwise. Unlike rename, a
    // formatting response only ever targets the requesting document itself,
    // so no per-URI resolution is needed -- structurally closer to
    // RequestSignatureHelp than to RequestRename. tabSize/insertSpaces are
    // fixed (TabWidth(), true) rather than caller-supplied: this codebase
    // has no per-buffer tabs-vs-spaces concept to source them from yet.
    using FormattingCallback = std::function<void(std::optional<std::vector<WorkspaceTextEdit>> edits)>;
    void RequestFormatting(text::Buffer& buffer, FormattingCallback callback, const std::string& serverKey = {});

    // rangeFormatting follow-up: same rangeStartByte/rangeEndByte pair as
    // RequestCodeActions above. Not wired into any command yet (save-buffer/
    // format-buffer are whole-buffer operations already) -- added for API
    // symmetry and a future "format region"/"format on paste" command.
    void RequestRangeFormatting(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte,
                                FormattingCallback callback, const std::string& serverKey = {});

    // on-type-formatting follow-up. ch is the just-typed trigger character
    // (the caller is expected to have already matched it against
    // OnTypeFormattingTriggersFor(serverKey) -- see that method's own doc
    // comment for why this class doesn't do that matching itself);
    // byteOffset is where it landed, converted to the request's own
    // "position". Reuses FormattingCallback's exact shape/nullopt-on-failure
    // convention, and the same fixed FormattingOptions RequestFormatting
    // already builds.
    void RequestOnTypeFormatting(text::Buffer& buffer, std::size_t byteOffset, const std::string& ch, FormattingCallback callback,
                                 const std::string& serverKey = {});

    // symbol-search follow-up. A SymbolEntry (LspContent.h) with its own uri
    // already resolved to a real filesystem path -- mirrors ResolvedLocation's
    // own reasoning (LspManager owns the uri<->path boundary, callers never
    // see a raw uri). A result whose uri doesn't resolve is dropped, not
    // kept with a nonsense path -- matches ResolvedLocation's own "skip a
    // malformed entry" convention rather than SendLocationRequest's stricter
    // rename-only "refuse the whole batch" one (a symbol picker losing one
    // unresolvable entry out of many is a minor degrade, not a correctness
    // risk the way silently applying half a rename would be).
    struct SymbolResult {
        std::string           name;
        std::string           containerName;
        int                   kind = 0;
        std::filesystem::path path;
        LspPosition           position;
    };
    using SymbolCallback = std::function<void(std::vector<SymbolResult> symbols)>;

    // Sent for lsp-goto-symbol. Same "resolve purely from bufferState_"
    // shape as RequestHover/RequestDefinition -- no position parameter
    // (textDocument/documentSymbol takes only the document itself).
    // serverKey: see RequestHover's own doc comment above.
    void RequestDocumentSymbols(text::Buffer& buffer, SymbolCallback callback, const std::string& serverKey = {});

    // Sent for lsp-workspace-symbol. workspace/symbol has no textDocument/
    // position of its own at all -- buffer is only used to resolve which
    // running server to ask (ResolveSyncState, same as every other request
    // here), matching this client's existing "no multi-root workspace,
    // one server per language" scope cut: a query is sent to exactly one
    // server, never fanned out and merged across every language server the
    // project happens to have running. serverKey: see RequestHover's own
    // doc comment above.
    void RequestWorkspaceSymbols(text::Buffer& buffer, const std::string& query, SymbolCallback callback,
                                 const std::string& serverKey = {});

    // call/type-hierarchy follow-up. A HierarchyItem (LspContent.h) with its
    // own uri resolved to a real filesystem path -- SymbolResult's own
    // "LspManager owns the uri<->path boundary" reasoning applies verbatim.
    // item.raw is kept as-is (untouched by path resolution) since it's what
    // a later incomingCalls/outgoingCalls/supertypes/subtypes request
    // replays back verbatim as its own "item" parameter -- the server-given
    // uri inside it must stay exactly as sent, this codebase's own resolved
    // path is purely an addition for BufferView's own use. Dropped, not kept
    // with a nonsense path, when the uri doesn't resolve -- SymbolResult's
    // own "one bad entry, minor degrade" precedent, not
    // SendLocationRequest's stricter rename-only "refuse the whole batch."
    struct ResolvedHierarchyItem {
        HierarchyItem          item;
        std::filesystem::path  path;
    };
    using HierarchyItemsCallback = std::function<void(std::vector<ResolvedHierarchyItem> items)>;

    // Sent for lsp-call-hierarchy-incoming/-outgoing: the first of the two
    // requests those commands issue (textDocument/prepareCallHierarchy),
    // resolving point to however many CallHierarchyItems the server thinks
    // sit there -- almost always 0 or 1, but the spec allows more (an
    // overload set, a macro expansion) so every item is kept and, per real
    // eglot/lsp-mode precedent, a caller with more than one should let the
    // user pick rather than guessing. The *second* request (incoming/
    // outgoing calls for whichever item the tree session is expanding) is
    // RequestIncomingCalls/RequestOutgoingCalls below, not this method --
    // prepare only ever runs once, at the point the session starts.
    void RequestPrepareCallHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                     const std::string& serverKey = {});

    // Sent for lsp-type-hierarchy-supertypes/-subtypes' own prepare step --
    // textDocument/prepareTypeHierarchy, otherwise identical to
    // RequestPrepareCallHierarchy above (TypeHierarchyItem is wire-identical
    // to CallHierarchyItem, see HierarchyItem's own doc comment).
    void RequestPrepareTypeHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                     const std::string& serverKey = {});

    // call/type-hierarchy follow-up. One expand step of a call-hierarchy
    // tree session: item must be a ResolvedHierarchyItem this same
    // LspManager already handed back (from RequestPrepareCallHierarchy or a
    // prior RequestIncomingCalls/RequestOutgoingCalls call on the same
    // serverKey) -- its raw field is replayed verbatim as the request's own
    // "item" parameter, which is how a server correlates this call back to
    // the symbol it resolved earlier (via its own opaque "data", carried
    // inside raw). buffer/serverKey resolve which running connection to
    // send to -- callers should pass the same buffer/serverKey the
    // originating prepare request used, so this lands on the exact same
    // server session raw's "data" (if any) was minted by.
    //
    // callSites' file context differs by direction, per spec: for
    // incomingCalls, each entry's callSites are positions within the
    // *returned* item's own file (the caller calls the requested symbol at
    // these points in the caller's own source); for outgoingCalls, they're
    // positions within the *originally requested* item's file instead (the
    // file the call site itself lives in), not the returned item's --
    // BufferView must track which file that is itself rather than reading
    // it off the returned item. Not resolved to a jump target in this v1 --
    // ResolvedHierarchyItem::path/position (the item's own definition site)
    // is the primary jump target a tree row offers; callSites is exposed
    // only for an annotation like "(3 call sites)", real navigation to a
    // specific call site is a documented future refinement.
    struct ResolvedHierarchyCall {
        ResolvedHierarchyItem     item;
        std::vector<LspPosition> callSites;
    };
    using HierarchyCallsCallback = std::function<void(std::vector<ResolvedHierarchyCall> calls)>;

    // callHierarchy/incomingCalls -- "who calls item".
    void RequestIncomingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                              const std::string& serverKey = {});

    // callHierarchy/outgoingCalls -- "what item calls".
    void RequestOutgoingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                              const std::string& serverKey = {});

    // typeHierarchy/supertypes -- "what item extends/implements". Reuses
    // HierarchyItemsCallback (a bare HierarchyItem[] response, no call-site
    // wrapper) since supertypes/subtypes have no fromRanges concept at all,
    // unlike incoming/outgoingCalls.
    void RequestSupertypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                           const std::string& serverKey = {});

    // typeHierarchy/subtypes -- "what extends/implements item".
    void RequestSubtypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                         const std::string& serverKey = {});

    // graceful-lsp-shutdown follow-up. Called once, synchronously, from
    // main.cpp's post-Run() shutdown sequence, before this LspManager (and
    // every LspClient it owns) is destroyed by ordinary local-variable
    // teardown. For every *directly-spawned* running client (never a
    // broker-backed one -- see brokerBackedLanguages_'s own doc comment: a
    // broker-owned server is shared with other ned processes and the broker
    // daemon itself, and must outlive this one), sends a real LSP
    // "shutdown" request immediately followed by "exit", mirroring
    // LspBroker::Shutdown()'s own TearDownEntry pattern exactly --
    // including that v1 deliberately does not wait for the shutdown
    // response before also sending exit (see that method's own doc comment
    // for why: no live EventLoop::Run() is pumping Post-marshaled callbacks
    // at this point in shutdown, so there is nothing to wait *with* --
    // Transport::WriteFrame's own bounded stall timeout is what keeps
    // sending these two frames from ever hanging). The actual bounded wait
    // for the server to have genuinely exited comes from the same place it
    // always has: ChildProcess::~ChildProcess()'s close-stdin/poll/SIGKILL-
    // escalation sequence, which fires the instant this LspManager's own
    // clients_ map is destroyed right after this method returns -- this
    // method only adds the courtesy protocol goodbye in front of that
    // already-bounded, already-battle-tested teardown, it doesn't replace
    // or extend it.
    void Shutdown();

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
    // logic without a real .ned/settings.json on disk. graceful-lsp-shutdown
    // follow-up: brokerBacked lets a test register a client as broker-owned
    // (Shutdown()'s own skip case) without needing a real broker connection
    // -- defaults to false, so every existing call site (an ordinary
    // direct-spawn stand-in) keeps compiling and behaving unchanged.
    //
    // LSP multi-root follow-up: connectionKeyOverride lets a test register a
    // client under a distinct connection identity from its own serverKey
    // (language) -- needed to simulate "two roots, same language, two
    // separate connections" without a real filesystem walk. nullopt (the
    // default, every pre-existing call site) registers clients_[language]
    // exactly as before -- ClientForLanguage's own real spawn path collapses
    // to that same bare-language key whenever a buffer's resolved root is
    // editor::ProjectRoot() (see ConnectionKey), which is what every
    // existing SyncBuffer-driven test still resolves to.
    LspClient& SetClientForTesting(std::string language, std::unique_ptr<LspClient> client,
                                   const Json& workspaceConfiguration = Json::object(), bool brokerBacked = false,
                                   std::optional<std::string> connectionKeyOverride = std::nullopt);

    // on-type-formatting follow-up. SetClientForTesting above bypasses the
    // real spawn/initialize/initialized handshake entirely, so a test
    // driving it never populates onTypeFormattingTriggers_/
    // semanticTokensLegend_ the way a real handshake does (see the
    // `initialize` response lambda in LspManager.cpp) -- this is the same
    // "test-only, production code never calls this" injection point for
    // that one piece of state, mirroring SetClientForTesting's own
    // rationale exactly.
    void SetOnTypeFormattingTriggersForTesting(std::string language, OnTypeFormattingTriggers triggers) {
        onTypeFormattingTriggers_[std::move(language)] = std::move(triggers);
    }

    // semanticTokens follow-up: same test-only injection point as
    // SetOnTypeFormattingTriggersForTesting just above, for the sibling
    // piece of `initialize`-response state.
    void SetSemanticTokensLegendForTesting(std::string language, SemanticTokensLegend legend) {
        semanticTokensLegend_[std::move(language)] = std::move(legend);
    }

    // incremental-sync follow-up: same test-only injection point as
    // SetSemanticTokensLegendForTesting just above, for the sibling piece of
    // `initialize`-response state -- a test wanting to exercise the
    // Incremental sync path must seed this directly, since
    // SetClientForTesting bypasses the real handshake that would otherwise
    // populate it.
    void SetTextDocumentSyncKindForTesting(std::string language, TextDocumentSyncKind kind) {
        textDocumentSyncKind_[std::move(language)] = kind;
    }

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

    // semantic-tokens/on-type-formatting follow-up. Captured from
    // serverKey's own `initialize` response the moment it arrives (the one
    // place this class previously discarded that response entirely) --
    // nullopt if serverKey has never finished a handshake, or its server
    // doesn't advertise the corresponding provider. See LspContent.h's
    // ExtractSemanticTokensLegend/ExtractOnTypeFormattingTriggers for why
    // these two are stored as data a request needs to function, not as a
    // general capability-gating check (this class deliberately has none --
    // see ExecuteCommand's own doc comment above).
    [[nodiscard]] std::optional<SemanticTokensLegend>     SemanticTokensLegendFor(const std::string& serverKey) const;
    [[nodiscard]] std::optional<OnTypeFormattingTriggers> OnTypeFormattingTriggersFor(const std::string& serverKey) const;

    // incremental-sync follow-up: unlike the two accessors above, this
    // returns a plain TextDocumentSyncKind rather than an optional -- every
    // caller wants the already-defaulted value (Full when serverKey never
    // advertised, or unparseably advertised, a sync kind -- see
    // ExtractTextDocumentSyncKind's own doc comment for why Full and not the
    // spec's technical None default), and there's no legitimate case for a
    // caller to distinguish "unset" from "explicitly Full."
    [[nodiscard]] TextDocumentSyncKind TextDocumentSyncKindFor(const std::string& serverKey) const;

    // semanticTokens follow-up. Called once per Paint() for the active
    // buffer (BufferView.cpp, alongside the existing SyncBuffer call, not
    // from inside SyncBuffer/SyncToServer itself -- see this method's own
    // definition comment for why keeping it out of LspManager's core sync
    // path matters, a real lesson learned fixing pull-diagnostics' own test
    // regression above). No-ops when semantic highlighting is disabled
    // (LspSemanticHighlightingEnabled), when serverKey never advertised a
    // legend (SemanticTokensLegendFor -- the response would be
    // undecodable), or when buffer's content hasn't changed since this was
    // last called for it (a cursor-blink/scroll-only repaint must not
    // resend the request every single frame). Result lands in
    // SemanticTokenSpans(buffer)/SemanticTokensGeneration(buffer) below,
    // not a caller-supplied callback -- BufferView's highlight cache is
    // this method's only real consumer, and it polls the generation counter
    // the same way it already polls Buffer::ContentGeneration(), rather
    // than being pushed to.
    void RequestSemanticTokensFull(text::Buffer& buffer, const std::string& serverKey);

    // The most recently applied full-document semantic-token spans for
    // buffer, already resolved to byte offsets/SyntaxClass -- empty if
    // never requested, not yet answered, or the server has no legend at
    // all. captureId is always kNoCapture on every span (see
    // SyntaxClassForSemanticTokenType's own doc comment for why this
    // reuses SyntaxClass rather than adding a new styling axis).
    [[nodiscard]] const std::vector<editor::HighlightSpan>& SemanticTokenSpans(const text::Buffer& buffer) const;

    // Bumped every time SemanticTokenSpans(buffer) actually changes --
    // BufferView's own highlight-cache key polls this alongside
    // Buffer::ContentGeneration()/editor::CaptureClassGeneration(), the
    // same "cheap did-it-change counter" shape ContentGeneration() itself
    // already is. 0 if buffer has never had a semantic-tokens response
    // applied.
    [[nodiscard]] std::size_t SemanticTokensGeneration(const text::Buffer& buffer) const;

    // inlayHint follow-up. One applied hint, already resolved to a byte
    // offset -- label is the plain, already-flattened display text (see
    // lsp::InlayHint's own doc comment in LspContent.h for how a
    // richer InlayHintLabelPart[] response collapses to this).
    struct ResolvedInlayHint {
        std::size_t byteOffset;
        std::string label;
    };

    // Called once per Paint() for the active buffer (BufferView.cpp,
    // alongside SyncBuffer/RequestSemanticTokensFull), scoped to
    // [viewportStartByte, viewportEndByte) -- unlike semanticTokens'
    // whole-document request, the LSP spec's own "range" param on this
    // method exists specifically so a client only asks for what's visible,
    // since a large file can carry far more hints than are ever on screen
    // at once. No legend/data precondition to gate on (inlay hints aren't
    // index-encoded) -- this is a plain recurring background request, so
    // (matching RequestPullDiagnostics'/RequestSemanticTokensFull's own
    // precedent) it no-ops when disabled (LspInlayHintsEnabled), when the
    // exact same [buffer, viewport range, content generation] triple was
    // already requested (a cursor-blink/scroll-into-the-same-view repaint
    // must not resend), and latches a real error response into
    // inlayHintsUnsupported_ so a non-implementing server is never asked
    // again for this connection's lifetime.
    void RequestInlayHints(text::Buffer& buffer, std::size_t viewportStartByte, std::size_t viewportEndByte,
                           const std::string& serverKey);

    // The most recently applied inlay hints for buffer, sorted by
    // byteOffset -- empty if never requested, not yet answered, or the
    // server has no hints for the last-requested range at all.
    [[nodiscard]] const std::vector<ResolvedInlayHint>& InlayHintSpans(const text::Buffer& buffer) const;

    // codeLens follow-up. One applied lens, already resolved to byte
    // offsets -- see LspContent.h's CodeLens for what each field means;
    // raw is kept for a later codeLens/resolve if hasCommand is false.
    struct ResolvedCodeLens {
        std::size_t startByte;
        std::size_t endByte;
        std::string title;
        std::string commandName;
        Json        commandArguments;
        bool        hasCommand;
        Json        raw;
    };

    // Called once per Paint() for the active buffer, alongside
    // RequestSemanticTokensFull/RequestInlayHints -- whole-document scope
    // (codeLens has no "range" param, unlike inlayHint), so the dedup gate
    // is just buffer.ContentGeneration(), the same shape
    // RequestSemanticTokensFull's own gate is. No legend/data precondition
    // (codeLens ranges aren't index-encoded) -- a plain recurring
    // background request, so (matching RequestPullDiagnostics'/
    // RequestInlayHints' own precedent) it no-ops when disabled
    // (LspCodeLensEnabled) and latches a real error response into
    // codeLensUnsupported_ so a non-implementing server is never asked
    // again for this connection's lifetime.
    void RequestCodeLenses(text::Buffer& buffer, const std::string& serverKey);

    // The most recently applied code lenses for buffer, sorted by
    // startByte -- empty if never requested, not yet answered, or the
    // server reported none.
    [[nodiscard]] const std::vector<ResolvedCodeLens>& CodeLensSpans(const text::Buffer& buffer) const;

    // lsp-run-code-lens-at-point follow-up. Sends codeLens/resolve with
    // lens.raw verbatim (the same round-trip ResolveCodeAction already
    // does for code actions) -- called only when lens.hasCommand is
    // false. callback receives the resolved ResolvedCodeLens (hasCommand
    // true if the server actually filled it in) or nullopt on any failure.
    using ResolveCodeLensCallback = std::function<void(std::optional<ResolvedCodeLens> resolved)>;
    void ResolveCodeLens(text::Buffer& buffer, const ResolvedCodeLens& lens, ResolveCodeLensCallback callback,
                        const std::string& serverKey = {});

  private:
    // Returns the already-running client for language, or nullptr if none
    // is running and none is configured -- never spawns one. Used by
    // NotifyBufferClosed, which has no reason to spawn a server just to
    // immediately tell it to close something.
    [[nodiscard]] LspClient* ExistingClientForLanguage(const std::string& language) const;

    // Returns the running (lazily spawning one if needed) client for
    // serverKey against root, or nullptr if nothing is configured for it.
    // serverKey == kProseLanguageKey resolves its command via
    // ProseCheckerCommand() instead of LspServerCommand(serverKey) -- the
    // only place that distinction is made; everything else here treats it
    // like any other language key. LSP multi-root follow-up: root is what
    // actually gets sent as the "initialize" request's rootUri and threaded
    // into TryConnectToBroker's own (root, language) keying -- see
    // ConnectionKey for how it also (usually invisibly) affects clients_'s
    // own key.
    LspClient* ClientForLanguage(const std::string& serverKey, const std::filesystem::path& root);

    // LSP multi-root follow-up: clients_'s actual key for (root, serverKey)
    // -- collapses to serverKey unchanged when root equals
    // editor::ProjectRoot(), which is what keeps every pre-existing
    // single-root behavior (including every LspManagerTest fixture, which
    // registers a test client under a bare serverKey string via
    // SetClientForTesting) byte-for-byte unchanged: only a buffer whose
    // resolved root is genuinely more specific (LspRootResolver.h's marker
    // tier actually matched) earns a distinct, composite connection
    // identity. '\x1f' separator mirrors activeProgress_'s own existing
    // composite-key convention below, not a new one.
    [[nodiscard]] std::string ConnectionKey(const std::filesystem::path& root, const std::string& serverKey) const;

    // LSP multi-root follow-up: resolves and caches buffer's own LSP root
    // (LspRootResolver.h's ResolveLspRoot), keyed by its containing
    // directory + language rather than by Buffer* -- so buffers sharing a
    // directory reuse one cached walk, and a buffer whose path changes (e.g.
    // save-as) naturally resolves fresh against the new directory's own
    // cache entry instead of needing an explicit per-buffer invalidation.
    // ResolveLspRoot's own upward directory walk is a real per-call
    // filesystem cost -- SyncBuffer runs every Paint() for the active buffer
    // and every background tick for every other open one, so paying that
    // walk more than once per (directory, language) pair for this process's
    // lifetime would be a real, unbounded-per-frame regression, the same
    // class of bug this subsystem's own per-frame-sync-materialize follow-up
    // already fixed once (see SyncToServer's own doc comment). Never
    // invalidated within a session -- the same "detected once" reasoning
    // editor::ProjectRoot() itself already relies on.
    [[nodiscard]] std::filesystem::path ResolveCachedRoot(const std::filesystem::path& bufferPath, const std::string& language);

    struct BufferSyncState {
        // LSP multi-root follow-up: renamed from `language` -- now the
        // *connection* identity (ConnectionKey's own result, see
        // SyncTextToServer), not necessarily the bare language/serverKey
        // string, though the two are equal whenever this buffer's resolved
        // root is editor::ProjectRoot() (the common case). Every
        // Request*/Resolve* method below reads this field to find the
        // right running client via ExistingClientForLanguage -- that's what
        // actually routes a request to the correct per-root server, with no
        // other change needed at any of those call sites.
        std::string connectionKey;
        std::string uri;
        std::size_t lastSyncedGeneration = 0;
        int         version              = 0;
        bool        opened               = false;

        // sync-debounce follow-up: the content generation a currently-armed
        // syncDebounceTimers_ entry targets, distinct from
        // lastSyncedGeneration (what the server has actually been told
        // about). SyncToServer -- the buffer.Text()-based wrapper SyncBuffer
        // calls for the primary/prose connections, not SyncTextToServer
        // itself, which embedded-document sync also calls directly and
        // stays fully synchronous/undebounced -- only (re)arms the timer
        // when this doesn't already match buffer.ContentGeneration(); without
        // that check, a Paint() call that runs for any *other* reason during
        // the debounce window (another feature's own debounce firing, a
        // mouse event, ...) would keep resetting the clock and the sync
        // would never actually fire. Deliberately never reset back to
        // nullopt after firing -- ContentGeneration() is monotonic, so a
        // stale match can only recur if no further edit has happened, in
        // which case "nothing new to (re)arm" is exactly the right call
        // anyway.
        std::optional<std::size_t> pendingSyncGeneration;

        // incremental-sync follow-up: the exact text last sent to this
        // server (via didOpen or didChange), used as the "old" side of a
        // byte diff the next time this same (buffer, serverKey) syncs, so an
        // Incremental-capable server can be sent only the changed span
        // instead of the whole document again. Updated after *every*
        // successful send, including the full-text fallback path -- a
        // server that stops being Incremental-capable mid-session (or never
        // was) must still leave a valid baseline for a later sync to diff
        // against. Doubles per-buffer memory for a buffer talking to an
        // Incremental-capable server -- accepted, since a buffer big enough
        // for that to matter is already excluded from LSP sync entirely by
        // SyncBuffer's own huge-file-lsp-gate check.
        std::string lastSyncedText;
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
    // instead. LSP multi-root follow-up: root is the buffer's own resolved
    // LSP root (ResolveCachedRoot), forwarded to ClientForLanguage.
    void SyncToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId, const std::filesystem::path& root);

    // embedded-language-documents follow-up: SyncToServer's actual body,
    // generalized so the text synced isn't always buffer.Text() -- an
    // embedded virtual document has its own padded text, same byte length
    // and line/UTF-16 structure as the host buffer (see EmbeddedDocumentSync's
    // own doc comment) but not the same content.
    void SyncTextToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                          const std::string& documentText, const std::filesystem::path& root);

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

    // declaration/typeDefinition/implementation follow-up: RequestDefinition's
    // actual body, generalized over the wire method so
    // RequestDeclaration/RequestTypeDefinition/RequestImplementation can
    // share it verbatim -- every other aspect (sync-state resolution,
    // position encoding, response parsing via ExtractDefinitionLocations,
    // uri-to-path resolution) is identical across all four requests per the
    // LSP spec.
    // find-references follow-up: extraParams (default empty, a no-op merge)
    // is merge_patch'd into the request's own params object after the shared
    // textDocument/position pair is built -- lets RequestReferences add its
    // "context" field without every other caller's params shape changing.
    void SendLocationRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                             const std::string& serverKey, const Json& extraParams = Json::object());

    // call/type-hierarchy follow-up: SendLocationRequest's sibling for the
    // two prepare requests -- same textDocument/position params shape, but
    // ExtractHierarchyItems/ResolvedHierarchyItem instead of
    // ExtractDefinitionLocations/ResolvedLocation, so it isn't just another
    // extraParams-shaped call through SendLocationRequest itself.
    void SendHierarchyPrepareRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset,
                                     HierarchyItemsCallback callback, const std::string& serverKey);

    // call/type-hierarchy follow-up: shared by RequestSupertypes/
    // RequestSubtypes -- both send {"item": item.raw} and parse a bare
    // HierarchyItem[] back, differing only in method name. RequestIncomingCalls/
    // RequestOutgoingCalls don't share this (their response shape has the
    // extra fromRanges wrapper ExtractIncomingCalls/ExtractOutgoingCalls
    // parse), so each stays its own small method rather than forcing a third
    // shape through here.
    void SendTypeHierarchyStepRequest(const std::string& method, text::Buffer& buffer, const HierarchyItem& item,
                                      HierarchyItemsCallback callback, const std::string& serverKey);

    // prose-checking follow-up: flattens every source language's current
    // diagnostics slice for buffer (diagnosticsBySource_[&buffer]) into one
    // vector and pushes it via buffer.SetDiagnostics -- the actual merge
    // point that replaces the old "last publisher wins" wholesale replace.
    // Called after any publish, and after a source's slice is dropped
    // (disconnect) so stale diagnostics from a dead server don't linger.
    void PushMergedDiagnostics(text::Buffer& buffer);

    // pull-diagnostics follow-up. Called from SyncTextToServer right after
    // each real didOpen/didChange it sends -- no separate debounce timer,
    // since it rides that same cadence -- but only when
    // LspServerConfig.h's LspPullDiagnosticsEnabled() is on (checked at
    // that call site, not in here): unconditionally, this would mean one
    // extra request per content sync for every server, forever, which both
    // wastes round trips against a server that already pushes fine and
    // (found empirically, fixing a real test-suite regression) interleaves
    // unpredictably with whatever frame a test/caller expects to read next
    // off that same connection. Deliberately per-serverKey rather than
    // gated on any capability check (this class has none -- see
    // ExecuteCommand's own doc comment above), but unlike a one-shot
    // user-invoked request, this one repeats on every sync once enabled, so
    // a server that answers with a real error (not just "no diagnostics")
    // is latched in pullDiagnosticsUnsupported_ and never asked again for
    // this connection's lifetime -- the same "learned once, don't retry a
    // known-bad thing every cycle" shape failedCommands_ already
    // establishes, just keyed on a response instead of a spawn outcome.
    // Writes into the *same* diagnosticsBySource_[buffer][serverKey] slice
    // HandlePublishDiagnostics uses -- a server that also happens to push
    // publishDiagnostics unprompted just has this overwritten by the next
    // push, same as any two publishes today.
    void RequestPullDiagnostics(text::Buffer& buffer, const std::string& serverKey);

    void HandlePublishDiagnostics(const nlohmann::json& params, const std::string& language);

    // embedded-language-documents follow-up: factored out of
    // HandlePublishDiagnostics so pull-diagnostics' own response handler
    // (RequestPullDiagnostics) applies the exact same owned-ranges filtering
    // without duplicating it -- see HandlePublishDiagnostics' original
    // inline comment (now moved here) for the "why" of dropping a
    // diagnostic outside every owned range for an embedded server.
    void FilterToOwnedRanges(text::Buffer* buffer, const std::string& language, std::vector<text::Buffer::Diagnostic>& diagnostics) const;

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
    //
    // LSP multi-root follow-up: serverKey and connectionKey are threaded
    // through separately -- serverKey (plain) captures into
    // HandlePublishDiagnostics/the workDoneProgress handler (the
    // deliberately-still-plain-keyed caches, see this class's own header
    // comment), connectionKey captures into the disconnect handler, which
    // must erase the exact same clients_ entry ClientForLanguage inserted.
    void WireNotificationHandlers(LspClient& client, const std::string& serverKey, const std::string& connectionKey,
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
    //
    // LSP multi-root follow-up: serverKey drives every still-plain-keyed
    // cache below (unchanged from before this follow-up); connectionKey is
    // what actually gets erased from clients_/brokerBackedLanguages_ -- must
    // be the exact identity ClientForLanguage inserted those under, or a
    // disconnected client would never actually leave clients_.
    void ClientDisconnected(const std::string& serverKey, const std::string& connectionKey);

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    // edit-application-gaps follow-up: see SetApplyEditHandler's own doc
    // comment above.
    ApplyEditHandler applyEditHandler_;

    // LspManagerTest-broker-hermeticity follow-up: test-only override for
    // the broker socket path ClientForLanguage's TryConnectToBroker call
    // resolves against -- nullopt (the real default) resolves the real
    // BrokerSocketPath(). Without this, a test asserting a spawn failure
    // surfaces synchronously is at the mercy of whatever broker daemon (if
    // any) happens to already be listening on the machine running the test
    // -- see SetBrokerSocketPathOverrideForTesting's own doc comment.
    std::optional<std::filesystem::path> brokerSocketPathOverrideForTesting_;

    std::unordered_map<std::string, std::unique_ptr<LspClient>> clients_; // keyed by ConnectionKey (see its own doc comment)

    // LSP multi-root follow-up: see ResolveCachedRoot's own doc comment for
    // why this cache exists and why it's keyed by directory rather than by
    // Buffer*. Never erased -- unbounded only in the number of distinct
    // (directory, language) pairs ever synced this session, which tracks
    // the number of directories actually opened, not per-buffer/per-frame.
    std::unordered_map<std::string, std::filesystem::path> resolvedRootCache_;

    // LSP multi-root follow-up: buffer's own resolved LSP root, stamped by
    // SyncBuffer -- what SyncEmbeddedDocuments reads instead of re-resolving
    // (an embedded document shares its host buffer's root, never its own
    // embedded language's, see SyncBuffer's own doc comment). Erased in
    // NotifyBufferClosed.
    std::unordered_map<text::Buffer*, std::filesystem::path> bufferResolvedRoot_;

    // graceful-lsp-shutdown follow-up: languages whose current clients_
    // entry is a connection to an already-running LSP broker daemon rather
    // than a subprocess this LspManager spawned itself -- Shutdown() must
    // never send "shutdown"/"exit" to one of these, since the broker (and
    // whichever other ned processes are also attached to it) still needs
    // that server running after this process exits. Stamped in
    // ClientForLanguage right where that distinction is actually known
    // (TryConnectToBroker succeeding vs. falling through to the direct-spawn
    // fallback); cleared in ClientDisconnected alongside clients_ itself, so
    // a stale entry can never outlive the client it described.
    std::unordered_set<std::string> brokerBackedLanguages_;

    // prose-checking follow-up: outer key is the buffer, inner key is the
    // server ("cpp", kProseLanguageKey, ...) -- a buffer now has up to two
    // concurrent sync states (its primary language server and the prose
    // checker), each tracking its own didOpen/version/lastSyncedGeneration
    // independently. Was a flat unordered_map<Buffer*, BufferSyncState>
    // before this could ever be true.
    std::unordered_map<text::Buffer*, std::unordered_map<std::string, BufferSyncState>> bufferState_;

    // huge-file-lsp-gate follow-up: which huge buffers have already gotten
    // their one-time "no LSP/prose sync" LogError -- SyncBuffer checks/
    // inserts into this instead of logging on every skipped call (every
    // Paint() while the buffer is focused). Erased in NotifyBufferClosed.
    std::unordered_set<text::Buffer*> hugeSyncSkipNotified_;

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

    // sync-debounce follow-up: one debounce timer per (buffer, serverKey)
    // with a pending textDocument/didChange -- see BufferSyncState::
    // pendingSyncGeneration's own doc comment for why SyncTextToServer only
    // (re)arms an entry when a genuinely new edit has landed, not on every
    // Paint()-driven re-entry. Nested the same way bufferState_ itself is
    // (per buffer, per serverKey) since primary and prose sync
    // independently. NotifyBufferClosed erases a buffer's whole entry
    // before any of its timers could fire against a dead buffer -- same
    // rationale as diagnosticsDebounceTimers_ just above.
    std::unordered_map<text::Buffer*, std::unordered_map<std::string, ned::ui::DeadlineTimer>> syncDebounceTimers_;

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

    // semantic-tokens/on-type-formatting follow-up. Keyed by serverKey,
    // captured from that server's own `initialize` response the moment it
    // arrives; erased in ClientDisconnected alongside everything else tied
    // to that connection's lifetime (a respawned server may advertise a
    // different legend/trigger set than the one that just died). See
    // SemanticTokensLegendFor/OnTypeFormattingTriggersFor's own doc comment
    // above for why these exist instead of a general capability store.
    std::unordered_map<std::string, SemanticTokensLegend>     semanticTokensLegend_;
    std::unordered_map<std::string, OnTypeFormattingTriggers> onTypeFormattingTriggers_;

    // incremental-sync follow-up: same role/lifetime as the two caches just
    // above -- captured from `initialize`'s own response, erased in
    // ClientDisconnected. Absent means "assume Full" (see
    // ExtractTextDocumentSyncKind's own doc comment) -- TextDocumentSyncKindFor
    // applies that default itself, so callers never re-derive it.
    std::unordered_map<std::string, TextDocumentSyncKind> textDocumentSyncKind_;

    // pull-diagnostics follow-up: see RequestPullDiagnostics' own doc
    // comment above for why this exists instead of a capability check.
    // Erased in ClientDisconnected alongside everything else tied to that
    // connection's lifetime -- a respawned server gets one fresh attempt.
    std::unordered_set<std::string> pullDiagnosticsUnsupported_;

    // semanticTokens follow-up. requestedGeneration_ is buffer's own
    // ContentGeneration() at the moment RequestSemanticTokensFull last sent
    // a request for it -- the dedup gate that keeps a cursor-blink/scroll-
    // only repaint (content unchanged) from resending the request every
    // frame, the same role bufferState_'s own lastSyncedGeneration plays
    // for didChange. requestCounter_ is a separate, plain incrementing
    // per-buffer counter -- the staleness guard inside the response
    // callback (only the reply to the *latest* request for a buffer is
    // ever applied, an older one arriving after a newer request was
    // already sent is just discarded, matching
    // BufferView::signatureHelpRequestGeneration_'s own precedent). spans_/
    // generation_ are the applied result BufferView actually reads. All
    // four erased together in NotifyBufferClosed.
    std::unordered_map<text::Buffer*, std::size_t>                     semanticTokensRequestedGeneration_;
    std::unordered_map<text::Buffer*, std::size_t>                     semanticTokensRequestCounter_;
    std::unordered_map<text::Buffer*, std::vector<editor::HighlightSpan>> semanticTokenSpans_;
    std::unordered_map<text::Buffer*, std::size_t>                     semanticTokensGeneration_;

    // inlayHint follow-up. requestedRange_ is the (contentGeneration,
    // viewportStartByte, viewportEndByte) triple last requested for a
    // buffer -- the dedup gate (same role semanticTokensRequestedGeneration_
    // plays, just keyed on viewport too, since scrolling to reveal new
    // content is worth a fresh request even when content itself hasn't
    // changed). requestCounter_ is the same plain per-buffer staleness
    // guard semanticTokensRequestCounter_ is. spans_ is the applied,
    // byte-resolved, sorted-by-byteOffset result BufferView reads.
    // unsupported_ is the same "learned once from a real error response,
    // stop asking" set pullDiagnosticsUnsupported_ already establishes.
    // All four erased together in NotifyBufferClosed (unsupported_ instead
    // cleared in ClientDisconnected, same as the others of its kind).
    std::unordered_map<text::Buffer*, std::tuple<std::size_t, std::size_t, std::size_t>> inlayHintsRequestedRange_;
    std::unordered_map<text::Buffer*, std::size_t>                                       inlayHintsRequestCounter_;
    std::unordered_map<text::Buffer*, std::vector<ResolvedInlayHint>>                    inlayHintSpans_;
    std::unordered_set<std::string>                                                      inlayHintsUnsupported_;

    // codeLens follow-up. requestedGeneration_ is the same
    // dedup-by-content-generation gate semanticTokensRequestedGeneration_
    // is (whole-document scope, no viewport to also key on).
    // requestCounter_/spans_/unsupported_ mirror the same three fields'
    // roles for semanticTokens/inlayHint. All erased in NotifyBufferClosed
    // (unsupported_ instead cleared in ClientDisconnected).
    std::unordered_map<text::Buffer*, std::size_t>                   codeLensRequestedGeneration_;
    std::unordered_map<text::Buffer*, std::size_t>                   codeLensRequestCounter_;
    std::unordered_map<text::Buffer*, std::vector<ResolvedCodeLens>> codeLensSpans_;
    std::unordered_set<std::string>                                  codeLensUnsupported_;

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
