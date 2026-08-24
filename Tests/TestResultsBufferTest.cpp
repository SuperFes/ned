#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/TestRun/TestResultsBuffer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::testrun::RebuildTestResultsBuffer;
using ned::editor::testrun::TestResult;
using ned::editor::testrun::TestResultsBufferName;
using ned::editor::testrun::TestRunOutcome;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

TestRunOutcome MixedOutcome() {
    TestRunOutcome outcome;
    outcome.format   = "pytest";
    outcome.parsedOk = true;
    outcome.passed   = 2;
    outcome.failed   = 2;
    outcome.skipped  = 1;
    outcome.results  = {
        TestResult{.name = "test_ok", .status = TestResult::Status::Passed, .file = "tests/a.py", .line = 3},
        TestResult{.name    = "test_fails",
                   .status  = TestResult::Status::Failed,
                   .file    = "tests/a.py",
                   .line    = 7,
                   .message = "assert 1 == 2"},
        TestResult{.name = "test_skipped", .status = TestResult::Status::Skipped, .message = "not ready"},
        TestResult{.name = "NoLocationFail", .status = TestResult::Status::Failed},
        TestResult{.name = "test_ok2", .status = TestResult::Status::Passed},
    };
    return outcome;
}

} // namespace

TEST_CASE("RebuildTestResultsBuffer writes a summary, failures first, skips after, passes omitted", "[TestRun]") {
    BufferList bufferList;
    Buffer&    buffer = RebuildTestResultsBuffer(bufferList, MixedOutcome());

    REQUIRE(buffer.ReadOnly());
    const std::string text = buffer.Text();

    REQUIRE(text.find("Tests: 2 passed, 2 failed, 1 skipped (pytest)") == 0);
    // Jump-to-source rides the existing path:line: convention.
    REQUIRE(text.find("tests/a.py:7: [FAILED] test_fails -- assert 1 == 2") != std::string::npos);
    REQUIRE(text.find("[FAILED] NoLocationFail") != std::string::npos);
    REQUIRE(text.find("[SKIPPED] test_skipped -- not ready") != std::string::npos);
    // Failures come before skips regardless of result order.
    REQUIRE(text.find("[FAILED] NoLocationFail") < text.find("[SKIPPED]"));
    // Passing tests never appear.
    REQUIRE(text.find("test_ok") == std::string::npos);

    // Point sits on the first failure line, not the summary.
    REQUIRE(buffer.Point() == text.find("tests/a.py:7:"));
}

TEST_CASE("RebuildTestResultsBuffer attaches one severity-mapped diagnostic per listed line", "[TestRun]") {
    BufferList bufferList;
    Buffer&    buffer = RebuildTestResultsBuffer(bufferList, MixedOutcome());

    const auto& diagnostics = buffer.Diagnostics();
    REQUIRE(diagnostics.size() == 3); // 2 failed + 1 skipped
    REQUIRE(diagnostics[0].severity == ned::text::Buffer::Diagnostic::Severity::Error);
    REQUIRE(diagnostics[1].severity == ned::text::Buffer::Diagnostic::Severity::Error);
    REQUIRE(diagnostics[2].severity == ned::text::Buffer::Diagnostic::Severity::Warning);

    const std::string text = buffer.Text();
    for (const auto& diagnostic : diagnostics) {
        // Each diagnostic spans exactly its own line, newline excluded.
        REQUIRE(diagnostic.endByte > diagnostic.startByte);
        REQUIRE(text[diagnostic.endByte] == '\n');
        REQUIRE((diagnostic.startByte == 0 || text[diagnostic.startByte - 1] == '\n'));
    }
}

TEST_CASE("RebuildTestResultsBuffer refreshes the same buffer in place", "[TestRun]") {
    BufferList bufferList;
    Buffer&    first = RebuildTestResultsBuffer(bufferList, MixedOutcome());

    TestRunOutcome clean;
    clean.format   = "pytest";
    clean.parsedOk = true;
    clean.passed   = 5;
    Buffer& second = RebuildTestResultsBuffer(bufferList, clean);

    REQUIRE(&first == &second);
    REQUIRE(second.Name() == TestResultsBufferName());
    REQUIRE(second.Text().find("Tests: 5 passed, 0 failed, 0 skipped") == 0);
    REQUIRE(second.Text().find("All tests passed.") != std::string::npos);
    REQUIRE(second.Text().find("[FAILED]") == std::string::npos);
    REQUIRE(second.Diagnostics().empty());
}

TEST_CASE("RebuildTestResultsBuffer marks an unparsed outcome in the summary", "[TestRun]") {
    BufferList     bufferList;
    TestRunOutcome unparsed;
    unparsed.format = "ctest";
    Buffer& buffer  = RebuildTestResultsBuffer(bufferList, unparsed);

    REQUIRE(buffer.Text().find("output did not match this format") != std::string::npos);
    REQUIRE(buffer.Text().find("All tests passed.") == std::string::npos); // no false all-clear
}
