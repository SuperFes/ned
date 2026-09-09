#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "Editor/Project/Root.h"
#include "Editor/Vcs/ProviderRegistry.h"
#include "Editor/Vcs/Runner.h"
#include "Text/Buffer.h"
#include "UI/EventLoop.h"

using ned::editor::vcs::ActiveProviderFor;
using ned::editor::vcs::ClearRegistry;
using ned::editor::vcs::ExtractCommitMessage;
using ned::editor::vcs::kVcsCommitMessageFilename;
using ned::editor::vcs::RegisterProvider;
using ned::editor::vcs::BlameLine;
using ned::editor::vcs::CommandSpec;
using ned::editor::vcs::CommitMessagePath;
using ned::editor::vcs::DiffHunk;
using ned::editor::vcs::LogEntry;
using ned::editor::vcs::Provider;
using ned::editor::vcs::Runner;
using ned::text::Buffer;

// Same rationale as TaskProcessTest.cpp/TaskRunnerTest.cpp's own header
// comments: a real ned::ui::EventLoop is constructed (Runner needs a
// real EventLoop& to hand each TaskProcess it spawns), but its Run() loop
// is never started here, so a real spawned process's streamed
// completion/parse callback never actually fires within these tests --
// only Runner's own synchronous behavior (no-path/no-provider/
// already-running guards, and a provider callback throwing before any
// process is even spawned) is under test, matching this codebase's
// established "never run a real EventLoop::Run() loop in a unit test"
// convention.

namespace {

class FakeProvider : public Provider {
  public:
    explicit FakeProvider(bool blameArgvThrows = false) : blameArgvThrows_(blameArgvThrows) {
    }

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }

    [[nodiscard]] CommandSpec BlameArgv(const std::filesystem::path&) const override {
        if (blameArgvThrows_) {
            throw std::runtime_error("fake blame-argv failure");
        }
        return CommandSpec{{"sleep", "5"}}; // long-running -- never actually completes within a test
    }
    [[nodiscard]] std::vector<BlameLine> ParseBlame(const std::string&) const override {
        return {};
    }
    [[nodiscard]] CommandSpec LogArgv(const std::filesystem::path&) const override {
        return CommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] std::vector<LogEntry> ParseLog(const std::string&) const override {
        return {};
    }
    [[nodiscard]] CommandSpec DiffArgv(const std::filesystem::path&) const override {
        return CommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] std::vector<DiffHunk> ParseDiff(const std::string&) const override {
        return {};
    }

  private:
    bool blameArgvThrows_;
};

struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistry();
    }
    ~RegistryResetGuard() {
        ClearRegistry();
    }
};

} // namespace

TEST_CASE("Runner::RequestBlame reports an error for a buffer with no associated path", "[Runner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<BlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runner::RequestBlame reports an error when no provider is registered", "[Runner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<BlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runner::RequestBlame reports an error if BlameArgv throws", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>(/*blameArgvThrows=*/true));

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<BlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE(error == "fake blame-argv failure");
}

TEST_CASE("Runner::RequestBlame refuses a second concurrent request for the same buffer", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>());

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    bool firstErrored = false;
    runner.RequestBlame(
        buffer, [](std::vector<BlameLine>) {}, [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored); // first request spawned successfully (sleep 5, still running)

    std::string secondError;
    runner.RequestBlame(
        buffer, [](std::vector<BlameLine>) { FAIL("onComplete should not be called"); },
        [&secondError](std::string message) { secondError = message; });

    REQUIRE_FALSE(secondError.empty());
}

// VCS vocabulary-completion follow-up: the same synchronous guard/error
// paths for the new operations. FakeProvider deliberately does NOT
// override any of them, so these also prove the base class's
// default-throwing "not supported by this provider" answer travels the
// whole runner path into onError -- exactly what a partial provider's
// user would see on the status line.

