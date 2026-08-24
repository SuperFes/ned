#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/TestRun/TestOutputParser.h"

using ned::editor::testrun::BuiltInTestFormats;
using ned::editor::testrun::ParseCargoTest;
using ned::editor::testrun::ParseCatch2;
using ned::editor::testrun::ParseCtest;
using ned::editor::testrun::ParseGoTestJson;
using ned::editor::testrun::ParseJUnitXml;
using ned::editor::testrun::ParsePhpUnit;
using ned::editor::testrun::ParsePytest;
using ned::editor::testrun::ParseTestOutput;
using ned::editor::testrun::TestResult;

// Every fixture below is captured from a real tool run (2026-08: ctest 3.x,
// Catch2 v3.7.1, pytest 8.4.2, go 1.21+, current cargo) with only paths
// shortened -- except the PHPUnit fixture, which follows the documented
// console format (no local PHPUnit was available; see TestOutputParser.h).

// --- ctest ------------------------------------------------------------------

namespace {
constexpr const char* kCtestOutput = R"(Test project /home/user/fixture/bld
    Start 1: PassingTest
1/4 Test #1: PassingTest ......................   Passed    0.00 sec
    Start 2: FailingTest
2/4 Test #2: FailingTest ......................***Failed    0.25 sec

    Start 3: AnotherPass
3/4 Test #3: AnotherPass ......................   Passed    0.00 sec
    Start 4: SkippedTest
4/4 Test #4: SkippedTest ......................***Skipped   0.00 sec

75% tests passed, 1 tests failed out of 4

Total Test time (real) =   0.01 sec

The following tests did not run:
	  4 - SkippedTest (Skipped)

The following tests FAILED:
	  2 - FailingTest (Failed)
)";
} // namespace

TEST_CASE("ParseCtest reads per-test lines, statuses, and the FAILED trailer", "[TestRun]") {
    const auto outcome = ParseCtest(kCtestOutput);

    REQUIRE(outcome.parsedOk);
    REQUIRE_FALSE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 4);
    REQUIRE(outcome.passed == 2);
    REQUIRE(outcome.failed == 1);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "PassingTest");
    REQUIRE(outcome.results[0].status == TestResult::Status::Passed);
    REQUIRE(outcome.results[0].file.empty());

    REQUIRE(outcome.results[1].name == "FailingTest");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].message == "Failed");
    REQUIRE(outcome.results[1].durationMs == 250.0);

    REQUIRE(outcome.results[3].name == "SkippedTest");
    REQUIRE(outcome.results[3].status == TestResult::Status::Skipped);
}

TEST_CASE("ParseCtest maps Timeout and Exception statuses to Failed", "[TestRun]") {
    const auto outcome = ParseCtest("1/2 Test #1: SlowTest .........................***Timeout   30.01 sec\n"
                                    "2/2 Test #2: CrashTest ........................***Exception: SegFault  0.10 sec\n");
    REQUIRE(outcome.results.size() == 2);
    REQUIRE(outcome.results[0].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[0].message == "Timeout");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].message == "Exception: SegFault");
}

TEST_CASE("ParseCtest on unrelated output reports parsedOk false", "[TestRun]") {
    const auto outcome = ParseCtest("hello world\nnothing to see here\n");
    REQUIRE_FALSE(outcome.parsedOk);
    REQUIRE(outcome.results.empty());
}

// --- Catch2 -----------------------------------------------------------------

namespace {
constexpr const char* kCatch2Rule79Dashes =
    "-------------------------------------------------------------------------------";
constexpr const char* kCatch2Rule79Dots =
    "...............................................................................";

const std::string kCatch2Output = std::string("Randomness seeded to: 3817138800\n\n") +
                                  "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" +
                                  "fixture is a Catch2 v3.7.1 host application.\n" +
                                  "Run with -? for options\n\n" + kCatch2Rule79Dashes + "\n" +
                                  "Addition fails\n" + kCatch2Rule79Dashes + "\n" +
                                  "catch2fixture.cpp:7\n" + kCatch2Rule79Dots + "\n\n" +
                                  "catch2fixture.cpp:9: FAILED:\n" +
                                  "  CHECK( a == 2 )\n" +
                                  "with expansion:\n" +
                                  "  1 == 2\n\n" +
                                  "catch2fixture.cpp:10: FAILED:\n" +
                                  "  CHECK( a == 3 )\n" +
                                  "with expansion:\n" +
                                  "  1 == 3\n\n" + kCatch2Rule79Dashes + "\n" +
                                  "Sections here\n" +
                                  "  bad part\n" + kCatch2Rule79Dashes + "\n" +
                                  "catch2fixture.cpp:17\n" + kCatch2Rule79Dots + "\n\n" +
                                  "catch2fixture.cpp:18: FAILED:\n" +
                                  "  REQUIRE( 1 == 0 )\n\n" +
                                  "===============================================================================\n" +
                                  "test cases: 3 | 1 passed | 2 failed\n" +
                                  "assertions: 5 | 2 passed | 3 failed\n";
} // namespace

