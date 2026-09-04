#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include "Editor/DiagnosticsLog.h"
#include "Editor/TestRun/TestRunConfig.h"
#include "Editor/TestRun/TestRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::LogCategory;
using ned::editor::LogEntries;
using ned::editor::ResetDiagnosticsLogForTesting;
using ned::editor::testrun::SetTestCommand;
using ned::editor::testrun::SetTestFilterCommand;
using ned::editor::testrun::SetTestResultsFile;
using ned::editor::testrun::TestOutputBufferName;
using ned::editor::testrun::TestResult;
using ned::editor::testrun::TestRunner;
using ned::text::Buffer;
using ned::text::BufferList;

// Same convention as TaskRunnerTest.cpp: a real ned::ui::EventLoop is
// constructed but its Run() loop never started, so a real spawned process's
// posted callbacks never fire inside these tests. The parse-on-exit logic
// is driven directly through TestRunner's public-for-tests
// DispatchProcessOutput/DispatchProcessExit instead (TaskProcess's own
// DispatchOutput/DispatchExit precedent).

namespace {

// Every test that configures the process-wide TestRunConfig statics cleans
// up through this guard so a failure can't leak state into later tests.
struct ConfigResetGuard {
    ~ConfigResetGuard() {
        SetTestCommand({}, "");
        SetTestFilterCommand({});
        SetTestResultsFile("");
    }
};

constexpr const char* kCtestFixture = "1/2 Test #1: PassingTest ......................   Passed    0.00 sec\n"
                                      "2/2 Test #2: FailingTest ......................***Failed    0.10 sec\n"
                                      "50% tests passed, 1 tests failed out of 2\n";

} // namespace

TEST_CASE("TestOutputBufferName follows the bracket-naming convention", "[TestRun]") {
    REQUIRE(TestOutputBufferName() == "*test output*");
}

TEST_CASE("RunAll without a configured command reports the gap in the output buffer", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);

    Buffer* buffer = runner.RunAll();
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->ReadOnly());
    REQUIRE(buffer->Text().find("No test command configured") != std::string::npos);
    REQUIRE_FALSE(runner.IsRunning());
    REQUIRE_FALSE(runner.LatestOutcome().has_value());
}

TEST_CASE("A sanitizer report in the run's output is logged to DiagnosticsLog with its source location", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    ResetDiagnosticsLogForTesting();

    runner.DispatchProcessOutput("2/2 Test #2: SomeTest ......................***Failed    0.00 sec\n"
                                  "==12345==ERROR: AddressSanitizer: heap-use-after-free on address 0x1\n"
                                  "SUMMARY: AddressSanitizer: heap-use-after-free /a/file.cpp:12:3 in main\n"
                                  "0% tests passed, 1 tests failed out of 1\n");
    runner.DispatchProcessExit(1);

    const auto entries = LogEntries();
    const auto it       = std::ranges::find_if(entries, [](const auto& e) { return e.category == LogCategory::Subprocess; });
    REQUIRE(it != entries.end());
    CHECK(it->message.find("AddressSanitizer") != std::string::npos);
    CHECK(it->message.find("heap-use-after-free") != std::string::npos);
    REQUIRE(it->path.has_value());
    CHECK(*it->path == "/a/file.cpp");
    REQUIRE(it->line.has_value());
    CHECK(*it->line == 12);

    ResetDiagnosticsLogForTesting();
}

TEST_CASE("Sanitizer-free output logs nothing to DiagnosticsLog", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    ResetDiagnosticsLogForTesting();

    runner.DispatchProcessOutput(kCtestFixture);
    runner.DispatchProcessExit(0);

    const auto entries = LogEntries();
    CHECK(std::ranges::none_of(entries, [](const auto& e) { return e.category == LogCategory::Subprocess; }));

    ResetDiagnosticsLogForTesting();
}

TEST_CASE("RunFiltered without a filter template reports the gap in the output buffer", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);

    Buffer* buffer = runner.RunFiltered("SomeTest", "some/file.cpp");
    REQUIRE(buffer->Text().find("No test filter command configured") != std::string::npos);
    REQUIRE_FALSE(runner.IsRunning());
}

