//
// Fuzzy (subsequence) matching over short identifier-like strings --
// currently command names (M-x, execute-extended-command follow-up), a
// foundation Phase 9's own "fuzzy file finder / command palette" wishlist
// item can build on later rather than duplicate. UI-agnostic and pure, the
// same "no dependency on Source/UI/" convention ProjectSearch.h/.cpp already
// establishes.
//

#ifndef NED_EDITOR_FUZZYMATCH_H
#define NED_EDITOR_FUZZYMATCH_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// nullopt if query's characters (case-insensitive; command names are always
// plain-ASCII kebab-case identifiers defined in this codebase's own source,
// unlike arbitrary file paths/buffer names, so no Unicode folding is needed
// here) don't all appear as an in-order subsequence of candidate. Otherwise
// a score, higher is a better match: a match starting at a word boundary
// (start-of-string, or right after '-'/'_') scores well above an incidental
// interior match, consecutive runs of matched characters score
// progressively better, and matches found with fewer skipped candidate
// characters since the previous match score better than ones found further
// away. An empty query matches every candidate with score 0 -- callers that
// want "list everything" get it for free via FuzzyFilterAndRank's
// alphabetical tie-break, with no special case needed here.
//
// Deliberately a single greedy left-to-right scan (first case-insensitive
// occurrence of each query character at or after the previous match), not a
// full alignment search: simple, O(candidate length), and for short
// kebab-case command names the greedy alignment coincides with the globally
// best one in every case that matters for this UI -- a dynamic-programming
// matcher would be over-engineering for a few dozen short candidates
// recomputed on every keystroke.
[[nodiscard]] std::optional<int> FuzzyScore(std::string_view candidate, std::string_view query);

// Filters candidates to those FuzzyScore doesn't reject, sorted by score
// descending, ties broken alphabetically -- matches this codebase's existing
// sorted-completion-output convention (e.g. CommandRegistry::Names).
[[nodiscard]] std::vector<std::string> FuzzyFilterAndRank(const std::vector<std::string>& candidates,
                                                          std::string_view                query);

} // namespace ned::editor

#endif // NED_EDITOR_FUZZYMATCH_H
