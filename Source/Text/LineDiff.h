//
// diff-preview-line-diff-utility follow-up (ROADMAP "Diff preview before an
// agent edit's permission grant" / "AI-assisted editing (ACP) gaps"): a
// reusable, pure, line-granular diff primitive extracted out of
// ThreeWayMerge.cpp's own private LCS implementation (SplitLines/DiffHunks),
// which was the exact "no reusable line-diff utility yet" gap both of those
// ROADMAP items cited. ThreeWayMerge.cpp itself is refactored to build on
// this rather than keeping its own private copy -- one algorithm, two
// documented public entry points (the raw hunk list, and a rendered
// unified-diff view) instead of a private implementation detail duplicated
// wherever a line diff is needed next (AcpPanel's tool-call/permission-prompt
// diff rendering being the first such caller).
//
// No dependency on Buffer or any other part of this codebase -- same
// "composable primitive" shape ThreeWayMerge.h/KillRing.h already establish.
//

#ifndef NED_TEXT_LINEDIFF_H
#define NED_TEXT_LINEDIFF_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::text {

// Splits on '\n', each returned piece keeping its own trailing '\n' (the
// last piece has none if text doesn't end with one) -- reassembly of a
// sequence of pieces is then plain concatenation, no separator logic needed
// anywhere downstream.
[[nodiscard]] std::vector<std::string_view> SplitLines(std::string_view text);

// A base range [aStart, aStart+aCount) replaced by b[bStart, bStart+bCount) --
// aCount/bCount 0 for a pure insert/delete.
struct LineDiffHunk {
    std::size_t aStart;
    std::size_t aCount;
    std::size_t bStart;
    std::size_t bCount;

    bool operator==(const LineDiffHunk&) const = default;
};

// Trims a common prefix/suffix of matching lines, then an LCS
// dynamic-programming backtrack over the remaining core to extract a minimal
// hunk list -- a hunk is a maximal run of non-matching lines. O(m*n) over the
// trimmed core; fine for the tool-call/permission-prompt-sized diffs this is
// built for, not intended for whole-repository-scale input.
[[nodiscard]] std::vector<LineDiffHunk> DiffLines(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b);

// A rendered unified-diff-style line, trailing '\n' already stripped (each
// entry is exactly one screen row for a caller like AcpPanel).
enum class DiffLineKind { Context,
                          Added,
                          Removed,
                          // A run of unchanged lines longer than fits within
                          // contextLines on both sides of it was collapsed --
                          // `text` is a human-readable summary ("N unchanged
                          // lines"), not a real line from either input.
                          Omitted };

struct DiffLine {
    DiffLineKind kind;
    std::string  text;

    bool operator==(const DiffLine&) const = default;
};

// git diff's own "hunk" windowing: each changed run (LineDiffHunk) is shown
// with up to `contextLines` unchanged lines of context on either side;
// adjacent hunks close enough that their context windows would overlap are
// merged into one continuous run (the connecting unchanged lines shown in
// full, never elided, matching real diff tools); everything else collapses
// into a single Omitted marker. Returns an empty vector when oldText and
// newText are line-for-line identical -- "no diff" rather than an all-context
// dump.
[[nodiscard]] std::vector<DiffLine> UnifiedDiff(std::string_view oldText, std::string_view newText, std::size_t contextLines = 3);

} // namespace ned::text

#endif // NED_TEXT_LINEDIFF_H
