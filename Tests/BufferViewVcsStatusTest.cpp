//
// VCS vocabulary-completion follow-up: the *vcs status*/*vcs branches*
// buffer building/refreshing and the stage/unstage target resolution --
// exercised through the DispatchStatusForTesting/DispatchBranchesForTesting/
// ResolveVcsFileTargetForTesting seams (see their shared doc comment in
// BufferView.h), since the real async path needs a live EventLoop this
// codebase's tests never run.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::editor::vcs::VcsBranchEntry;
using ned::editor::vcs::VcsStatusEntry;
using ned::ui::BufferView;

namespace {

struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, bufferList, dispatcher, statusMessage, mode, theme);
    }
};

// The mutex-guarded process-wide root, saved/restored around each test --
// ProjectSidebarTest/BufferViewDiffGutterTest's own convention.
struct ProjectRootGuard {
    std::filesystem::path previous = ned::editor::ProjectRoot();
    explicit ProjectRootGuard(const std::filesystem::path& root) {
        ned::editor::SetProjectRoot(root);
    }
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

std::string BufferText(const ned::text::Buffer& buffer) {
    return buffer.Content().Substring(0, buffer.Content().ByteLength());
}

} // namespace

TEST_CASE("status entries build a read-only *vcs status* buffer in the visitable path:1: shape",
          "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    view.DispatchStatusForTesting({VcsStatusEntry{" M", "src/main.cpp"}, VcsStatusEntry{"??", "new.txt"}});

    ned::text::Buffer* status = fixture.bufferList.Find("*vcs status*");
    REQUIRE(status != nullptr);
    REQUIRE(status->ReadOnly());
    REQUIRE(&fixture.activeBuffer.Get() == status);
    REQUIRE(BufferText(*status) == "/repo/src/main.cpp:1:  M src/main.cpp\n"
                                   "/repo/new.txt:1: ?? new.txt\n");
    REQUIRE(fixture.statusMessage.find("2 changed files") != std::string::npos);
}

TEST_CASE("an empty status result announces a clean working tree", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    view.DispatchStatusForTesting({});

    ned::text::Buffer* status = fixture.bufferList.Find("*vcs status*");
    REQUIRE(status != nullptr);
    REQUIRE(BufferText(*status).empty());
    REQUIRE(fixture.statusMessage == "Working tree clean.");
}

TEST_CASE("a second status dispatch refills the same buffer in place, preserving point", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    view.DispatchStatusForTesting({VcsStatusEntry{" M", "a.txt"}, VcsStatusEntry{" M", "b.txt"}});

    ned::text::Buffer* status = fixture.bufferList.Find("*vcs status*");
    REQUIRE(status != nullptr);
    // Park point on the second line, as a stage-at-point would.
    const std::size_t secondLineStart = status->Content().LineToByteOffset(1);
    status->SetPoint(secondLineStart);

    view.DispatchStatusForTesting({VcsStatusEntry{"M ", "a.txt"}, VcsStatusEntry{"M ", "b.txt"}});

    // Refilled in place -- no uniquified "*vcs status*<2>" copy.
    REQUIRE(fixture.bufferList.Find("*vcs status*<2>") == nullptr);
    REQUIRE(fixture.bufferList.Find("*vcs status*") == status);
    REQUIRE(BufferText(*status) == "/repo/a.txt:1: M  a.txt\n"
                                   "/repo/b.txt:1: M  b.txt\n");
    REQUIRE(status->Point() == secondLineStart);

    // Shrinking below the old point clamps rather than leaving a
    // past-the-end point behind.
    view.DispatchStatusForTesting({});
    REQUIRE(status->Point() == 0);
}

