//
// Structured test-runner integration (ROADMAP.md): the common vocabulary a
// test framework's output is parsed into, shared by every built-in parser
// (TestOutputParser.h), Janet-registered parsers (TestRunConfig.h), the
// "*test results*" failures buffer, and BufferView's per-test gutter marks.
// Deliberately framework-neutral: `name` is kept exactly as the framework
// itself reported it ("tests::it_fails", "Class::testMethod", "TestFoo/sub",
// "test_param[2]") -- the gutter's name matching, not this struct, owns the
// policy for relating those shapes to a discovered test definition.
//

#ifndef NED_EDITOR_TESTRUN_TESTRESULT_H
#define NED_EDITOR_TESTRUN_TESTRESULT_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::testrun {

struct TestResult {
    std::string name;
    enum class Status { Passed,
                        Failed,
                        Skipped } status = Status::Failed;
    // Source location as the framework reported it -- possibly relative to
    // the run's working directory, possibly a bare basename (go test), and
    // empty/0 when the framework simply doesn't know (ctest never does).
    std::string file;
    std::size_t line = 0; // 1-based; 0 = unknown
    // First meaningful failure line, single-line -- what the results buffer
    // shows after the test's name.
    std::string message;
    double      durationMs = 0.0; // 0 = unreported

    [[nodiscard]] bool operator==(const TestResult&) const = default;
};

struct TestRunOutcome {
    std::vector<TestResult> results;
    // Counts come from the tool's own summary where the per-test list can't
    // supply them (a failures-only format knows how many passed but not
    // which), else are counted from results.
    std::size_t passed  = 0;
    std::size_t failed  = 0;
    std::size_t skipped = 0;
    std::string format; // which parser produced this ("ctest", "pytest", ...)
    // True for formats whose output names failures (and skips) but never
    // passing tests (catch2/phpunit console) -- the gutter's cue that "no
    // result entry for a discovered test" may mean "passed" rather than
    // "not run", but only for an unfiltered, successfully parsed run.
    bool failuresOnly = false;
    // False = the output didn't look like this format at all (no per-test
    // lines and no summary recognized) -- callers report a parse failure
    // instead of showing a misleading all-zero outcome.
    bool parsedOk = false;
};

// True when a result the runner parsed refers to the discovered test
// definition named markerName -- the gutter's matching rule, pure and
// separately testable. Frameworks qualify names in ways a query can't see
// ("Class::method", "pkg.TestName", "Suite.Name", "test_x[param]",
// "TestSub/child"), so beyond exact equality this accepts: the result name
// with a "[...]" parameter suffix stripped; a "marker/..." subtest child
// (aggregating onto the parent definition); and the result's trailing
// segment (after the last "::", ".", or "#") equaling the
// bare marker name.
[[nodiscard]] bool MatchesTestName(std::string_view markerName, std::string_view resultName);

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTRESULT_H