TEST_CASE("ParseCatch2 reads failure blocks with the test case's own location", "[TestRun]") {
    const auto outcome = ParseCatch2(kCatch2Output);

    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 2);

    REQUIRE(outcome.results[0].name == "Addition fails");
    REQUIRE(outcome.results[0].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[0].file == "catch2fixture.cpp");
    REQUIRE(outcome.results[0].line == 7);
    REQUIRE(outcome.results[0].message == "CHECK( a == 2 ) with expansion: 1 == 2");

    REQUIRE(outcome.results[1].name == "Sections here");
    REQUIRE(outcome.results[1].line == 17);
    REQUIRE(outcome.results[1].message == "REQUIRE( 1 == 0 )");

    // Counts come from the summary -- the passed test never appears by name.
    REQUIRE(outcome.passed == 1);
    REQUIRE(outcome.failed == 2);
}

TEST_CASE("ParseCatch2 reads the all-passed summary shape", "[TestRun]") {
    const auto outcome = ParseCatch2("Randomness seeded to: 1\n"
                                     "All tests passed (5 assertions in 3 test cases)\n");
    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.results.empty());
    REQUIRE(outcome.passed == 3);
    REQUIRE(outcome.failed == 0);
}

TEST_CASE("ParseCatch2 on unrelated output reports parsedOk false", "[TestRun]") {
    REQUIRE_FALSE(ParseCatch2("make: *** No rule to make target 'all'.\n").parsedOk);
}

// --- pytest -----------------------------------------------------------------

namespace {
constexpr const char* kPytestVerboseOutput = R"(============================= test session starts ==============================
platform linux -- Python 3.13.15, pytest-8.4.2, pluggy-1.6.0 -- /usr/bin/python3.13
cachedir: .pytest_cache
rootdir: /home/user/fixture
collecting ... collected 7 items

test_sample.py::test_ok PASSED                                           [ 14%]
test_sample.py::test_fails FAILED                                        [ 28%]
test_sample.py::test_skipped SKIPPED (not ready)                         [ 42%]
test_sample.py::test_param[1] PASSED                                     [ 57%]
test_sample.py::test_param[2] FAILED                                     [ 71%]
test_sample.py::TestThings::test_method_ok PASSED                        [ 85%]
test_sample.py::TestThings::test_method_fails FAILED                     [100%]

=================================== FAILURES ===================================
__________________________________ test_fails __________________________________

    def test_fails():
>       assert 1 == 2, "one is not two"
E       AssertionError: one is not two
E       assert 1 == 2

test_sample.py:7: AssertionError
________________________________ test_param[2] _________________________________

n = 2

    @pytest.mark.parametrize("n", [1, 2])
    def test_param(n):
>       assert n < 2
E       assert 2 < 2

test_sample.py:15: AssertionError
_________________________ TestThings.test_method_fails _________________________

self = <test_sample.TestThings object at 0x7f5d38f4a850>

    def test_method_fails(self):
>       raise RuntimeError("boom")
E       RuntimeError: boom

test_sample.py:22: RuntimeError
=========================== short test summary info ============================
FAILED test_sample.py::test_fails - AssertionError: one is not two
FAILED test_sample.py::test_param[2] - assert 2 < 2
FAILED test_sample.py::TestThings::test_method_fails - RuntimeError: boom
==================== 3 failed, 3 passed, 1 skipped in 0.03s ====================
)";
} // namespace

