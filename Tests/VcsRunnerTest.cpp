#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "Editor/ProjectRoot.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "UI/EventLoop.h"

using ned::editor::vcs::ActiveProviderFor;
using ned::editor::vcs::ClearRegistry;
using ned::editor::vcs::ExtractCommitMessage;
using ned::editor::vcs::kVcsCommitMessageFilename;
using ned::editor::vcs::RegisterProvider;
using ned::editor::vcs::VcsBlameLine;
using ned::editor::vcs::VcsCommandSpec;
using ned::editor::vcs::VcsCommitMessagePath;
using ned::editor::vcs::VcsDiffHunk;
using ned::editor::vcs::VcsLogEntry;
using ned::editor::vcs::VcsProvider;
using ned::editor::vcs::VcsRunner;
using ned::text::Buffer;

// Same rationale as TaskProcessTest.cpp/TaskRunnerTest.cpp's own header
// comments: a real ned::ui::EventLoop is constructed (VcsRunner needs a
// real EventLoop& to hand each TaskProcess it spawns), but its Run() loop
// is never started here, so a real spawned process's streamed
// completion/parse callback never actually fires within these tests --
// only VcsRunner's own synchronous behavior (no-path/no-provider/
// already-running guards, and a provider callback throwing before any
// process is even spawned) is under test, matching this codebase's
// established "never run a real EventLoop::Run() loop in a unit test"
// convention.

namespace {

class FakeProvider : public VcsProvider {
  public:
    explicit FakeProvider(bool blameArgvThrows = false) : blameArgvThrows_(blameArgvThrows) {
    }

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }

    [[nodiscard]] VcsCommandSpec BlameArgv(const std::filesystem::path&) const override {
        if (blameArgvThrows_) {
            throw std::runtime_error("fake blame-argv failure");
        }
        return VcsCommandSpec{{"sleep", "5"}}; // long-running -- never actually completes within a test
    }
    [[nodiscard]] std::vector<VcsBlameLine> ParseBlame(const std::string&) const override {
        return {};
    }
    [[nodiscard]] VcsCommandSpec LogArgv(const std::filesystem::path&) const override {
        return VcsCommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] std::vector<VcsLogEntry> ParseLog(const std::string&) const override {
        return {};
    }
    [[nodiscard]] VcsCommandSpec DiffArgv(const std::filesystem::path&) const override {
        return VcsCommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] std::vector<VcsDiffHunk> ParseDiff(const std::string&) const override {
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

TEST_CASE("VcsRunner::RequestBlame reports an error for a buffer with no associated path", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VcsRunner::RequestBlame reports an error when no provider is registered", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VcsRunner::RequestBlame reports an error if BlameArgv throws", "[VcsRunner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>(/*blameArgvThrows=*/true));

    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE(error == "fake blame-argv failure");
}

TEST_CASE("VcsRunner::RequestBlame refuses a second concurrent request for the same buffer", "[VcsRunner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>());

    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    Buffer buffer = Buffer::NewFile("/tmp/ned-vcs-runner-test-file.txt");

    bool firstErrored = false;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) {}, [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored); // first request spawned successfully (sleep 5, still running)

    std::string secondError;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) { FAIL("onComplete should not be called"); },
        [&secondError](std::string message) { secondError = message; });

    REQUIRE_FALSE(secondError.empty());
}

// VCS vocabulary-completion follow-up: the same synchronous guard/error
// paths for the new operations. FakeProvider deliberately does NOT
// override any of them, so these also prove the base class's
// default-throwing "not supported by this provider" answer travels the
// whole runner path into onError -- exactly what a partial provider's
// user would see on the status line.

TEST_CASE("VcsRunner root-scoped requests report an error when no provider is registered", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    std::string error;
    runner.RequestStatus([](std::vector<ned::editor::vcs::VcsStatusEntry>) { FAIL("onComplete should not be called"); },
                         [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestCommit(
        "a message", [](std::string) { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestBranchList(
        [](std::vector<ned::editor::vcs::VcsBranchEntry>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());

    error.clear();
    runner.RequestFullDiff([](std::string) { FAIL("onComplete should not be called"); },
                           [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VcsRunner surfaces the provider's own 'not supported' answer for unimplemented operations", "[VcsRunner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>()); // blame/log/diff only -- no vocabulary-completion overrides

    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    std::string error;
    runner.RequestStatus([](std::vector<ned::editor::vcs::VcsStatusEntry>) { FAIL("onComplete should not be called"); },
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
        [](std::vector<ned::editor::vcs::VcsBranchEntry>) { FAIL("onComplete should not be called"); },
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
}

namespace {

// Implements the vocabulary-completion operations with never-completing
// commands, mirroring FakeProvider's own sleep-5 convention, for the
// duplicate-concurrent-request guard below.
class FakeVocabProvider : public FakeProvider {
  public:
    [[nodiscard]] VcsCommandSpec StatusArgv(const std::filesystem::path&) const override {
        return VcsCommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] VcsCommandSpec CommitArgv(const std::filesystem::path&, const std::string&) const override {
        return VcsCommandSpec{{"sleep", "5"}};
    }
};

} // namespace

TEST_CASE("VcsRunner refuses a second concurrent status/commit for the same root", "[VcsRunner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeVocabProvider>());

    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    bool firstErrored = false;
    runner.RequestStatus([](std::vector<ned::editor::vcs::VcsStatusEntry>) {},
                         [&firstErrored](std::string) { firstErrored = true; });
    REQUIRE_FALSE(firstErrored); // first request spawned successfully (sleep 5, still running)

    std::string secondError;
    runner.RequestStatus(
        [](std::vector<ned::editor::vcs::VcsStatusEntry>) { FAIL("onComplete should not be called"); },
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

TEST_CASE("VcsRunner::RequestHunkApply reports an error for a pathless buffer", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestHunkApply(
        buffer, 1, /*stage=*/true, [] { FAIL("onSuccess should not be called"); },
        [&error](std::string message) { error = message; });
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VcsRunner::RequestHunkApply surfaces a provider without the staged-diff vocabulary", "[VcsRunner]") {
    RegistryResetGuard guard;
    RegisterProvider("fake", std::make_unique<FakeProvider>()); // no StagedDiffArgv override

    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

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

TEST_CASE("VcsCommitMessagePath is the temp dir plus kVcsCommitMessageFilename", "[Vcs]") {
    REQUIRE(VcsCommitMessagePath() == std::filesystem::temp_directory_path() / std::string(kVcsCommitMessageFilename));
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
