//
// hover/completion follow-up. Parses the two LSP response payload shapes
// LspManager::RequestHover/RequestCompletion consume -- factored out to
// namespace scope (rather than kept as file-local anonymous-namespace
// helpers in LspManager.cpp, which is where these started) specifically so
// they're directly unit-testable against crafted JSON, the same "extract a
// pure conversion into its own declared, testable header" precedent
// LspPosition.h already established, without needing a real LspClient/
// subprocess round-trip just to exercise parsing logic.
//

#ifndef NED_EDITOR_LSP_LSPCONTENT_H
#define NED_EDITOR_LSP_LSPCONTENT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "LspPosition.h"

namespace ned::editor::lsp {

using Json = nlohmann::json;

struct CompletionItem {
    std::string label;
    std::string insertText; // falls back to label if the server omitted it
    // snippet-expansion follow-up: insertTextFormat == 2 -- insertText is
    // TextMate snippet syntax (${1:...}), not literal text; the accept path
    // must expand it (Editor/Snippet.h), never insert it raw. Trailing with
    // a default so every existing designated-init call site (including the
    // dabbrev/Janet-synthesized items, which are never snippets) keeps
    // compiling unchanged.
    bool isSnippet = false;

    // completion-popup follow-up: the raw LSP CompletionItemKind (1-25,
    // spec section 3.17.2.3), 0 for unset/unknown -- BufferView buckets
    // this down to a small glyph via its own CompletionKindBucket, the
    // same "keep the wire value verbatim, let the UI layer interpret it"
    // split VcsStatusEntry::state/VcsBlameLine::date already establish
    // elsewhere. detail is the server's short type/signature string (e.g.
    // "(int, int) -> int"), shown as the popup row's right-aligned column;
    // empty when the server omitted it.
    int         kind = 0;
    std::string detail;

    // completion-popup-preview follow-up: the server's completionItem.documentation
    // (string | MarkupContent per the spec), joined/flattened the same way
    // ExtractHoverText already does for hover's own "contents" field -- reused
    // directly rather than re-implementing the string-or-MarkupContent extraction.
    // Empty when the server omitted it (dabbrev/Janet-binding synthesized items
    // always leave this empty -- no doc source to draw from).
    std::string documentation;

    bool operator==(const CompletionItem&) const = default;
};

// Extracts a hover result's "contents" field, which per the LSP spec is one
// of: a bare string, a MarkupContent/MarkedString object ({"value": "..."},
// possibly alongside "kind"/"language"), or an array of either -- joined
// with a blank line between entries when there's more than one. nullopt for
// a null/missing/empty result.
[[nodiscard]] std::optional<std::string> ExtractHoverText(const Json& result);

// A completion response is either a bare CompletionItem[] or a
// CompletionList {isIncomplete, items} -- both handled uniformly. Items
// without a "label" are skipped; insertText falls back to label. Returned
// in server order -- no client-side re-filtering/re-sorting.
[[nodiscard]] std::vector<CompletionItem> ExtractCompletionItems(const Json& result);

// code-actions follow-up. One text edit within a WorkspaceEdit's "changes"
// array for a single URI -- start/end stay as LSP Positions (not byte
// offsets) since a code action's edits are only resolved against a buffer's
// *current* content at the moment the user actually accepts it, which can
// be well after the response arrived.
struct WorkspaceTextEdit {
    LspPosition start;
    LspPosition end;
    std::string newText;

    bool operator==(const WorkspaceTextEdit&) const = default;
};

// project-undo follow-up: one URI's worth of edits out of a WorkspaceEdit's
// "changes" map -- shared by CodeAction::edits and RenameResult::edits
// below, since both a code action's and a rename's edit can touch more than
// one file the exact same way (LSP's WorkspaceEdit is the one wire shape
// behind both requests).
struct RenameEdit {
    std::string                    uri;
    std::vector<WorkspaceTextEdit> edits;
};

struct CodeAction {
    std::string             title;
    std::vector<RenameEdit> edits;           // one entry per touched URI/file; empty if hasEdit is false or touchesUnsupportedForm is true
    bool                    hasEdit = false; // false for a bare Command with no "edit" at all -- executing one is out of scope
    // project-undo follow-up: was touchesOtherFiles, refused wholesale --
    // multiple URIs are now parsed into edits above like RenameResult
    // already does. Only a "documentChanges" WorkspaceEdit (file
    // create/rename/delete, not just edits to existing ones) is still
    // unparsed/refused -- same scope cut RenameResult's own
    // touchesUnsupportedForm documents.
    bool touchesUnsupportedForm = false;

