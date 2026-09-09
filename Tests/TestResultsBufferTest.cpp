#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Project/Root.h"
#include "Editor/TestRun/TestResultsBuffer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::testrun::RebuildTestResultsBuffer;
using ned::editor::testrun::TestResult;
using ned::editor::testrun::TestResultsBufferName;
using ned::editor::testrun::Outcome;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

Outcome MixedOutcome() {
    Outcome outcome;
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

// ProjectRoot is process-wide state (ProjectRoot.h) -- same guard shape as
// ProjectSearchTest.cpp's ProjectSearchThreadsGuard.
//
// Every case that rebuilds a results buffer needs one, not just the two
// that assert on resolution: ProjectRoot() defaults to the process's
// working directory, which under ctest is the *build* tree -- so an
// unresolvable path would send TestSourceResolver's fallback walk through
// tens of thousands of build artifacts (~30s per case under ASan, and it
// starved unrelated tests sharing the parallel run). Pinning to an empty
// directory keeps these cases about formatting, which is what they test.
struct ProjectRootGuard {
    std::filesystem::path previous;

    explicit ProjectRootGuard(const std::filesystem::path& root) : previous(ned::editor::ProjectRoot()) {
        ned::editor::SetProjectRoot(root);
    }
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

// An empty, disposable root: nothing to walk, nothing to resolve against.
struct EmptyProjectRootGuard {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_test_results_buffer_empty_root";
    ProjectRootGuard      guard;

    EmptyProjectRootGuard() : guard([&] {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(root);
            return root;
        }()) {
    }
    ~EmptyProjectRootGuard() {
        std::filesystem::remove_all(root);
    }
};

} // namespace

TEST_CASE("RebuildTestResultsBuffer writes a summary, failures first, skips after, passes omitted", "[TestRun]") {
    const EmptyProjectRootGuard rootGuard;
    BufferList                  bufferList;
    Buffer&                     buffer = RebuildTestResultsBuffer(bufferList, MixedOutcome());

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
    const EmptyProjectRootGuard rootGuard;
    BufferList                  bufferList;
    Buffer&                     buffer = RebuildTestResultsBuffer(bufferList, MixedOutcome());

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
    const EmptyProjectRootGuard rootGuard;
    BufferList                  bufferList;
    Buffer&                     first = RebuildTestResultsBuffer(bufferList, MixedOutcome());

    Outcome clean;
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
    const EmptyProjectRootGuard rootGuard;
    BufferList                  bufferList;
    Outcome              unparsed;
    unparsed.format = "ctest";
    Buffer& buffer  = RebuildTestResultsBuffer(bufferList, unparsed);

    REQUIRE(buffer.Text().find("output did not match this format") != std::string::npos);
    REQUIRE(buffer.Text().find("All tests passed.") == std::string::npos); // no false all-clear
}

TEST_CASE("RebuildTestResultsBuffer explains a failures-only run in the summary", "[TestRun]") {
    const EmptyProjectRootGuard rootGuard;
    // test-runner-gaps follow-up: without -v pytest never names passing
    // tests, so per-test pass marks silently don't happen -- say why,
    // rather than rewriting the user's own configured argv.
    BufferList     bufferList;
    Outcome outcome;
    outcome.format       = "pytest";
    outcome.parsedOk     = true;
    outcome.failuresOnly = true;
    outcome.passed       = 3;
    Buffer& buffer       = RebuildTestResultsBuffer(bufferList, outcome);

    REQUIRE(buffer.Text().find("failures only; add -v for per-test pass marks") != std::string::npos);

    // A format that does name passing tests says nothing extra.
    Outcome full;
    full.format    = "pytest";
    full.parsedOk  = true;
    full.passed    = 3;
    Buffer& second = RebuildTestResultsBuffer(bufferList, full);
    REQUIRE(second.Text().find("failures only") == std::string::npos);
}

TEST_CASE("RebuildTestResultsBuffer resolves a basename-only path to a real file", "[TestRun]") {
    // Gap C end to end: go reports "calc_test.go" with no directory, and the
    // "path:line:" line this writes is what Enter/click hands to
    // OpenOrCreateFile -- an unresolved basename would silently open an
    // empty scratch buffer instead of the real file.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_test_results_buffer_resolve";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "internal" / "beta");
    std::ofstream(root / "internal" / "beta" / "calc_test.go") << "package beta\n";
    const ProjectRootGuard rootGuard(root);

    BufferList     bufferList;
    Outcome outcome;
    outcome.format   = "go-json";
    outcome.parsedOk = true;
    outcome.failed   = 1;
    outcome.results  = {TestResult{.name        = "TestFails",
                                   .status      = TestResult::Status::Failed,
                                   .file        = "calc_test.go",
                                   .line        = 12,
                                   .packagePath = "github.com/org/mod/internal/beta",
                                   .message     = "boom"}};
    Buffer& buffer   = RebuildTestResultsBuffer(bufferList, outcome);

    const std::string expected = (root / "internal" / "beta" / "calc_test.go").string() + ":12: [FAILED] TestFails -- boom";
    REQUIRE(buffer.Text().find(expected) != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("RebuildTestResultsBuffer leaves an unresolvable path exactly as reported", "[TestRun]") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_test_results_buffer_unresolved";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const ProjectRootGuard rootGuard(root);

    BufferList     bufferList;
    Outcome outcome;
    outcome.format   = "go-json";
    outcome.parsedOk = true;
    outcome.failed   = 1;
    outcome.results  = {TestResult{
        .name = "TestGone", .status = TestResult::Status::Failed, .file = "absent_test.go", .line = 4}};
    Buffer& buffer   = RebuildTestResultsBuffer(bufferList, outcome);

    // Still informative to read, and no worse than before the resolver.
    REQUIRE(buffer.Text().find("absent_test.go:4: [FAILED] TestGone") != std::string::npos);

    std::filesystem::remove_all(root);
}
