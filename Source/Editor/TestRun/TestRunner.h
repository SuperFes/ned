//
// Structured test-runner integration: owns the (single) in-flight test run
// -- analogous to Tasks/TaskRunner.h but for the one project-wide test
// command rather than a name-keyed table, and with a structured tail: raw
// output still streams into a read-only buffer exactly like a task's, but
// it is also accumulated and, when the process exits, parsed (built-in
// TestOutputParser.h format or a registered TestParserFn -- registry wins,
// see Config.h) into a Outcome that the "*test results*"
// buffer and BufferView's per-test gutter marks consume.
//
// Threading: constructed and driven on the main thread; the spawned
// TaskProcess marshals its output/exit callbacks there too, so parsing --
// including a Janet-backed parser -- always runs on the main thread.
//

#ifndef NED_EDITOR_TESTRUN_TESTRUNNER_H
#define NED_EDITOR_TESTRUN_TESTRUNNER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Lifetime.h"
#include "Editor/Remote/Placement.h"
#include "Editor/Tasks/TaskProcess.h"
#include "TestResult.h"
#include "Text/RemoteFile.h"
#include "UI/EventLoop.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::testrun {

// The raw-output streaming buffer, "*task: <name>*"'s sibling convention.
[[nodiscard]] std::string TestOutputBufferName();

class TestRunner {
  public:
    // bufferList and eventLoop must both outlive this TestRunner -- the
    // same requirement TaskRunner's constructor documents.
    TestRunner(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop);
    ~TestRunner() = default;

    TestRunner(const TestRunner&)            = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // Spawns TestCommand(); if nothing is configured, appends an error line
    // to the output buffer instead (TaskRunner's no-crash convention).
    // Returns the output buffer for the caller to switch to; a no-op
    // returning the existing buffer while a run is already in flight.
    //
    // A run goes beside from (Remote/Placement.h): on its host, in the
    // project root there, for a remote buffer. Without one, or from a
    // buffer with no file, it goes where the latest run did.
    text::Buffer* RunAll(const text::Buffer* from = nullptr);

    // TestFilterCommand()'s template with {test}/{file} substituted -- a
    // single test's run whose parsed results MERGE by name into the stored
    // outcome instead of replacing it (a filtered run says nothing about
    // the tests it didn't run).
    text::Buffer* RunFiltered(const std::string& testName, const std::string& file, const text::Buffer* from = nullptr);

    // Re-runs every currently-Failed result through the filter template,
    // one sequential filtered run per test (the next spawned from the
    // previous one's exit; merge-by-name accumulates the flips) -- the
    // honest framework-agnostic shape, since no generic filter template can
    // express "these N names" in one invocation. Returns how many were
    // queued; 0 with no failures, no outcome, or no filter template (the
    // caller reports which via the output buffer/status line). A
    // framework's own native rerun (pytest --lf) stays the faster
    // per-framework alternative, run as an ordinary configured command.
    std::size_t RerunFailed();

    void               Cancel();
    [[nodiscard]] bool IsRunning() const;

    // nullopt until a run's output has been parsed at least once.
    [[nodiscard]] const std::optional<Outcome>& LatestOutcome() const;
    // Bumped on every parse -- the gutter cache's invalidation key.
    [[nodiscard]] std::size_t OutcomeGeneration() const;
    // True when the newest parse came from a filtered run -- the gutter's
    // cue not to infer passes from absence under a failuresOnly format.
    [[nodiscard]] bool LastRunWasFiltered() const;

    // Called (main thread) after every parse -- BufferView/WindowManager's
    // rebuild-results-buffer-and-repaint hook. Single-slot, unset is fine.
    void SetOnOutcomeChanged(std::function<void()> fn);

    // Null, the default, runs every test command on this machine. hosts
    // must outlive this TestRunner.
    void SetHosts(remote::Hosts* hosts);

    // Public primarily for tests, mirroring TaskProcess::DispatchOutput/
    // DispatchExit's own precedent: the real TaskProcess callbacks route
    // here via EventLoop::Post, which no unit test can drive; calling these
    // directly exercises the same accumulate/parse/merge logic without a
    // process or a running event loop.
    void DispatchProcessOutput(std::string_view chunk);
    void DispatchProcessExit(std::optional<int> exitCode);

  private:
    text::Buffer* StartRun(const std::vector<std::string>& argv, bool filtered, const text::Buffer* from);
    void          Started(remote::Spawned spawned);
    void          Stopped();
    // A remote run's results file, read from its host.
    void          ReadRemoteResults(const std::string& resultsFile, const std::string& format);
    void          ParseResults(const std::string& format, const std::string& input);
    text::Buffer* OutputBuffer();
    void          MergeOutcome(Outcome fresh);

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    std::string                   accumulated_;
    std::optional<Outcome> latestOutcome_;
    std::size_t                   generation_      = 0;
    bool                          running_         = false;
    bool                          currentFiltered_ = false;
    bool                          lastFiltered_    = false;
    std::function<void()>         onOutcomeChanged_;
    remote::Hosts*                hosts_ = nullptr;

    // Where the latest run went, and the directory it ran in there.
    std::optional<text::RemoteFile>      placedAt_;
    std::optional<std::filesystem::path> ranIn_;

    // RerunFailed's pending (name, file) pairs -- popped one per exit,
    // cleared outright by a cancel.
    std::vector<std::pair<std::string, std::string>> rerunQueue_;

    // Kept allocated after exit (running_ is the liveness flag) and only
    // replaced on the next StartRun -- destroying it from inside its own
    // onExit callback would destroy the closure mid-execution, the exact
    // use-after-free Runner::RunAndCollect's ordering comment documents.
    std::unique_ptr<tasks::TaskProcess> process_;
    // Held while a run starts, possibly on its host; a cancel drops it, so
    // nothing is spawned for that run.
    std::unique_ptr<LifetimeGuard> starting_;
    LifetimeGuard                  lifetime_;
};

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTRUNNER_H