    // code-actions-resolve follow-up. Many real servers (clangd included)
    // advertise codeActionProvider.resolveProvider and deliberately send a
    // CodeAction back with no "edit" yet -- computing every possible fix's
    // edit just to list titles is wasted work the server does only once the
    // client actually asks, via a codeAction/resolve request carrying this
    // exact original item back verbatim. resolvable is true only for an
    // item shaped like a real CodeAction (has "kind" -- a Command object
    // never does) that's missing "edit": a genuine edit-less Command
    // (resolvable=false, hasEdit=false) can't be resolved into one, since
    // executing a server-side Command is out of this codebase's scope
    // either way. raw is the original item verbatim, needed to send back
    // for resolve.
    bool resolvable = false;
    Json raw;

    // quick-fix follow-up. kind is the raw CodeActionKind string
    // ("quickfix", "refactor.rewrite", ...; empty for a bare Command, which
    // never has one); isPreferred is the server's own "apply this one on an
    // auto-fix command" marker (clangd sets it on the fix tied to the
    // diagnostic at the requested range). Both drive
    // BufferView::RequestQuickFixAtPoint's pick-without-asking decision.
    std::string kind;
    bool        isPreferred = false;

    // executeCommand follow-up. The nested Command object a real
    // CodeAction's own "command" field carries, or -- for a bare Command
    // response item, which *is* one of these shapes at the top level --
    // that item's own command/arguments folded in here identically, so
    // BufferView::ApplyCodeAction need not care which wire shape produced
    // it. Independent of hasEdit/resolvable: a resolvable action
    // legitimately has neither edit nor command yet (codeAction/resolve
    // only ever fills in "edit" per spec, never "command"). name is the
    // opaque, server-defined command identifier (e.g. harper-ls's
    // "HarperAddToUserDict"); arguments is the raw "arguments" array
    // round-tripped verbatim -- the client has no business interpreting it,
    // only replaying it back via workspace/executeCommand.
    struct CodeActionCommand {
        std::string name;
        Json        arguments; // [] if the server sent no "arguments" at all

        bool operator==(const CodeActionCommand&) const = default;
    };
    std::optional<CodeActionCommand> command;

    bool operator==(const CodeAction&) const = default;
};

// Parses one response item, either a bare Command (has "command", no
// "edit" -- hasEdit=false) or a real CodeAction ("title" required; "edit"
// is an optional WorkspaceEdit). Only the "changes": {uri: TextEdit[]}
// shape of WorkspaceEdit is parsed -- one RenameEdit per named URI, however
// many that is; a WorkspaceEdit using "documentChanges" instead of
// "changes" is reported as touchesUnsupportedForm=true with edits left
// empty -- refused wholesale by the caller rather than partially applied.
// Exposed publicly (not just used internally by ExtractCodeActions' own
// loop below) so LspManager::ResolveCodeAction can parse a
// codeAction/resolve response -- itself always exactly one CodeAction, not
// an array -- the same way. ownUri is kept in the signature for call-site
// symmetry with every other ExtractX(..., ownUri) function in this file,
// though project-undo follow-up: edits parsing no longer filters by it
// (every URI the "changes" map names is now kept, not just this one).
[[nodiscard]] CodeAction ExtractSingleCodeAction(const Json& item, const std::string& ownUri);

// Each element of a textDocument/codeAction response array, parsed via
// ExtractSingleCodeAction above. Items missing "title" are skipped.
// Returned in server order -- no client-side re-sorting.
[[nodiscard]] std::vector<CodeAction> ExtractCodeActions(const Json& result, const std::string& ownUri);

// go-to-definition follow-up. One resolved target from a
// textDocument/definition response -- the same shape is reused verbatim for
// /declaration, /typeDefinition, and /implementation, which the LSP spec
// gives an identical result shape. uri stays a raw string here (not resolved
// to a filesystem::path) so this stays a pure, URI-agnostic parser like
// every other ExtractX function in this file -- LspManager is what resolves
// it, the same layering ExtractCodeActions/ExtractSingleCodeAction already
// keep (they take ownUri as a plain string too).
struct DefinitionLocation {
    std::string uri;
    LspPosition position;

