//
// Code-coverage gutter (ROADMAP.md's Maybelist entry): parses lcov's `.info`
// trace-file format (SF:/DA:/BRDA:/end_of_record) -- the common export
// target for `lcov` itself, `llvm-cov export -format=lcov`, and
// `gcovr --lcov`, so this one parser covers both the gcc/gcov and
// Clang/llvm-cov toolchains without a second format-specific
// implementation. Raw per-source-file `.gcov` text output is deliberately
// not a second input format here -- it needs a directory scan (one file per
// translation unit) rather than a single document, a different shape of
// problem than every other parser in this codebase (TestOutputParser.h,
// SanitizerOutputParser.h, ValgrindOutputParser.h all consume one blob of
// text) -- worth adding only if lcov's own `.info` format proves
// insufficient in practice.
//

#ifndef NED_EDITOR_COVERAGE_COVERAGEOUTPUTPARSER_H
#define NED_EDITOR_COVERAGE_COVERAGEOUTPUTPARSER_H

#include <string_view>

#include "CoverageReport.h"

namespace ned::editor::coverage {

// Pure, never throws: unrecognized lines are skipped, a document with no
// SF:/end_of_record pair at all yields an empty report -- the same
// "unrecognized input is not an error" contract every parser in this
// codebase follows. Multiple SF: blocks for the same path within one
// document (multiple TN: test sections covering the same source, e.g. as
// `lcov -a` combining several runs already produces) are merged: per-line
// hit counts and branch tallies accumulate across every block rather than
// the later block replacing the earlier one. A record missing its own
// trailing end_of_record (a truncated capture) is still flushed at
// end-of-input rather than silently dropped.
[[nodiscard]] CoverageReport ParseLcovInfo(std::string_view output);

} // namespace ned::editor::coverage

#endif // NED_EDITOR_COVERAGE_COVERAGEOUTPUTPARSER_H
