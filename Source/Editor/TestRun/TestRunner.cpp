#include "TestRunner.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "Editor/BackgroundActivity.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/SanitizerOutputParser.h"
#include "Editor/ValgrindOutputParser.h"
#include "TestOutputParser.h"
#include "TestRunConfig.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::testrun {

namespace {

    constexpr const char* kActivityName = "tests";

    std::string ReadWholeFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return {};
        }
        std::ostringstream contents;
        contents << stream.rdbuf();
        return std::move(contents).str();
    }

} // namespace

std::string TestOutputBufferName() {
    return "*test output*";
}

TestRunner::TestRunner(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

text::Buffer* TestRunner::OutputBuffer() {
    // Re-found by name on every access rather than cached as a pointer, so
    // a buffer the user closed mid-run is recreated instead of dangling --
    // AsyncFileLoader's own re-resolve convention.
    text::Buffer* buffer = bufferList_.Find(TestOutputBufferName());
    if (!buffer) {
        buffer = &bufferList_.CreateBuffer(TestOutputBufferName());
        buffer->SetReadOnly(true); // before the first append -- AppendWhileReadOnly's precondition
    }
    return buffer;
}

text::Buffer* TestRunner::RunAll() {
    text::Buffer* buffer = OutputBuffer();
    if (running_) {
        return buffer;
    }
    const std::optional<TestCommandConfig> config = TestCommand();
    if (!config) {
        if (!buffer->Text().empty()) {
            buffer->AppendWhileReadOnly("\n--- re-run ---\n");
        }
        buffer->AppendWhileReadOnly("No test command configured (see ned/set-test-command).\n");
        return buffer;
    }
    return StartRun(config->argv, /*filtered=*/false);
}

text::Buffer* TestRunner::RunFiltered(const std::string& testName, const std::string& file) {
    text::Buffer* buffer = OutputBuffer();
    if (running_) {
        return buffer;
    }
    const std::optional<std::vector<std::string>> filterTemplate = TestFilterCommand();
    if (!filterTemplate) {
        if (!buffer->Text().empty()) {
            buffer->AppendWhileReadOnly("\n--- re-run ---\n");
        }
        buffer->AppendWhileReadOnly("No test filter command configured (see ned/set-test-filter-command).\n");
        return buffer;
    }
    return StartRun(SubstituteFilterTemplate(*filterTemplate, testName, file), /*filtered=*/true);
}

std::size_t TestRunner::RerunFailed() {
    if (running_ || !latestOutcome_ || !TestFilterCommand()) {
        return 0;
    }
    rerunQueue_.clear();
    for (const TestResult& result : latestOutcome_->results) {
        if (result.status == TestResult::Status::Failed) {
            rerunQueue_.emplace_back(result.name, result.file);
        }
    }
    if (rerunQueue_.empty()) {
        return 0;
    }
    const std::size_t queued = rerunQueue_.size();
    auto [name, file]        = rerunQueue_.front();
    rerunQueue_.erase(rerunQueue_.begin());
    RunFiltered(name, file);
    return queued;
}

text::Buffer* TestRunner::StartRun(const std::vector<std::string>& argv, bool filtered) {
    text::Buffer* buffer = OutputBuffer();
    if (!buffer->Text().empty()) {
        buffer->AppendWhileReadOnly("\n--- re-run ---\n");
    }

    accumulated_.clear();
    currentFiltered_ = filtered;

    try {
        auto process = std::make_unique<tasks::TaskProcess>(
            argv, eventLoop_, [this](std::string_view chunk) { DispatchProcessOutput(chunk); },
            [this](std::optional<int> exitCode) { DispatchProcessExit(exitCode); });
        // The previous run's (long-exited) process is only released here,
        // never from inside its own exit callback -- see the header.
        process_ = std::move(process);
        running_ = true;
        BeginBackgroundActivity(kActivityName);
    }
    catch (const std::exception& e) {
        buffer->AppendWhileReadOnly(std::string("Failed to start tests: ") + e.what() + "\n");
    }
    return buffer;
}

void TestRunner::DispatchProcessOutput(std::string_view chunk) {
    accumulated_ += chunk;
    OutputBuffer()->AppendWhileReadOnly(chunk);
}

void TestRunner::DispatchProcessExit(std::optional<int> exitCode) {
    if (running_) {
        EndBackgroundActivity(kActivityName);
        running_ = false;
    }

    text::Buffer* buffer = OutputBuffer();
    if (exitCode) {
        buffer->AppendWhileReadOnly("\n[exited " + std::to_string(*exitCode) + "]\n");
    }
    else {
        buffer->AppendWhileReadOnly("\n[cancelled]\n");
        rerunQueue_.clear(); // a cancel abandons any pending rerun-failed sequence too
        return;              // a cancelled run's partial output would parse misleadingly
    }

    // sanitizer-output-parser follow-up: a durable, clickable record of any
    // ASan/UBSan/TSan/MSan/LSan report in this run's own output, alongside
    // the transient "*test output*" scroll -- scanned regardless of
    // exitCode, since a recoverable UBSan finding (-fsanitize-recover=all)
    // can still exit 0.
    for (SanitizerFinding& finding : ParseSanitizerOutput(accumulated_)) {
        std::string message = finding.tool + ": " + finding.message;
        if (!finding.symbol.empty()) {
            message += " (in " + finding.symbol + ")";
        }
        LogMessage(LogCategory::Subprocess, LogSeverity::Error, "test run: " + message,
                   finding.file.empty() ? std::nullopt : std::make_optional(std::move(finding.file)),
                   finding.line == 0 ? std::nullopt : std::make_optional(finding.line));
    }

    // valgrind-xml-parser follow-up: same durable, clickable finding record
    // for a test command pointed at `valgrind --xml=yes ...` -- see the
    // sanitizer loop above.
    for (ValgrindFinding& finding : ParseValgrindXml(accumulated_)) {
        std::string message = finding.tool.empty() ? "valgrind" : finding.tool;
        if (!finding.kind.empty()) {
            message += ": " + finding.kind;
        }
        if (!finding.message.empty()) {
            message += " (" + finding.message + ")";
        }
        LogMessage(LogCategory::Subprocess, LogSeverity::Error, "test run: " + message,
                   finding.file.empty() ? std::nullopt : std::make_optional(std::move(finding.file)),
                   finding.line == 0 ? std::nullopt : std::make_optional(finding.line));
    }

    const std::optional<TestCommandConfig> config = TestCommand();
    const std::string                      format = config ? config->format : std::string();

    const std::optional<std::string> resultsFile = TestResultsFile();
    std::string                      fileContents;
    const std::string*               parseInput = &accumulated_;
    if (resultsFile) {
        fileContents = ReadWholeFile(*resultsFile);
        parseInput   = &fileContents;
    }

    TestRunOutcome fresh;
    if (const std::optional<TestParserFn> registered = RegisteredTestParser(format)) {
        try {
            fresh = (*registered)(*parseInput);
        }
        catch (const std::exception& e) {
            buffer->AppendWhileReadOnly(std::string("[test parser \"") + format + "\" failed: " + e.what() + "]\n");
            fresh          = {};
            fresh.format   = format;
            fresh.parsedOk = false;
        }
    }
    else if (const std::optional<TestRunOutcome> builtIn = ParseTestOutput(format, *parseInput)) {
        fresh = *builtIn;
    }
    else {
        buffer->AppendWhileReadOnly("[unknown test format \"" + format + "\" -- see ned/set-test-command]\n");
        rerunQueue_.clear(); // nothing further would parse either
        return;
    }

    if (!fresh.parsedOk) {
        buffer->AppendWhileReadOnly("[test output did not match format \"" + format + "\"]\n");
    }
    else {
        buffer->AppendWhileReadOnly("[tests: " + std::to_string(fresh.passed) + " passed, " + std::to_string(fresh.failed) +
                                    " failed, " + std::to_string(fresh.skipped) + " skipped]\n");
    }

    MergeOutcome(std::move(fresh));

    // A pending rerun-failed sequence chains its next filtered run from
    // here (the previous run's own exit) -- sequential by construction,
    // never two subprocesses at once.
    if (!rerunQueue_.empty()) {
        auto [name, file] = rerunQueue_.front();
        rerunQueue_.erase(rerunQueue_.begin());
        RunFiltered(name, file);
    }
}

void TestRunner::MergeOutcome(TestRunOutcome fresh) {
    lastFiltered_ = currentFiltered_;

    if (currentFiltered_ && latestOutcome_ && fresh.parsedOk) {
        // Replace-by-name into the previous outcome; anything the filtered
        // run didn't touch keeps its old result.
        TestRunOutcome merged = *latestOutcome_;
        for (TestResult& result : fresh.results) {
            const auto match = std::ranges::find_if(merged.results,
                                                    [&](const TestResult& existing) { return existing.name == result.name; });
            if (match != merged.results.end()) {
                *match = std::move(result);
            }
            else {
                merged.results.push_back(std::move(result));
            }
        }
        // failed/skipped recount from the merged list; the passed count of
        // a failuresOnly outcome is a full-run fact a filtered rerun can't
        // refresh, so it's carried forward as-is.
        merged.failed            = 0;
        merged.skipped           = 0;
        std::size_t passedInList = 0;
        for (const TestResult& result : merged.results) {
            switch (result.status) {
                case TestResult::Status::Passed:
                    ++passedInList;
                    break;
                case TestResult::Status::Failed:
                    ++merged.failed;
                    break;
                case TestResult::Status::Skipped:
                    ++merged.skipped;
                    break;
            }
        }
        if (!merged.failuresOnly) {
            merged.passed = passedInList;
        }
        latestOutcome_ = std::move(merged);
    }
    else if (!currentFiltered_ || fresh.parsedOk) {
        latestOutcome_ = std::move(fresh);
    }

    ++generation_;
    if (onOutcomeChanged_) {
        onOutcomeChanged_();
    }
}

void TestRunner::Cancel() {
    if (running_ && process_) {
        process_->Cancel();
    }
}

bool TestRunner::IsRunning() const {
    return running_;
}

const std::optional<TestRunOutcome>& TestRunner::LatestOutcome() const {
    return latestOutcome_;
}

std::size_t TestRunner::OutcomeGeneration() const {
    return generation_;
}

bool TestRunner::LastRunWasFiltered() const {
    return lastFiltered_;
}

void TestRunner::SetOnOutcomeChanged(std::function<void()> fn) {
    onOutcomeChanged_ = std::move(fn);
}

} // namespace ned::editor::testrun
