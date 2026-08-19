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

struct CodeAction {
    std::string                    title;
    std::vector<WorkspaceTextEdit> edits;         // edits touching ownUri only; empty if hasEdit is false or touchesOtherFiles is true
    bool                           hasEdit           = false; // false for a bare Command with no "edit" at all -- executing one is out of scope
    bool                           touchesOtherFiles = false; // a "changes" map naming more than one URI, or a "documentChanges" form (unparsed)

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

    bool operator==(const CodeAction&) const = default;
};

// Parses one response item, either a bare Command (has "command", no
// "edit" -- hasEdit=false) or a real CodeAction ("title" required; "edit"
// is an optional WorkspaceEdit). Only the "changes": {uri: TextEdit[]}
// shape of WorkspaceEdit is parsed; an edit naming a URI other than ownUri,
// or a WorkspaceEdit using "documentChanges" instead of "changes", is
// reported as touchesOtherFiles=true with edits left empty -- refused
// wholesale by the caller rather than partially applied. Exposed publicly
// (not just used internally by ExtractCodeActions' own loop below) so
// LspManager::ResolveCodeAction can parse a codeAction/resolve response --
// itself always exactly one CodeAction, not an array -- the same way.
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

// rename follow-up. One URI's worth of edits out of a textDocument/rename
// response's WorkspaceEdit -- unlike CodeAction::edits (deliberately scoped
// to one buffer's own URI, see that struct's own doc comment above), a
// rename is expected to touch every file a symbol appears in, so this keeps
// one entry per URI the response actually named, not just the buffer the
// request was sent from.
struct RenameEdit {
    std::string                    uri;
    std::vector<WorkspaceTextEdit> edits;
};

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

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPCONTENT_H
