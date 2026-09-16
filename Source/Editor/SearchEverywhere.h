//
// The merged Actions/Files/Buffers candidate pool behind the
// search-everywhere popup (a command by name, a named keyboard macro
// (MacroRegistry.h) by name, a project file by relative path, an open
// buffer by name). Pure and UI-free -- FuzzyMatch.h's own convention -- and
// deliberately does not reuse FuzzyFilterAndRank: that function takes and
// returns bare std::string and re-sorts ties alphabetically, discarding
// which source a candidate came from and any secondary text (a file's
// path, a command's doc string) worth showing beside it. This calls
// FuzzyScore directly per candidate and keeps that provenance through its
// own sort instead.
//

#ifndef NED_EDITOR_SEARCHEVERYWHERE_H
#define NED_EDITOR_SEARCHEVERYWHERE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

enum class SearchEverywhereKind {
    Command,
    Macro,
    File,
    Buffer,
};

struct SearchEverywhereCandidate {
    SearchEverywhereKind kind;
    std::string          label;  // fuzzy-matched against
    std::string          detail; // shown dimmed alongside label: a doc string, a relative path, or ""
};

struct SearchEverywhereResult {
    std::size_t candidateIndex; // index into the candidates vector RankSearchEverywhere was called with
    int         score;
};

// FuzzyScore per candidate's label, filtered to kindFilter when set (nullopt
// means "All"). Sorted score descending; ties break first by kind (in
// SearchEverywhereKind's own declaration order: Command, Macro, File,
// Buffer -- keeps same-scored rows grouped by kind rather than interleaved),
// then by label alphabetically. An empty query matches every candidate with
// score 0, same as FuzzyScore's own convention -- "All" with an empty query
// lists everything, grouped by kind.
[[nodiscard]] std::vector<SearchEverywhereResult> RankSearchEverywhere(
    const std::vector<SearchEverywhereCandidate>& candidates, std::string_view query,
    std::optional<SearchEverywhereKind> kindFilter);

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHEVERYWHERE_H
