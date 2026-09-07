//
// Code-coverage gutter (ROADMAP.md's Maybelist entry): the framework-neutral
// data model a coverage report parser (CoverageOutputParser.h) produces and
// BufferView's coverage gutter consumes, TestResult.h's own split from
// TestOutputParser.h.
//

#ifndef NED_EDITOR_COVERAGE_COVERAGEREPORT_H
#define NED_EDITOR_COVERAGE_COVERAGEREPORT_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ned::editor::coverage {

enum class LineStatus { Covered,
                        Partial,
                        Uncovered };

struct LineCoverage {
    std::size_t line = 0; // 0-based

    std::size_t hitCount = 0;

    // 0 when the source carries no BRDA: branch records for this line at
    // all (line-only coverage, or a tool that doesn't emit branch data) --
    // Status() then falls back to plain hit/no-hit.
    std::size_t branchesTotal = 0;
    std::size_t branchesTaken = 0;

    // Covered: executed, and either no branch data or every branch taken.
    // Partial: executed, but at least one branch on this line was never
    // taken (lcov's own genhtml "partial" highlight). Uncovered: never
    // executed at all, regardless of branch data.
    [[nodiscard]] LineStatus Status() const {
        if (hitCount == 0) {
            return LineStatus::Uncovered;
        }
        if (branchesTotal > 0 && branchesTaken < branchesTotal) {
            return LineStatus::Partial;
        }
        return LineStatus::Covered;
    }

    [[nodiscard]] bool operator==(const LineCoverage&) const = default;
};

struct FileCoverage {
    std::string               path;  // verbatim SF: value -- absolute or relative to wherever the tool ran, not normalized here
    std::vector<LineCoverage> lines; // sorted by line, unique per line

    [[nodiscard]] bool operator==(const FileCoverage&) const = default;
};

using CoverageReport = std::vector<FileCoverage>;

// Best-effort match of an open buffer's own path against the report's own
// SF: path spellings -- an lcov .info's SF: value is whatever path the
// instrumented build's debug info carried (often absolute, from the
// compiler's own invocation directory; sometimes relative to wherever the
// coverage tool was run), never guaranteed to already match however this
// buffer's path happens to be spelled. Tries, in order: every entry
// resolved (relative SF: paths resolved against projectRoot first) and
// compared via weakly_canonical against bufferPath (BufferList::FindByPath's
// own NormalizedPathKey precedent); falling back to a unique-by-filename
// match among the report's own entries (EnsureTestGutterCache's own
// bufferBasename fallback) when exactly one entry shares the buffer's
// filename. Returns nullptr when nothing matches -- including an ambiguous
// filename shared by more than one entry, where silently picking the wrong
// file's coverage would be worse than showing none.
[[nodiscard]] const FileCoverage* FindFileCoverage(const CoverageReport& report, const std::filesystem::path& bufferPath,
                                                   const std::filesystem::path& projectRoot);

} // namespace ned::editor::coverage

#endif // NED_EDITOR_COVERAGE_COVERAGEREPORT_H