TEST_CASE("ParsePytest verbose output yields every test with locations for failures", "[TestRun]") {
    const auto outcome = ParsePytest(kPytestVerboseOutput);

    REQUIRE(outcome.parsedOk);
    REQUIRE_FALSE(outcome.failuresOnly); // verbose lines name passing tests too
    REQUIRE(outcome.results.size() == 7);
    REQUIRE(outcome.passed == 3);
    REQUIRE(outcome.failed == 3);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "test_ok");
    REQUIRE(outcome.results[0].status == TestResult::Status::Passed);
    REQUIRE(outcome.results[0].file == "test_sample.py");

    REQUIRE(outcome.results[1].name == "test_fails");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].line == 7);
    REQUIRE(outcome.results[1].message == "AssertionError: one is not two");

    REQUIRE(outcome.results[2].name == "test_skipped");
    REQUIRE(outcome.results[2].status == TestResult::Status::Skipped);
    REQUIRE(outcome.results[2].message == "not ready");

    REQUIRE(outcome.results[4].name == "test_param[2]");
    REQUIRE(outcome.results[4].line == 15);

    // Class-scoped test: node id spells it "::", the FAILURES header "." --
    // the location must still attach.
    REQUIRE(outcome.results[6].name == "TestThings::test_method_fails");
    REQUIRE(outcome.results[6].line == 22);
    REQUIRE(outcome.results[6].message == "RuntimeError: boom");
}

TEST_CASE("ParsePytest non-verbose output degrades to failures-only with locations", "[TestRun]") {
    constexpr const char* kPlain  = R"(============================= test session starts ==============================
collected 7 items

test_sample.py .Fs.F.F                                                   [100%]

=================================== FAILURES ===================================
__________________________________ test_fails __________________________________

    def test_fails():
>       assert 1 == 2, "one is not two"
E       AssertionError: one is not two

test_sample.py:7: AssertionError
=========================== short test summary info ============================
FAILED test_sample.py::test_fails - AssertionError: one is not two
==================== 1 failed, 5 passed, 1 skipped in 0.03s ====================
)";
    const auto            outcome = ParsePytest(kPlain);

    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 1);
    REQUIRE(outcome.results[0].name == "test_fails");
    REQUIRE(outcome.results[0].file == "test_sample.py");
    REQUIRE(outcome.results[0].line == 7);
    // Counts come from the final summary line, not the results list.
    REQUIRE(outcome.passed == 5);
    REQUIRE(outcome.failed == 1);
    REQUIRE(outcome.skipped == 1);
}

// --- go test -json ----------------------------------------------------------

namespace {
constexpr const char* kGoJsonOutput = R"(go: downloading nothing
{"Time":"2026-08-24T14:27:01Z","Action":"start","Package":"fixture"}
{"Time":"2026-08-24T14:27:01Z","Action":"run","Package":"fixture","Test":"TestOk"}
{"Time":"2026-08-24T14:27:01Z","Action":"output","Package":"fixture","Test":"TestOk","Output":"=== RUN   TestOk\n"}
{"Time":"2026-08-24T14:27:01Z","Action":"pass","Package":"fixture","Test":"TestOk","Elapsed":0.5}
{"Time":"2026-08-24T14:27:01Z","Action":"run","Package":"fixture","Test":"TestFails"}
{"Time":"2026-08-24T14:27:01Z","Action":"output","Package":"fixture","Test":"TestFails","Output":"    calc_test.go:12: expected 2, got 3\n"}
{"Time":"2026-08-24T14:27:01Z","Action":"output","Package":"fixture","Test":"TestFails","Output":"--- FAIL: TestFails (0.00s)\n"}
{"Time":"2026-08-24T14:27:01Z","Action":"fail","Package":"fixture","Test":"TestFails","Elapsed":0}
{"Time":"2026-08-24T14:27:01Z","Action":"output","Package":"fixture","Test":"TestSkipped","Output":"    calc_test.go:16: not ready\n"}
{"Time":"2026-08-24T14:27:01Z","Action":"skip","Package":"fixture","Test":"TestSkipped","Elapsed":0}
{"Time":"2026-08-24T14:27:01Z","Action":"run","Package":"fixture","Test":"TestSub/child_fail"}
{"Time":"2026-08-24T14:27:01Z","Action":"output","Package":"fixture","Test":"TestSub/child_fail","Output":"    calc_test.go:21: nested failure\n"}
{"Time":"2026-08-24T14:27:01Z","Action":"fail","Package":"fixture","Test":"TestSub/child_fail","Elapsed":0}
{"Time":"2026-08-24T14:27:01Z","Action":"fail","Package":"fixture","Elapsed":0.003}
)";
} // namespace

TEST_CASE("ParseGoTestJson reads pass/fail/skip events with scraped locations", "[TestRun]") {
    const auto outcome = ParseGoTestJson(kGoJsonOutput);

    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.results.size() == 4); // the package-level fail event carries no Test key
    REQUIRE(outcome.passed == 1);
    REQUIRE(outcome.failed == 2);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "TestOk");
    REQUIRE(outcome.results[0].durationMs == 500.0);

    REQUIRE(outcome.results[1].name == "TestFails");
    REQUIRE(outcome.results[1].file == "calc_test.go");
    REQUIRE(outcome.results[1].line == 12);
    REQUIRE(outcome.results[1].message == "expected 2, got 3");

    REQUIRE(outcome.results[3].name == "TestSub/child_fail");
    REQUIRE(outcome.results[3].line == 21);
}

