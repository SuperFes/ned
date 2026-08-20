#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "Editor/ProjectRoot.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "UI/EventLoop.h"

using ned::editor::vcs::ActiveProviderFor;
using ned::editor::vcs::ClearRegistry;
using ned::editor::vcs::RegisterProvider;
using ned::editor::vcs::VcsBlameLine;
using ned::editor::vcs::VcsCommandSpec;
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
    explicit FakeProvider(bool blameArgvThrows = false) : blameArgvThrows_(blameArgvThrows) {}

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override { return true; }

    [[nodiscard]] VcsCommandSpec BlameArgv(const std::filesystem::path&) const override {
        if (blameArgvThrows_) {
            throw std::runtime_error("fake blame-argv failure");
        }
        return VcsCommandSpec{{"sleep", "5"}}; // long-running -- never actually completes within a test
    }
    [[nodiscard]] std::vector<VcsBlameLine> ParseBlame(const std::string&) const override { return {}; }
    [[nodiscard]] VcsCommandSpec            LogArgv(const std::filesystem::path&) const override {
        return VcsCommandSpec{{"sleep", "5"}};
    }
    [[nodiscard]] std::vector<VcsLogEntry> ParseLog(const std::string&) const override { return {}; }

  private:
    bool blameArgvThrows_;
};

struct RegistryResetGuard {
    RegistryResetGuard() { ClearRegistry(); }
    ~RegistryResetGuard() { ClearRegistry(); }
};

} // namespace

TEST_CASE("VcsRunner::RequestBlame reports an error for a buffer with no associated path", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop  eventLoop;
    VcsRunner           runner(eventLoop);

    Buffer      buffer("scratch");
    std::string error;
    runner.RequestBlame(
        buffer, [](std::vector<VcsBlameLine>) { FAIL("onComplete should not be called"); },
        [&error](std::string message) { error = message; });

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VcsRunner::RequestBlame reports an error when no provider is registered", "[VcsRunner]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop  eventLoop;
    VcsRunner           runner(eventLoop);

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
