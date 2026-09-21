//
// One completion candidate, independent of where it came from.
//
// The popup is fed by four producers -- a language server, the snippet
// registry, the buffer's own words, and Janet's bound names -- and only the
// first of them speaks LSP. This type is what the other three stop
// impersonating: a source tag says what a candidate is, so the session, the
// popup and the accept path can ask that question directly instead of
// inferring it from a null `raw` field or from which code path happened to
// build the list.
//
// Three fields stay LSP-shaped on purpose. `replaceEdit`/`additionalEdits`
// carry LSP Positions because the apply path (Lsp/EditApply.h) resolves them
// against the buffer's live content, and `raw` is the verbatim item a server
// demands back on completionItem/resolve. A locally-produced candidate
// leaves all three empty, which is exactly what "this one has nothing to
// round-trip" should look like.
//

#ifndef NED_EDITOR_COMPLETION_H
#define NED_EDITOR_COMPLETION_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Lsp/Content.h"

namespace ned::editor {

// Who produced a candidate, and -- via CompletionSourceRank -- how the four
// break a tie against each other. A snippet leads because it is the only one
// of the four the user registered deliberately, and because there are a
// handful per language rather than thousands; a server comes next because it
// is the only one that actually knows the language; a Janet binding is a
// real name in a live environment; and a buffer word is a guess drawn from
// text that happens to be nearby. This is a tie-break only -- the fuzzy
// score against what the user typed decides first, in every case.
enum class CompletionSource {
    Snippet,
    Lsp,
    JanetBinding,
    BufferWord,
};

// Lower sorts first. Declared here rather than inside the merge so the
// popup's own source labels, the merge's dedup order and the session's
// ranking all read from one table.
[[nodiscard]] int CompletionSourceRank(CompletionSource source);

// A short word for the source, shown in a row's right column when the
// candidate carries no detail of its own. Empty for Lsp -- a server item
// with no detail says nothing rather than saying "lsp".
[[nodiscard]] std::string_view CompletionSourceLabel(CompletionSource source);

struct Completion {
    // What the popup row reads, and what a typed prefix is matched against.
    // filterText/sortText default to the label the way the LSP spec says a
    // client must treat an omitted one, so no consumer re-implements that
    // fallback or has to read an empty string as "match nothing".
    std::string label;
    std::string filterText;
    std::string sortText;

    // The text accepting this candidate inserts. TextMate snippet syntax
    // when isSnippet -- the accept path expands it (Editor/Snippet.h) rather
    // than inserting it raw.
    std::string insertText;
    bool        isSnippet = false;

    CompletionSource source = CompletionSource::Lsp;

    // LSP CompletionItemKind (1-25), 0 for unset -- kept as the shared kind
    // vocabulary rather than a ned-specific enum, since every source already
    // has an honest value in it (15 Snippet, 3 Function, 1 Text) and the
    // popup already buckets it down to a glyph.
    int         kind = 0;
    std::string detail;
    std::string documentation;

    // The server's own textEdit, when it sent one: the range this candidate
    // replaces. Absent for every locally-produced candidate, which falls
    // back to the caller's word-boundary prefix start instead.
    std::optional<lsp::WorkspaceTextEdit> replaceEdit;
    // Edits applied alongside the inserted text -- the "#include <vector>"
    // an accepted std::vector needs. Usually empty until
    // completionItem/resolve fills them in.
    std::vector<lsp::WorkspaceTextEdit> additionalEdits;

    // Typing one of these accepts this candidate and then inserts the
    // character. Empty means the candidate declared none, and nothing is
    // committed on -- no default set is ever substituted.
    std::vector<std::string> commitCharacters;
    // The server saying "this is the one" before anything is typed.
    bool preselect = false;

    // The item exactly as a server sent it, for completionItem/resolve.
    // Null for every source but Lsp, and for an Lsp item this client built
    // rather than parsed.
    lsp::Json raw;

    bool operator==(const Completion&) const = default;
};

// The boundary conversion: one server item becomes one candidate. Consumes
// the item -- a completion list can be thousands of them, all with strings.
[[nodiscard]] Completion FromLspItem(lsp::CompletionItem item);

// Every item of a server's response, in the order the server sent them.
[[nodiscard]] std::vector<Completion> FromLspItems(std::vector<lsp::CompletionItem> items);

// The inverse, for the one consumer that needs it: ResolveCompletionItem
// hands a server back the item it sent. Only ever called for a
// CompletionSource::Lsp candidate, whose `raw` is what actually matters --
// the rest is filled in so the wire struct is coherent if a caller reads it.
[[nodiscard]] lsp::CompletionItem ToLspItem(const Completion& completion);

} // namespace ned::editor

#endif // NED_EDITOR_COMPLETION_H
