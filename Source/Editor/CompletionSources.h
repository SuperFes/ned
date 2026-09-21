//
// The four producers behind one completion popup, and the merge that puts
// them in a single list.
//
// Before this, a language server and the local sources were mutually
// exclusive: a running server meant server items only, and the buffer's own
// words, the snippet registry and Janet's bound names were reachable only
// where no server was. They answer different questions -- a server knows the
// language, a snippet is a shape the user registered, a buffer word is a
// name that exists in this file whether or not any index knows it -- so a
// popup that shows one of them at a time is a popup missing candidates.
//
// UI-free and pure, the convention DabbrevComplete.h/FuzzyMatch.h already
// establish: each collector takes text and a prefix and returns candidates.
// The caller (BufferView) supplies the prefix, decides which collectors are
// applicable to the buffer at hand, and owns the LSP round trip.
//
// One rule they all share: a local collector requires a non-empty prefix.
// These sources rank against a typed word -- without one, a trigger
// character ("." after an expression) would draw every snippet and every
// word in the file into a popup that should be showing members.
//

#ifndef NED_EDITOR_COMPLETIONSOURCES_H
#define NED_EDITOR_COMPLETIONSOURCES_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Completion.h"

namespace ned::editor {

// The buffer's own words, ranked by proximity to point (CollectDabbrevCandidates'
// own order, preserved -- nearest before point first). Capped because this is
// the one unbounded source: a large file has thousands of words matching a
// two-character prefix, and past the first handful they are noise a server
// item would have to scroll past. extraWordCharacters widens the word rule to
// match the caller's own prefix rule (see DabbrevComplete.h).
[[nodiscard]] std::vector<Completion> BufferWordCompletions(std::string_view content, std::size_t point, std::string_view prefix,
                                                            std::string_view extraWordCharacters = {},
                                                            std::size_t      maxCandidates       = 20);

// Snippet triggers visible to languageKey (its own tier plus the global
// one), fuzzy-matched against prefix. The candidate's insertText is the
// snippet *body*, flagged isSnippet so the accept path expands it rather
// than inserting it raw -- the same treatment a server's own
// insertTextFormat=2 item already gets.
//
// Unlike the other local sources an exact prefix match is kept: typing
// "for" in full and accepting still replaces it with the whole loop, which
// is the entire point of a snippet and the opposite of a buffer word that
// would only ever re-suggest what was already typed.
[[nodiscard]] std::vector<Completion> SnippetCompletions(const std::string& languageKey, std::string_view prefix);

// Janet's bound names (the caller passes them -- this layer has no
// environment), fuzzy-ranked against prefix. Exact-length matches are
// dropped for the buffer-word reason: point already sits after a complete
// name, so there is no suffix left to suggest.
[[nodiscard]] std::vector<Completion> JanetBindingCompletions(const std::vector<std::string>& names, std::string_view prefix);

// One list from several, in CompletionSourceRank order, with a label
// produced by a higher-ranked source suppressing the same label from a
// lower-ranked one -- a buffer word that merely echoes a symbol the server
// already offered is a duplicate row, not a second candidate. Duplicates
// *within* one source are kept: a server sending two "push_back" items is
// sending two real overloads, and dropping one would hide a signature.
//
// Order within a source is preserved, which is what leaves the server's own
// sortText order (and the buffer words' proximity order) intact for
// CompletionSession's stable ranking to fall back on.
[[nodiscard]] std::vector<Completion> MergeCompletions(std::vector<std::vector<Completion>> lists);

} // namespace ned::editor

#endif // NED_EDITOR_COMPLETIONSOURCES_H