    bool operator==(const DefinitionLocation&) const = default;
};

// Parses a textDocument/definition-shaped result: per the LSP spec, this is
// a single Location, a Location[], or a LocationLink[] (which reports its
// target as targetUri/targetSelectionRange instead of uri/range) -- all
// three handled uniformly. A malformed entry (no uri/targetUri, or a
// range/targetSelectionRange missing "start") is skipped, not treated as a
// parse error. Empty for a null result.
[[nodiscard]] std::vector<DefinitionLocation> ExtractDefinitionLocations(const Json& result);

// rename follow-up: a rename is expected to touch every file a symbol
// appears in, the same "one entry per named URI" shape RenameEdit (defined
// above, alongside CodeAction) already gives CodeAction::edits.
struct RenameResult {
    std::vector<RenameEdit> edits;
    // "documentChanges" is a real, more general WorkspaceEdit form (needed
    // for a rename that also creates/renames/deletes files, not just edits
    // existing ones) this v1 doesn't parse -- same scope cut
    // ExtractWorkspaceEditForUri's own doc comment already established for
    // code actions. touchesUnsupportedForm is true when the response used
    // it; edits is left empty in that case, refused wholesale rather than
    // silently doing a partial rename.
    bool touchesUnsupportedForm = false;
    bool hasEdit                = false;
};

// Parses a textDocument/rename response -- a bare WorkspaceEdit, not
// wrapped in an array/item the way a code action response is. Only the
// "changes": {uri: TextEdit[]} form is understood; see RenameResult's own
// doc comment for the "documentChanges" scope cut. A URI whose own edit
// array is empty after parsing is dropped rather than kept as a no-op
// entry.
[[nodiscard]] RenameResult ExtractRenameEdits(const Json& result);

// formatting follow-up. A textDocument/formatting or /rangeFormatting
// response is a bare TextEdit[] | null against the single requesting
// document -- unlike RenameResult's per-URI "changes" map, no URI grouping
// is needed (formatting only ever targets the document the request was sent
// for). Reuses WorkspaceTextEdit's own {start,end,newText} shape verbatim.
// Empty for a null/non-array result; an entry missing "range" is skipped,
// not treated as a parse error, matching every other ExtractX function here.
[[nodiscard]] std::vector<WorkspaceTextEdit> ExtractFormattingEdits(const Json& result);

// documentHighlight follow-up. One occurrence of the symbol under point,
// always within the requesting document itself -- unlike DefinitionLocation/
// RenameEdit there is no per-item URI at all, so this is the first
// LspPosition-pair result in this file whose *end* position actually matters
// to the caller (BufferView needs the whole range to paint, not just a jump
// target). kind is the LSP DocumentHighlightKind (1=Text, 2=Read, 3=Write);
// defaults to 1 for a server that omits it, the spec's own stated default.
struct DocumentHighlight {
    LspPosition start;
    LspPosition end;
    int         kind = 1;