TEST_CASE("stage/unstage target resolution: status line at point, else the buffer's own path", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    SECTION("in the *vcs status* buffer, the line at point wins") {
        view.DispatchStatusForTesting({VcsStatusEntry{" M", "src/main.cpp"}, VcsStatusEntry{"??", "new file.txt"}});
        ned::text::Buffer* status = fixture.bufferList.Find("*vcs status*");
        REQUIRE(status != nullptr);

        status->SetPoint(0);
        REQUIRE(view.ResolveVcsFileTargetForTesting() == std::filesystem::path("/repo/src/main.cpp"));

        // A path containing spaces still resolves whole -- the ":1:"
        // terminator is what ends it, not whitespace.
        status->SetPoint(status->Content().LineToByteOffset(1));
        REQUIRE(view.ResolveVcsFileTargetForTesting() == std::filesystem::path("/repo/new file.txt"));
    }

    SECTION("in a file buffer, the buffer's own path wins") {
        ned::text::Buffer fileBuffer = ned::text::Buffer::NewFile("/repo/somewhere/else.txt");
        fixture.activeBuffer.Set(fileBuffer);
        REQUIRE(view.ResolveVcsFileTargetForTesting() == std::filesystem::path("/repo/somewhere/else.txt"));
    }

    SECTION("a pathless scratch buffer resolves to nothing") {
        REQUIRE_FALSE(view.ResolveVcsFileTargetForTesting().has_value());
    }
}

TEST_CASE("hunk staging gates: unsaved changes and pathless buffers are refused up front", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    // A real (never Run) EventLoop + VcsRunner, so the guards under test
    // are BufferView's own rather than the "no vcs runner configured"
    // fallback -- VcsRunnerTest.cpp's own headless convention. The
    // registry is cleared so the final section's "no provider" outcome
    // can't depend on what other test files registered.
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    view.SetVcsRunner(&runner);

    SECTION("no runner wired refuses first") {
        view.SetVcsRunner(nullptr);
        view.StageHunkAtPointForTesting(true);
        REQUIRE(fixture.statusMessage == "no vcs runner configured");
    }

    SECTION("a pathless scratch buffer is refused") {
        view.StageHunkAtPointForTesting(true);
        REQUIRE(fixture.statusMessage == "no file associated with this buffer");
    }

    SECTION("a modified buffer is told to save first") {
        ned::text::Buffer fileBuffer = ned::text::Buffer::NewFile("/repo/file.txt");
        fileBuffer.InsertAtPoint("unsaved edit");
        REQUIRE(fileBuffer.Modified());
        fixture.activeBuffer.Set(fileBuffer);

        view.StageHunkAtPointForTesting(true);
        REQUIRE(fixture.statusMessage.find("save first") != std::string::npos);
    }

    SECTION("an unmodified file buffer gets past the gates into the runner") {
        // No provider registered -- the runner's own resolution failure is
        // what comes back, proving the BufferView gates all passed.
        ned::text::Buffer fileBuffer = ned::text::Buffer::NewFile("/repo/file.txt");
        REQUIRE_FALSE(fileBuffer.Modified());
        fixture.activeBuffer.Set(fileBuffer);

        view.StageHunkAtPointForTesting(true);
        REQUIRE(fixture.statusMessage == "vcs stage hunk: no vcs provider registered for this project");
    }
}

TEST_CASE("branch entries build a read-only *vcs branches* buffer marking the current branch", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    BufferView       view = fixture.View();

    view.DispatchBranchesForTesting({VcsBranchEntry{"dev", false}, VcsBranchEntry{"main", true}});

    ned::text::Buffer* branches = fixture.bufferList.Find("*vcs branches*");
    REQUIRE(branches != nullptr);
    REQUIRE(branches->ReadOnly());
    REQUIRE(&fixture.activeBuffer.Get() == branches);
    REQUIRE(BufferText(*branches) == "  dev\n"
                                     "* main\n");

    // Re-dispatch refills in place, same singleton convention as status.
    view.DispatchBranchesForTesting({VcsBranchEntry{"main", true}});
    REQUIRE(fixture.bufferList.Find("*vcs branches*<2>") == nullptr);
    REQUIRE(BufferText(*branches) == "* main\n");
}