TEST_CASE("A dispatched run accumulates output, parses on exit, and fires the outcome hook", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");

    int hookFired = 0;
    runner.SetOnOutcomeChanged([&hookFired] { ++hookFired; });

    const std::size_t generationBefore = runner.OutcomeGeneration();
    runner.DispatchProcessOutput(kCtestFixture);
    runner.DispatchProcessExit(0);

    REQUIRE(hookFired == 1);
    REQUIRE(runner.OutcomeGeneration() == generationBefore + 1);
    REQUIRE_FALSE(runner.LastRunWasFiltered());

    const auto& outcome = runner.LatestOutcome();
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->results.size() == 2);
    REQUIRE(outcome->passed == 1);
    REQUIRE(outcome->failed == 1);

    Buffer* buffer = bufferList.Find(TestOutputBufferName());
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->Text().find("[exited 0]") != std::string::npos);
    REQUIRE(buffer->Text().find("[tests: 1 passed, 1 failed, 0 skipped]") != std::string::npos);
}

TEST_CASE("A cancelled run appends the trailer and never parses partial output", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");

    runner.DispatchProcessOutput(kCtestFixture);
    runner.DispatchProcessExit(std::nullopt);

    REQUIRE_FALSE(runner.LatestOutcome().has_value());
    REQUIRE(bufferList.Find(TestOutputBufferName())->Text().find("[cancelled]") != std::string::npos);
}

TEST_CASE("An unknown format name is reported instead of parsed", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "no-such-format");

    runner.DispatchProcessOutput("whatever\n");
    runner.DispatchProcessExit(0);

    REQUIRE_FALSE(runner.LatestOutcome().has_value());
    REQUIRE(bufferList.Find(TestOutputBufferName())->Text().find("[unknown test format \"no-such-format\"") !=
            std::string::npos);
}

TEST_CASE("Output that doesn't match the configured format is reported as such", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");

    runner.DispatchProcessOutput("error: everything is on fire\n");
    runner.DispatchProcessExit(1);

    REQUIRE(bufferList.Find(TestOutputBufferName())->Text().find("did not match format \"ctest\"") != std::string::npos);
    const auto& outcome = runner.LatestOutcome();
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->parsedOk);
    REQUIRE(outcome->results.empty());
}

TEST_CASE("A filtered run merges results by name instead of replacing the outcome", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    SetTestFilterCommand({"true", "-R", "^{test}$"});

    // Full run: one pass, one fail.
    runner.DispatchProcessOutput(kCtestFixture);
    runner.DispatchProcessExit(0);
    REQUIRE(runner.LatestOutcome()->failed == 1);

    // Filtered rerun of the failing test, now passing. RunFiltered spawns a
    // real short-lived process whose posted callbacks never run here (no
    // event loop) -- the dispatch below is the test's stand-in for them.
    runner.RunFiltered("FailingTest", "");
    runner.DispatchProcessOutput("1/1 Test #1: FailingTest ......................   Passed    0.01 sec\n"
                                 "100% tests passed, 0 tests failed out of 1\n");
    runner.DispatchProcessExit(0);

    REQUIRE(runner.LastRunWasFiltered());
    const auto& outcome = runner.LatestOutcome();
    REQUIRE(outcome->results.size() == 2); // PassingTest kept, FailingTest replaced
    const auto failing = std::ranges::find_if(outcome->results,
                                              [](const TestResult& r) { return r.name == "FailingTest"; });
    REQUIRE(failing != outcome->results.end());
    REQUIRE(failing->status == TestResult::Status::Passed);
    REQUIRE(outcome->failed == 0);
    REQUIRE(outcome->passed == 2);

    runner.Cancel(); // reap the real spawned process if still alive
}

TEST_CASE("A configured results file is parsed instead of the accumulated output", "[TestRun]") {
    ConfigResetGuard            guard;
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    TestRunner                  runner(bufferList, eventLoop);
    const std::filesystem::path resultsPath =
        std::filesystem::temp_directory_path() / "ned-test-runner-test-results.xml";
    {
        std::ofstream out(resultsPath);
        out << R"(<testsuite><testcase classname="suite" name="from_file"><failure message="boom"/></testcase></testsuite>)";
    }
    SetTestCommand({"true"}, "junit-xml");
    SetTestResultsFile(resultsPath.string());

    runner.DispatchProcessOutput("this stdout text is NOT junit xml\n");
    runner.DispatchProcessExit(0);

    const auto& outcome = runner.LatestOutcome();
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->parsedOk);
    REQUIRE(outcome->results.size() == 1);
    REQUIRE(outcome->results[0].name == "suite::from_file");
    REQUIRE(outcome->results[0].status == TestResult::Status::Failed);

    std::filesystem::remove(resultsPath);
}