    bool operator==(const DocumentHighlight&) const = default;
};

// Parses a textDocument/documentHighlight response -- DocumentHighlight[] |
// null, never wrapped/nested like Location/LocationLink. Empty for a null or
// non-array result; an entry missing "range"/"start"/"end" is skipped, not
// treated as a parse error, matching every other ExtractX function here.
[[nodiscard]] std::vector<DocumentHighlight> ExtractDocumentHighlights(const Json& result);

// signature-help follow-up. Reduces a textDocument/signatureHelp response
// straight to the single status-line-ready string BufferView/Commands.cpp
// show verbatim -- mirrors ExtractHoverText's own "already plain, already
// the caller's whole answer" contract, so LspManager::RequestSignatureHelp
// can reuse HoverCallback's exact shape instead of a new one. Picks
// signatures[activeSignature] (default index 0), then wraps the active
// parameter's own slice of that signature's label in "**...**" -- plain
// ASCII (EchoArea::Paint renders the status line byte-by-byte, not
// UTF-8-aware, so a multi-byte marker like guillemets would come out as
// blank padding instead of a visible glyph) and terminal-safe either way,
// needing no dependency on Source/UI/'s EchoArea sentinel scheme, which
// this file (Editor/) must never depend on. The active parameter is
// read from the signature's own "activeParameter" first, falling back to
// the response's top-level one per spec; a parameter's "label" may be
// either a substring of the signature label (first occurrence used) or a
// [start, end) pair of UTF-16 code-unit offsets into it, both handled. No
// active parameter resolvable (missing, out of range, or a substring/range
// that doesn't actually land in the label) leaves the label unwrapped
// rather than treated as a parse failure. nullopt only when there is no
// usable signature at all (null result, empty/missing "signatures", or a
// signature missing "label").
[[nodiscard]] std::optional<std::string> ExtractSignatureHelp(const Json& result);

// symbol-search follow-up. One entry from a textDocument/documentSymbol or
// workspace/symbol response -- both requests return the same underlying
// vocabulary (name/kind/location), just at different scopes, so one shape
// serves both. uri stays a raw string (LspManager resolves it), the same
// layering ExtractCodeActions/ExtractDefinitionLocations already keep.
struct SymbolEntry {
    std::string name;
    std::string containerName; // immediate parent's name (hierarchical) or the server's own containerName; "" if none
    int         kind = 0;      // raw LSP SymbolKind (1-26); 0 for a malformed/missing entry, never produced by ExtractSymbols itself
    std::string uri;
    LspPosition position; // jump target: a DocumentSymbol's selectionRange.start, or a SymbolInformation/WorkspaceSymbol's range.start

    bool operator==(const SymbolEntry&) const = default;
};

// Human-readable word for a raw LSP SymbolKind (1-26 per spec) -- covers
// every value the spec currently defines; out-of-range or absent (0)
// returns "symbol" rather than being treated as a parse failure, since a
// future spec revision adding a new kind should still list, not vanish.
[[nodiscard]] std::string_view SymbolKindLabel(int kind);

// Parses a textDocument/documentSymbol or workspace/symbol response,
// handling every wire shape the spec allows uniformly:
//   - hierarchical DocumentSymbol[] ("range"/"selectionRange", no "location"
//     at all) -- recursed via "children", ownUri applied to every entry
//     (a DocumentSymbol never carries its own uri, unlike the other two
//     shapes) and containerName set to each entry's own immediate parent
//     name, mirroring SymbolInformation.containerName's real-world meaning.
//   - flat SymbolInformation[] ("location": {uri, range}, optional
//     top-level "containerName") -- ownUri is ignored, each entry's own
//     location.uri is used instead.
//   - 3.17 WorkspaceSymbol[] (same as SymbolInformation, but "location" may
//     be {uri} alone with no range for a symbol the server hasn't resolved
//     the precise range for yet) -- treated as position {0, 0} rather than
//     skipped: the symbol itself is still real and worth listing/jumping to
//     the top of its file, workspaceSymbol/resolve for the precise range is
//     a documented v1 cut (this client never sends it).
// An entry missing "name" is skipped, not a parse error. Returned in
// response order -- document order for documentSymbol, already
// server-ranked-against-its-query order for workspace/symbol; no
// client-side re-sorting either way.
[[nodiscard]] std::vector<SymbolEntry> ExtractSymbols(const Json& result, const std::string& ownUri = {});

// semantic-tokens/on-type-formatting follow-up. Two pieces of an
// `initialize` response's `capabilities` this client cannot proceed without
// once it decides to ask -- unlike every other ExtractX function in this
// file, these are read from the handshake response itself, not from a
// per-feature request's own response. Deliberately not part of a general
// "does the server support X" capability store: this codebase never gates a
// request on the server's advertised capabilities (see LspManager.h's own
// ExecuteCommand doc comment) -- it just sends the request and treats an
// error/empty response like any other "no results" case. These two are
// different in kind: the response is literally uninterpretable without
// them (semanticTokens' numeric token stream needs the server's own
// type/modifier vocabulary to decode at all; onTypeFormatting needs to know
// *which* keystroke should even trigger a request, since there's no
// sensible default). nullopt when the server doesn't advertise the
// corresponding provider at all -- that absence is what tells the caller
// not to bother asking, the same "empty means unsupported" signal every
// other ExtractX result already carries.
struct SemanticTokensLegend {
    std::vector<std::string> tokenTypes;
    std::vector<std::string> tokenModifiers;