TEST_CASE("ParseGoTestJson disambiguates the same test name across packages", "[TestRun]") {
    const auto outcome = ParseGoTestJson(
        R"({"Action":"pass","Package":"mod/alpha","Test":"TestThing","Elapsed":0}
{"Action":"fail","Package":"mod/beta","Test":"TestThing","Elapsed":0}
)");
    REQUIRE(outcome.results.size() == 2);
    REQUIRE(outcome.results[0].name == "alpha.TestThing");
    REQUIRE(outcome.results[1].name == "beta.TestThing");
}

// --- cargo test -------------------------------------------------------------

namespace {
constexpr const char* kCargoOutput = R"(   Compiling fixture v0.1.0 (/home/user/fixture)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.15s
     Running unittests src/lib.rs (target/debug/deps/fixture-73c034423271741b)

running 4 tests
test tests::ignored_test ... ignored
test tests::it_works ... ok
test tests::it_fails ... FAILED
test tests::panics_plain ... FAILED

failures:

---- tests::it_fails stdout ----

thread 'tests::it_fails' (1459540) panicked at src/lib.rs:11:21:
assertion `left == right` failed: custom message
  left: 2
 right: 3
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- tests::panics_plain stdout ----

thread 'tests::panics_plain' (1459542) panicked at src/lib.rs:14:25:
explicit panic text


failures:
    tests::it_fails
    tests::panics_plain

test result: FAILED. 1 passed; 2 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.00s
)";
} // namespace

TEST_CASE("ParseCargoTest reads statuses and panic locations", "[TestRun]") {
    const auto outcome = ParseCargoTest(kCargoOutput);

    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.results.size() == 4);
    REQUIRE(outcome.passed == 1);
    REQUIRE(outcome.failed == 2);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "tests::ignored_test");
    REQUIRE(outcome.results[0].status == TestResult::Status::Skipped);

    REQUIRE(outcome.results[2].name == "tests::it_fails");
    REQUIRE(outcome.results[2].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[2].file == "src/lib.rs");
    REQUIRE(outcome.results[2].line == 11); // the line, not the 21 column
    REQUIRE(outcome.results[2].message == "assertion `left == right` failed: custom message");

    REQUIRE(outcome.results[3].line == 14);
    REQUIRE(outcome.results[3].message == "explicit panic text");
}

TEST_CASE("ParseCargoTest handles the pre-1.73 quoted panic shape", "[TestRun]") {
    const auto outcome = ParseCargoTest("running 1 test\n"
                                        "test tests::old_style ... FAILED\n"
                                        "\n"
                                        "failures:\n"
                                        "\n"
                                        "---- tests::old_style stdout ----\n"
                                        "thread 'tests::old_style' panicked at 'boom happened', src/old.rs:5:9\n"
                                        "\n"
                                        "test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out\n");
    REQUIRE(outcome.results.size() == 1);
    REQUIRE(outcome.results[0].message == "boom happened");
    REQUIRE(outcome.results[0].file == "src/old.rs");
    REQUIRE(outcome.results[0].line == 5);
}

// --- JUnit XML --------------------------------------------------------------

TEST_CASE("ParseJUnitXml reads testcases with failure/skip children and entities", "[TestRun]") {
    constexpr const char* kXml    = R"(<?xml version="1.0" encoding="utf-8"?>
<testsuites name="pytest tests">
<testsuite name="pytest" errors="0" failures="1" skipped="1" tests="4" time="0.026">
<testcase classname="test_sample" name="test_ok" time="0.500" />
<testcase classname="test_sample" name="test_fails" file="tests/test_sample.py" line="6" time="0.001"><failure message="AssertionError: one is not two&#10;assert 1 == 2">def test_fails():
&gt;       assert 1 == 2
</failure></testcase>
<testcase classname="test_sample" name="test_skipped" time="0.000"><skipped type="pytest.skip" message="not ready"/></testcase>
<testcase classname="test_sample.TestThings" name="test_cdata" time="0.000"><error><![CDATA[raw <error> text
second line]]></error></testcase>
</testsuite>
</testsuites>
)";
    const auto            outcome = ParseJUnitXml(kXml);

    REQUIRE(outcome.parsedOk);
    REQUIRE_FALSE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 4);
    REQUIRE(outcome.passed == 1);
    REQUIRE(outcome.failed == 2);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "test_sample::test_ok");
    REQUIRE(outcome.results[0].status == TestResult::Status::Passed);
    REQUIRE(outcome.results[0].durationMs == 500.0);

    REQUIRE(outcome.results[1].name == "test_sample::test_fails");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].file == "tests/test_sample.py");
    REQUIRE(outcome.results[1].line == 6);
    REQUIRE(outcome.results[1].message == "AssertionError: one is not two"); // first line of the decoded message attr

    REQUIRE(outcome.results[2].status == TestResult::Status::Skipped);
    REQUIRE(outcome.results[2].message == "not ready");

    REQUIRE(outcome.results[3].name == "test_sample.TestThings::test_cdata");
    REQUIRE(outcome.results[3].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[3].message == "raw <error> text"); // CDATA preserved verbatim
}