TEST_CASE("Runner root-scoped requests report an error when no provider is registered", "[Runner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    std::string error;
    runner.RequestStatus([](std::vector<ned::editor::vcs::StatusEntry>) { FAIL("onComplete should not be called"); },
                         [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestCommit(
        "a message", [](std::string) { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestBranchList(
        [](std::vector<ned::editor::vcs::BranchEntry>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestFullDiff([](std::string) { FAIL("onComplete should not be called"); },
                           [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    // VCS side panel follow-up: revert/stash/push-pull-fetch/ahead-behind.
    error.clear();
    runner.RequestStashList(
        [](std::vector<ned::editor::vcs::StashEntry>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestStashPush(
        "a message", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestPush([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestPull([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestFetch([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestAheadBehind([](ned::editor::vcs::AheadBehind) { FAIL("onComplete should not be called"); },
                              [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runner surfaces the provider's own 'not supported' answer for unimplemented operations", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>()); // blame/log/diff only -- no vocabulary-completion overrides

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    std::string error;
    runner.RequestStatus([](std::vector<ned::editor::vcs::StatusEntry>) { FAIL("onComplete should not be called"); },
                         [&error](std::string message) { error = message; });
    REQUIRE(error == "status not supported by this provider");

    error.clear();
    runner.RequestStage(
        "/tmp/ned-vcs-runner-test-file.txt", [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "stage not supported by this provider");

    error.clear();
    runner.RequestUnstage(
        "/tmp/ned-vcs-runner-test-file.txt", [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "unstage not supported by this provider");

    error.clear();
    runner.RequestCommit(
        "a message", [](std::string) { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "commit not supported by this provider");

    error.clear();
    runner.RequestBranchList(
        [](std::vector<ned::editor::vcs::BranchEntry>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "branch listing not supported by this provider");

    error.clear();
    runner.RequestBranchSwitch(
        "dev", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "branch switching not supported by this provider");

    error.clear();
    runner.RequestBranchCreate(
        "dev", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "branch creation not supported by this provider");

    error.clear();
    runner.RequestFullDiff([](std::string) { FAIL("onComplete should not be called"); },
                           [&error](std::string message) { error = message; });
    REQUIRE(error == "full diff not supported by this provider");

    error.clear();
    runner.RequestCommitDiff(
        "abc1234", [](std::string) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "commit diff not supported by this provider");

    // VCS side panel follow-up: revert/stash/push-pull-fetch/ahead-behind.
    error.clear();
    runner.RequestRevert(
        "/tmp/ned-vcs-runner-test-file.txt", [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "revert not supported by this provider");

    error.clear();
    runner.RequestStashList(
        [](std::vector<ned::editor::vcs::StashEntry>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "stash listing not supported by this provider");

    error.clear();
    runner.RequestStashPush(
        "", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "stash push not supported by this provider");

    error.clear();
    runner.RequestStashPop(
        "stash@{0}", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "stash pop not supported by this provider");

    error.clear();
    runner.RequestStashDrop(
        "stash@{0}", [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "stash drop not supported by this provider");

    error.clear();
    runner.RequestPush([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "push not supported by this provider");

    error.clear();
    runner.RequestPull([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "pull not supported by this provider");

    error.clear();
    runner.RequestFetch([] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "fetch not supported by this provider");

    error.clear();
    runner.RequestAheadBehind([](ned::editor::vcs::AheadBehind) { FAIL("onComplete should not be called"); },
                              [&error](std::string message) { error = message; });
    REQUIRE(error == "ahead/behind not supported by this provider");

    // staged=true (not false): FakeProvider does implement DiffArgv (it
    // predates the vocabulary-completion split, "blame/log/diff only" per
    // its own comment above) -- staged=false would actually spawn a real
    // "sleep 5" subprocess instead of erroring. StagedDiffArgv is a
    // vocabulary-completion-era method FakeProvider doesn't override, same
    // distinction the RequestHunkApply test above already relies on.
    error.clear();
    runner.RequestFileDiffText(
        "/tmp/ned-vcs-runner-test-file.txt", /*staged=*/true, [](std::string) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "staged diff not supported by this provider");
}

namespace {

// Implements the vocabulary-completion operations with never-completing
// commands, mirroring FakeProvider's own sleep-5 convention, for the
// duplicate-concurrent-request guard below.
class FakeVocabProvider : public FakeProvider {
  public:
    [[nodiscard]] CommandSpec StatusArgv(const std::filesystem::path&) const override {
        return CommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] CommandSpec CommitArgv(const std::filesystem::path&, const std::string&) const override {
        return CommandSpec{{"sleep", "5"}};
    }
};

} // namespace

TEST_CASE("Runner refuses a second concurrent status/commit for the same root", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeVocabProvider>());

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    bool firstErrored = false;
    runner.RequestStatus([](std::vector<ned::editor::vcs::StatusEntry>) {},
                         [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored); // first request spawned successfully (sleep 5, still running)

    std::string secondError;
    runner.RequestStatus(
        [](std::vector<ned::editor::vcs::StatusEntry>) { FAIL("onComplete should not be called"); },
        [&secondError](std::string message) { secondError = message; });
    REQUIRE_FALSE(secondError.empty());

    // A different operation against the same root is NOT blocked -- the
    // guard is per (operation, root), not per root.
    bool commitErrored = false;
    runner.RequestCommit(
        "a message", [](std::string) {}, [&commitErrored](std::string) { commitErrored = true; });
    REQUIRE_FALSE(commitErrored);
}

// Hunk-staging follow-up: RequestHunkApply's synchronous guard paths. The
// async tail (extract -> temp file -> apply) is covered end to end by
// GitVcsPluginTest's real-repo hunk test and DiffPatchTest's own unit
// tests instead, per the no-live-EventLoop convention above.

TEST_CASE("Runner::RequestHunkApply reports an error for a pathless buffer", "[Runner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestHunkApply(
        buffer, 1, /*stage=*/true, [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runner::RequestHunkApply surfaces a provider without the staged-diff vocabulary", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>()); // no StagedDiffArgv override

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    // An unstage needs the cached diff first -- FakeProvider's base-class
    // default throws, and that answer must reach onError.
    std::string error;
    runner.RequestHunkApply(
        buffer, 1, /*stage=*/false, [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE(error == "staged diff not supported by this provider");

    // The stage direction starts from DiffArgv, which FakeProvider does
    // implement (sleep 5) -- it spawns and a duplicate is then guarded.
    bool firstErrored = false;
    runner.RequestHunkApply(
        buffer, 1, /*stage=*/true, [] {}, [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored);

    std::string secondError;
    runner.RequestHunkApply(
        buffer, 1, /*stage=*/true, [] { FAIL("onSuccess should not be called"); },
        [&secondError](std::string message) { secondError = message; });
    REQUIRE_FALSE(secondError.empty());
}

// VCS side panel follow-up: the path-based RequestHunkApply overload (no
// live Buffer&, VcsPanel's own diff preview) shares RequestHunkApplyForPath
// with the Buffer-taking overload above -- same "surfaces a provider
// without the staged-diff vocabulary" behavior confirms both really do
// share one core rather than having silently diverged.
TEST_CASE("Runner::RequestHunkApply's path-based overload surfaces the same provider errors", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>()); // no StagedDiffArgv override

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    std::string error;
    runner.RequestHunkApply(
        std::filesystem::path("/tmp/ned-vcs-runner-test-file.txt"), 1, /*stage=*/false,
        [] { FAIL("onSuccess should not be called"); }, [&error](std::string message) { error = message; });
    REQUIRE(error == "staged diff not supported by this provider");
}

// mouse-ergonomics follow-up: RequestHunkRevert's own synchronous guard
// paths, same "async tail covered by GitVcsPluginTest's real-repo test"
// convention RequestHunkApply's own tests above document.

TEST_CASE("Runner::RequestHunkRevert reports an error for a pathless buffer", "[Runner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestHunkRevert(
        buffer, 1, [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Runner::RequestHunkRevert guards against a duplicate concurrent request", "[Runner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>());

    ned::ui::EventLoop eventLoop;
    Runner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    // DiffArgv is FakeProvider's own long-running "sleep 5" -- spawns and
    // never completes within the test, occupying the "revert-hunk-diff:"
    // key so the second call below hits the duplicate-running guard
    // synchronously, the same shape RequestHunkApply's own vocabulary test
    // above relies on for its own first/duplicate pair.
    bool firstErrored = false;
    runner.RequestHunkRevert(buffer, 1, [] {}, [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored);

    std::string secondError;
    runner.RequestHunkRevert(
        buffer, 1, [] { FAIL("onSuccess should not be called"); },
        [&secondError](std::string message) { secondError = message; });
    REQUIRE_FALSE(secondError.empty());
}

TEST_CASE("CommitMessagePath is the temp dir plus kVcsCommitMessageFilename", "[Vcs]") {
    REQUIRE(CommitMessagePath() == std::filesystem::temp_directory_path() / std::string(kVcsCommitMessageFilename));
}

TEST_CASE("ExtractCommitMessage strips '#'-prefixed lines and trims trailing whitespace", "[Vcs]") {
    REQUIRE(ExtractCommitMessage("Fix the thing\n\n# Please enter the commit message...\n# with '#' ignored\n") ==
            "Fix the thing");
    REQUIRE(ExtractCommitMessage("Subject\nBody line 1\nBody line 2\n") == "Subject\nBody line 1\nBody line 2");
}

TEST_CASE("ExtractCommitMessage returns empty for an all-comments-or-blank buffer", "[Vcs]") {
    REQUIRE(ExtractCommitMessage("").empty());
    REQUIRE(ExtractCommitMessage("\n\n").empty());
    REQUIRE(ExtractCommitMessage("# only a comment\n#another\n").empty());
}

TEST_CASE("ExtractCommitMessage keeps a '#' that isn't the first character of a line", "[Vcs]") {
    // git's own comment convention: only a line whose *first* character is
    // '#' is stripped -- "fix issue #42" is real message content.
    REQUIRE(ExtractCommitMessage("fix issue #42\n") == "fix issue #42");
}