    bool operator==(const SemanticTokensLegend&) const = default;
};

// Parses `capabilities.semanticTokensProvider.legend` out of a full
// `initialize` response. Per spec, semanticTokensProvider is always an
// object (never a bare boolean) and legend is required within it whenever
// the provider is present at all; a present-but-malformed legend (either
// array missing or not an array) is treated the same as an absent provider.
[[nodiscard]] std::optional<SemanticTokensLegend> ExtractSemanticTokensLegend(const Json& initializeResult);

struct OnTypeFormattingTriggers {
    std::string              first; // documentOnTypeFormattingProvider.firstTriggerCharacter, required by spec whenever the provider is present
    std::vector<std::string> more;  // ...moreTriggerCharacter, optional -- [] if the server sent none

    bool operator==(const OnTypeFormattingTriggers&) const = default;
};

// Parses `capabilities.documentOnTypeFormattingProvider` out of a full
// `initialize` response. nullopt when the provider is absent, or present
// without a "firstTriggerCharacter" string (the one field the spec actually
// requires).
[[nodiscard]] std::optional<OnTypeFormattingTriggers> ExtractOnTypeFormattingTriggers(const Json& initializeResult);

// incremental-sync follow-up. LSP's own TextDocumentSyncKind (spec section
// "TextDocumentSyncKind") -- what a server's advertised
// capabilities.textDocumentSync says about how it wants
// textDocument/didChange's contentChanges shaped. A third legitimate
// exception to "no general capability store" (see this comment block's own
// opening paragraph above): the outgoing payload shape is uninterpretable
// without knowing which kind a server actually negotiated, the same reason
// SemanticTokensLegend/OnTypeFormattingTriggers are extracted here instead
// of just sent-and-forgotten.
enum class TextDocumentSyncKind {
    None        = 0,
    Full        = 1,
    Incremental = 2,
};

// Parses `capabilities.textDocumentSync` out of a full `initialize`
// response. Per spec this field is either a bare integer (0/1/2, the legacy
// form) or an object whose own "change" field carries the same 0/1/2
// ("openClose"/"save" aren't read -- not needed by anything this client
// does). nullopt when absent, non-numeric, or an out-of-range int --
// LspManager::TextDocumentSyncKindFor treats nullopt the same as Full (see
// its own doc comment for why: preserving every already-working server's
// current behavior takes priority over the spec's technical "absent means
// None" default).
[[nodiscard]] std::optional<TextDocumentSyncKind> ExtractTextDocumentSyncKind(const Json& initializeResult);

// pull-diagnostics follow-up. One entry from a textDocument/diagnostic
// response's "items" array -- same range/severity/message shape
// LspManager::HandlePublishDiagnostics already parses inline for the push
// form (publishDiagnostics), kept here as its own struct rather than
// reused directly since converting to byte offsets needs a Buffer's
// content, which this file deliberately has no dependency on (LspManager
// does that conversion itself, the same layering ExtractFormattingEdits'
// LspPosition-based WorkspaceTextEdit already established).
struct PullDiagnosticItem {
    LspPosition start;
    LspPosition end;
    int         severity = 3; // raw LSP DiagnosticSeverity (1=Error..4=Hint); 3=Information is the spec's own implied default
    std::string message;
};

// Parses a textDocument/diagnostic response's DocumentDiagnosticReport
// envelope. A "full" report (`kind: "full"`, or no "kind" at all -- some
// servers omit it despite the spec listing it as required) yields its own
// "items" array. An "unchanged" report (`kind: "unchanged"`, carrying only
// a "resultId") means the server has nothing new to say since the caller's
// last request -- nullopt, distinct from an empty vector, so the caller
// knows to leave its existing diagnostics slice alone rather than clearing
// it. Also nullopt for a non-object result or an unrecognized "kind".
[[nodiscard]] std::optional<std::vector<PullDiagnosticItem>> ExtractPullDiagnosticReport(const Json& result);

// semanticTokens follow-up. One decoded entry from a
// textDocument/semanticTokens/full response's delta-encoded "data" array --
// already unpacked to absolute line/character (the encoding's own
// deltaLine/deltaStartChar arithmetic resolved here, once, rather than
// leaving it to every caller), but still carrying the server's own raw
// token-type/modifier indices rather than a resolved name -- resolving
// those against the server's own legend (Phase 0's
// ExtractSemanticTokensLegend) is the caller's job, the same layering split
// PullDiagnosticItem's raw severity int already uses.
struct SemanticToken {
    LspPosition   start;
    std::size_t   length;         // UTF-16 code units, same unit LspPosition::character already uses
    std::size_t   tokenTypeIndex; // index into the server's own legend.tokenTypes
    std::uint32_t tokenModifiers; // bitset -- bit i set means legend.tokenModifiers[i] applies

