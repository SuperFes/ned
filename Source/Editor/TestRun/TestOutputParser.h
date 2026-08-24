//
// Built-in test-output parsers (structured test-runner integration, see
// TestResult.h). Pure free functions over a captured output blob -- no
// buffers, no processes, no Janet -- so every format is unit-testable
// against raw fixture text. Each parser tolerates interleaved unrecognized
// lines (a test binary's own stdout, build noise) by simply skipping them;
// a blob that matches nothing of the format at all comes back with
// parsedOk = false rather than an error.
//
// Format specifications were pinned against real tool output captured
// 2026-08 (ctest 3.x, Catch2 v3.7, pytest 8.4, go 1.21+ test -json,
// current cargo test, pytest's JUnit XML). PHPUnit's parser follows the
// documented console format (9.x-11.x shapes both handled) -- flagged
// because no local PHPUnit was available to capture from; adjust against
// live output if a discrepancy shows up.
//
// Janet-registered parsers are resolved by TestRunner via TestRunConfig's
// registry, not here -- this file stays Janet-free.
//

#ifndef NED_EDITOR_TESTRUN_TESTOUTPUTPARSER_H
#define NED_EDITOR_TESTRUN_TESTOUTPUTPARSER_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TestResult.h"

namespace ned::editor::testrun {

// ctest: "N/M Test #N: name ....   Passed|***Failed|***Skipped|...  T sec"
// lines plus the "The following tests FAILED:" trailer for failure reasons.
// Never knows file/line. Counts come from the per-test lines, not the "N%
// tests passed" summary (ctest counts skipped tests as passed there).
[[nodiscard]] TestRunOutcome ParseCtest(std::string_view output);

// Catch2 console reporter (v2/v3): 79-dash failure blocks (name, then the
// test case's own "path:line", then "path:line: FAILED:" assertions).
// failuresOnly -- passing test cases never appear by name; the passed count
// comes from the "test cases: N | ..." / "All tests passed (...)" summary.
[[nodiscard]] TestRunOutcome ParseCatch2(std::string_view output);

// pytest: verbose "file::node STATUS" lines when run with -v (recommended --
// that's what makes per-test pass marks possible), the FAILURES section for
// line numbers, and the short-summary FAILED lines for messages; without -v
// it degrades to failures-only.
[[nodiscard]] TestRunOutcome ParsePytest(std::string_view output);

// go test -json: one JSON object per line; pass/fail/skip actions carrying a
// "Test" key finalize a result, file:line scraped from that test's own
// accumulated output ("foo_test.go:12: msg" -- basename-only, a documented
// go limitation). Non-JSON lines (build errors) are skipped.
[[nodiscard]] TestRunOutcome ParseGoTestJson(std::string_view output);

// cargo test: "test name ... ok|FAILED|ignored" lines plus "---- name
// stdout ----" panic blocks (both the pre- and post-1.73 "panicked at"
// shapes); "test result:" summaries are summed across the several test
// binaries one cargo invocation runs.
[[nodiscard]] TestRunOutcome ParseCargoTest(std::string_view output);

// JUnit XML (the lingua franca file format most frameworks can emit) via a
// small hand-rolled, tolerant scanner -- deliberately not a real XML parser
// (no XML dependency exists in this codebase, and <testcase> attribute
// scanning doesn't need one). Handles both quote styles, self-closing and
// bodied testcases, nested <failure>/<error>/<skipped>, CDATA, and the
// standard entities. Typically consumed via TestRunConfig's results-file
// setting since frameworks write it to a file, not stdout.
[[nodiscard]] TestRunOutcome ParseJUnitXml(std::string_view output);

// PHPUnit console: "N) Class::testMethod" blocks under the "There were N
// failures/errors/skipped tests:" sections, message lines, then the
// "/path/File.php:NN" trace (first frame taken). failuresOnly, like Catch2.
[[nodiscard]] TestRunOutcome ParsePhpUnit(std::string_view output);

// Dispatch by format name -- "ctest", "catch2", "pytest", "go-json",
// "cargo", "junit-xml", "phpunit". nullopt for an unknown name (the caller
// reports it; a Janet-registered parser under that name is TestRunner's
// business, resolved before ever asking here).
[[nodiscard]] std::optional<TestRunOutcome> ParseTestOutput(std::string_view format, std::string_view output);

[[nodiscard]] std::vector<std::string> BuiltInTestFormats();

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTOUTPUTPARSER_H