TEST_CASE("A registered parser wins over the built-in format of the same name", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    ned::editor::testrun::RegisterTestParser("ctest", [](const std::string&) {
        ned::editor::testrun::TestRunOutcome outcome;
        outcome.format   = "shadowed";
        outcome.parsedOk = true;
        outcome.results.push_back(TestResult{.name = "FromCustomParser", .status = TestResult::Status::Passed});
        outcome.passed = 1;
        return outcome;
    });

    runner.DispatchProcessOutput(kCtestFixture);
    runner.DispatchProcessExit(0);

    REQUIRE(runner.LatestOutcome()->format == "shadowed");
    REQUIRE(runner.LatestOutcome()->results.size() == 1);

    ned::editor::testrun::RegisterTestParser("ctest", {}); // cleanup -- process-wide state
}

TEST_CASE("A throwing registered parser degrades to a reported parse failure", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "exploding");
    ned::editor::testrun::RegisterTestParser("exploding",
                                             [](const std::string&) -> ned::editor::testrun::TestRunOutcome {
                                                 throw std::runtime_error("parser blew up");
                                             });

    runner.DispatchProcessOutput("output\n");
    runner.DispatchProcessExit(0);

    REQUIRE(bufferList.Find(TestOutputBufferName())->Text().find("parser blew up") != std::string::npos);
    REQUIRE(runner.LatestOutcome().has_value());
    REQUIRE_FALSE(runner.LatestOutcome()->parsedOk);

    ned::editor::testrun::RegisterTestParser("exploding", {}); // cleanup
}

TEST_CASE("RerunFailed queues every failed result and chains sequentially through exits", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    SetTestFilterCommand({"true", "-R", "^{test}$"});

    // Full run with two failures and one pass.
    runner.DispatchProcessOutput("1/3 Test #1: AlphaFail ........................***Failed    0.01 sec\n"
                                 "2/3 Test #2: BetaFail .........................***Failed    0.01 sec\n"
                                 "3/3 Test #3: GammaPass ........................   Passed    0.01 sec\n");
    runner.DispatchProcessExit(0);
    REQUIRE(runner.LatestOutcome()->failed == 2);

    REQUIRE(runner.RerunFailed() == 2);
    REQUIRE(runner.IsRunning()); // first filtered run spawned immediately

    // First rerun: AlphaFail now passes; its exit chains the second run.
    runner.DispatchProcessOutput("1/1 Test #1: AlphaFail ........................   Passed    0.01 sec\n");
    runner.DispatchProcessExit(0);
    REQUIRE(runner.IsRunning()); // BetaFail's run chained from the exit

    runner.DispatchProcessOutput("1/1 Test #1: BetaFail .........................   Passed    0.01 sec\n");
    runner.DispatchProcessExit(0);
    REQUIRE_FALSE(runner.IsRunning());

    const auto& outcome = runner.LatestOutcome();
    REQUIRE(outcome->failed == 0);
    REQUIRE(outcome->passed == 3);
    REQUIRE(outcome->results.size() == 3);
}

TEST_CASE("RerunFailed is zero with no outcome, no failures, or no filter template", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);

    REQUIRE(runner.RerunFailed() == 0); // no outcome at all

    SetTestCommand({"true"}, "ctest");
    runner.DispatchProcessOutput("1/1 Test #1: OnlyPass .........................   Passed    0.01 sec\n");
    runner.DispatchProcessExit(0);
    SetTestFilterCommand({"true", "{test}"});
    REQUIRE(runner.RerunFailed() == 0); // nothing failed

    SetTestFilterCommand({});
    REQUIRE(runner.RerunFailed() == 0); // no template
}

TEST_CASE("A cancelled run abandons the pending rerun queue", "[TestRun]") {
    ConfigResetGuard   guard;
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    SetTestCommand({"true"}, "ctest");
    SetTestFilterCommand({"true", "{test}"});

    runner.DispatchProcessOutput("1/2 Test #1: FirstFail ........................***Failed    0.01 sec\n"
                                 "2/2 Test #2: SecondFail .......................***Failed    0.01 sec\n");
    runner.DispatchProcessExit(0);
    REQUIRE(runner.RerunFailed() == 2);

    runner.DispatchProcessExit(std::nullopt); // cancel mid-sequence
    REQUIRE_FALSE(runner.IsRunning());        // and nothing chained afterward
}

TEST_CASE("Cancel when nothing is running is a safe no-op", "[TestRun]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(bufferList, eventLoop);
    runner.Cancel();
    REQUIRE_FALSE(runner.IsRunning());
}