    bool operator==(const SemanticToken&) const = default;
};

// Decodes a textDocument/semanticTokens/full (or /range) response's "data"
// array: quintuples of (deltaLine, deltaStartChar, length, tokenType,
// tokenModifiers) per the spec's relative encoding -- deltaLine is relative
// to the previous token's line, deltaStartChar is relative to the previous
// token's start character *only when deltaLine is 0* (an absolute column
// otherwise), exactly as the spec defines it. Empty for a null/missing/
// non-array "data", or one whose length isn't a multiple of 5.
[[nodiscard]] std::vector<SemanticToken> ExtractSemanticTokens(const Json& result);

// inlayHint follow-up. One entry from a textDocument/inlayHint response --
// position stays an LspPosition (not yet resolved to a byte offset; the
// caller does that against real buffer content, the same layering every
// other LSP-position-carrying struct in this file already uses). label is
// the resolved display text: a bare string per the simpler response shape,
// or the concatenated "value" fields of an InlayHintLabelPart[] per the
// richer one -- both flattened to plain text here, since this client
// doesn't support per-part tooltips/commands (a v1 scope cut).
struct InlayHint {
    LspPosition position;
    std::string label;

    bool operator==(const InlayHint&) const = default;
};

// Parses a textDocument/inlayHint response: InlayHint[] | null. An entry
// missing "position", missing "label", or resolving to an empty label
// (nothing to render either way) is skipped, not treated as a parse error.
[[nodiscard]] std::vector<InlayHint> ExtractInlayHints(const Json& result);

// codeLens follow-up. One entry from a textDocument/codeLens response --
// start/end stay LspPositions, resolved to byte offsets by the caller, the
// same layering every other LSP-position-carrying struct in this file
// already uses. A lens sent with no "command" at all (hasCommand=false)
// needs codeLens/resolve before it's runnable -- title/commandName/
// commandArguments are all empty/default in that case. raw is the original
// item verbatim, needed to send back for resolve (same role
// CodeAction::raw already plays for codeAction/resolve).
struct CodeLens {
    LspPosition start;
    LspPosition end;
    std::string title;                            // Command.title -- what's rendered; empty until resolved
    std::string commandName;                      // Command.command -- opaque, replayed via workspace/executeCommand
    Json        commandArguments = Json::array(); // Command.arguments, round-tripped verbatim
    bool        hasCommand       = false;
    Json        raw;