TEST_CASE("ParseJUnitXml on non-XML input reports parsedOk false", "[TestRun]") {
    REQUIRE_FALSE(ParseJUnitXml("plain text, no xml at all").parsedOk);
}

// --- PHPUnit ----------------------------------------------------------------

TEST_CASE("ParsePhpUnit reads failure blocks and the counted summary", "[TestRun]") {
    constexpr const char* kPhpUnit = R"(PHPUnit 10.5.20 by Sebastian Bergmann and contributors.

Runtime:       PHP 8.3.1

FF...S.                                                             7 / 7 (100%)

Time: 00:00.042, Memory: 8.00 MB

There were 2 failures:

1) Tests\FooTest::testBar
Failed asserting that 1 matches expected 2.

/home/user/project/tests/FooTest.php:42

2) Tests\FooTest::testBaz with data set #0 (1, 2)
Failed asserting that false is true.

/home/user/project/tests/FooTest.php:57

There was 1 skipped test:

1) Tests\FooTest::testLater
Not implemented yet.

/home/user/project/tests/FooTest.php:63

FAILURES!
Tests: 7, Assertions: 9, Failures: 2, Skipped: 1.
)";
    const auto            outcome  = ParsePhpUnit(kPhpUnit);

    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 3);
    REQUIRE(outcome.passed == 4); // 7 total - 2 failed - 1 skipped
    REQUIRE(outcome.failed == 2);
    REQUIRE(outcome.skipped == 1);

    REQUIRE(outcome.results[0].name == "Tests\\FooTest::testBar");
    REQUIRE(outcome.results[0].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[0].file == "/home/user/project/tests/FooTest.php");
    REQUIRE(outcome.results[0].line == 42);
    REQUIRE(outcome.results[0].message == "Failed asserting that 1 matches expected 2.");

    // The data-set suffix is stripped so parameterized instances aggregate.
    REQUIRE(outcome.results[1].name == "Tests\\FooTest::testBaz");

    REQUIRE(outcome.results[2].name == "Tests\\FooTest::testLater");
    REQUIRE(outcome.results[2].status == TestResult::Status::Skipped);
}

TEST_CASE("ParsePhpUnit reads the all-OK summary", "[TestRun]") {
    const auto outcome = ParsePhpUnit("PHPUnit 10.5.20 by Sebastian Bergmann and contributors.\n"
                                      "\n"
                                      ".......                                                             7 / 7 (100%)\n"
                                      "\n"
                                      "OK (7 tests, 12 assertions)\n");
    REQUIRE(outcome.parsedOk);
    REQUIRE(outcome.results.empty());
    REQUIRE(outcome.passed == 7);
}

// --- dispatch ---------------------------------------------------------------

TEST_CASE("ParseTestOutput dispatches by format name and rejects unknown names", "[TestRun]") {
    const auto outcome = ParseTestOutput("ctest", kCtestOutput);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->format == "ctest");
    REQUIRE(outcome->results.size() == 4);

    REQUIRE_FALSE(ParseTestOutput("no-such-format", "whatever").has_value());
}

TEST_CASE("BuiltInTestFormats lists every dispatchable format", "[TestRun]") {
    for (const std::string& format : BuiltInTestFormats()) {
        REQUIRE(ParseTestOutput(format, "").has_value());
    }
}

TEST_CASE("Every parser tolerates empty input as parsedOk false", "[TestRun]") {
    for (const std::string& format : BuiltInTestFormats()) {
        const auto outcome = ParseTestOutput(format, "");
        REQUIRE(outcome.has_value());
        REQUIRE_FALSE(outcome->parsedOk);
        REQUIRE(outcome->results.empty());
    }
}
