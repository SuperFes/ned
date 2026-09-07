//
// Merge Conflict Resolution Mode: the read-back counterpart to
// Text/ThreeWayMerge.h's own marker-writing convention -- that file writes
// "<<<<<<< buffer" / "|||||||" (diff3 base, not emitted by ThreeWayMerge
// itself but understood here for markers left by `git merge`
// `--conflict-style diff3`) / "=======" / ">>>>>>> disk" markers into a
// buffer; this parses them back into a structured hunk list. Pure and
// buffer-free -- same layering HasConflictMarkers already sits at, no
// dependency on Buffer or any other part of this codebase.
//
// Tolerant of malformed/unterminated marker runs: an unclosed "<<<<<<<" (no
// "======="/">>>>>>>" before EOF or before a fresh "<<<<<<<") is silently
// dropped rather than treated as an error, matching Snippet.h's own
// "ill-formed syntax passes through as literal text" precedent -- a buffer
// that merely contains marker-shaped text (a file *about* conflict markers,
// for instance) should never throw.
//

#ifndef NED_TEXT_CONFLICTHUNK_H
#define NED_TEXT_CONFLICTHUNK_H

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ned::text {

struct ConflictHunk {
    struct Range {
        std::size_t start;
        std::size_t end;
    };

    std::size_t startByte; // the "<<<<<<< " marker's own line start
    std::size_t endByte;   // one past the ">>>>>>> " marker's line (incl. its newline)

    Range                oursRange;   // between "<<<<<<< " and the next marker
    std::optional<Range> baseRange;   // diff3 "||||||| " section, if present
    Range                theirsRange; // between "=======" and ">>>>>>> "
};

// Scans `text` for conflict-marker runs at line starts, in document order.
// A malformed/unterminated run is skipped entirely rather than reported.
[[nodiscard]] std::vector<ConflictHunk> ParseConflictHunks(std::string_view text);

} // namespace ned::text

#endif // NED_TEXT_CONFLICTHUNK_H