    bool operator==(const CodeLens&) const = default;
};

// Parses a single textDocument/codeLens response item (also what a
// codeLens/resolve response is -- always exactly one CodeLens, not an
// array, mirroring ExtractSingleCodeAction's own reuse for resolve).
[[nodiscard]] CodeLens ExtractSingleCodeLens(const Json& item);

// Parses a textDocument/codeLens response: CodeLens[] | null. An entry
// missing "range" is skipped, not treated as a parse error.
[[nodiscard]] std::vector<CodeLens> ExtractCodeLenses(const Json& result);

// call/type-hierarchy follow-up. CallHierarchyItem and TypeHierarchyItem are
// wire-identical per the LSP spec (name/kind/detail/uri/range/
// selectionRange/tags?/data?) -- one shared shape, the same "identical wire
// shape, one struct" precedent RenameEdit already establishes for
// CodeAction/RenameResult. kind uses the same raw LSP SymbolKind vocabulary
// SymbolEntry::kind does (SymbolKindLabel applies here too). raw is the
// entire original item verbatim -- the callHierarchy/incomingCalls,
// .../outgoingCalls, typeHierarchy/supertypes, and .../subtypes requests all
// take the *whole* item back as their "item" parameter (not just its "data"
// token), so a partial reconstruction from this struct's own fields would
// silently drop anything else the server sent (tags, extension fields);
// the same "keep raw, replay unmodified" contract CodeAction::raw/
// CodeLens::raw already use for resolve, just replayed as a sub-field of a
// larger request object here rather than the whole request body.
struct HierarchyItem {
    std::string name;
    std::string detail; // server's short type/signature string; "" if omitted
    int         kind = 0;
    std::string uri;
    LspPosition position; // jump target: selectionRange.start
    Json        raw;      // the original item verbatim; round-tripped as the next request's "item"

    bool operator==(const HierarchyItem&) const = default;
};

// Parses a textDocument/prepareCallHierarchy, textDocument/prepareTypeHierarchy,
// typeHierarchy/supertypes, or typeHierarchy/subtypes response -- all four
// are a bare HierarchyItem[] | null, the simplest of the hierarchy shapes. An
// entry missing "name", "uri", or "selectionRange" is skipped, not treated as
// a parse error, matching every other ExtractX function in this file.
[[nodiscard]] std::vector<HierarchyItem> ExtractHierarchyItems(const Json& result);

// One call-site entry from a callHierarchy/incomingCalls or
// .../outgoingCalls response -- "from"/"to" differ only in field name per
// spec (the caller's own item for incoming, the callee's for outgoing), both
// parsed through one shared helper. callSites is fromRanges[*].start only --
// a jump target, the same "start is enough" convention DefinitionLocation/
// SymbolEntry already use -- though a call site can legitimately appear more
// than once within the same function, so every occurrence is kept rather
// than just the first; empty if the server sent no "fromRanges" at all.
struct HierarchyCall {
    HierarchyItem            item;
    std::vector<LspPosition> callSites;

    bool operator==(const HierarchyCall&) const = default;
};

// Parses a callHierarchy/incomingCalls response: {from, fromRanges}[] | null.
// An entry missing "from" (or whose "from" is itself missing "name"/"uri"/
// "selectionRange") is skipped, not treated as a parse error.
[[nodiscard]] std::vector<HierarchyCall> ExtractIncomingCalls(const Json& result);

// Parses a callHierarchy/outgoingCalls response: {to, fromRanges}[] | null --
// same shape as ExtractIncomingCalls above, just keyed on "to" instead of
// "from" per spec.
[[nodiscard]] std::vector<HierarchyCall> ExtractOutgoingCalls(const Json& result);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPCONTENT_H
