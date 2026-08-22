//
// multi-line-commit-message follow-up: BeginVcsCommitMessage/
// FinishVcsCommitMessage/AbortVcsCommitMessage, exercised through the
// *ForTesting seams (BufferViewVcsStatusTest.cpp's own precedent) since the
// real async commit-completion path needs a live EventLoop this codebase's
// tests never run -- RequestCommit's own synchronous "no provider
// registered" guard (VcsRunnerTest.cpp) is what proves a request was
// genuinely fired from here.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewVcsStatusTest.cpp's own Fixture exactly, plus a real
// file buffer already in bufferList_ (rather than the bare unlisted
// "scratch" buffer that Fixture's own ActiveBuffer starts on) -- needed so
// CloseBufferNow's fallback-to-"the other buffer in bufferList_" reassignment
// (this test harness never wires ActiveBuffer::SetOnChange to
// BufferList::TouchBuffer -- that's WindowManager::Pane's job, not
// BufferView's own) has something deterministic to land on.
struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme                theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::text::Buffer&    original = bufferList.OpenOrCreateFile("/repo/original.txt");
    ned::ui::ActiveBuffer activeBuffer{original};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

struct ProjectRootGuard {
    std::filesystem::path previous = ned::editor::ProjectRoot();
    explicit ProjectRootGuard(const std::filesystem::path& root) {
        ned::editor::SetProjectRoot(root);
    }
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

// A leftover temp file from a prior, unclean test run (or a real crashed
// session) must never leak content into a fresh run of these tests.
struct CommitTempFileGuard {
    CommitTempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(ned::editor::vcs::VcsCommitMessagePath(), ec);
    }
    ~CommitTempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(ned::editor::vcs::VcsCommitMessagePath(), ec);
    }
};

} // namespace

TEST_CASE("BeginVcsCommitMessage without a wired VcsRunner reports and creates nothing", "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    BufferView          view = fixture.View();

    view.BeginVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath()) == nullptr);
}

TEST_CASE("BeginVcsCommitMessage opens a fresh, template-seeded buffer with point at 0 and switches to it",
          "[BufferView][Vcs]") {
    Fixture                    fixture;
    CommitTempFileGuard        tempGuard;
    ProjectRootGuard           rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();

    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == commitBuffer);
    REQUIRE(commitBuffer->Text().find("# Please enter the commit message") != std::string::npos);
    REQUIRE(commitBuffer->Point() == 0);
}

TEST_CASE("BeginVcsCommitMessage reuses an already-open commit buffer, preserving in-progress edits",
          "[BufferView][Vcs]") {
    Fixture                    fixture;
    CommitTempFileGuard        tempGuard;
    ProjectRootGuard           rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* firstOpen = fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath());
    REQUIRE(firstOpen != nullptr);
    firstOpen->SetPoint(0);
    firstOpen->InsertAtPoint("My in-progress message\n");

    fixture.activeBuffer.Set(fixture.original); // simulate switching away
    view.BeginVcsCommitMessageForTesting();      // and re-running vcs-commit

    ned::text::Buffer* secondOpen = fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath());
    REQUIRE(secondOpen == firstOpen); // same buffer, not a fresh re-seed
    REQUIRE(secondOpen->Text().find("My in-progress message") != std::string::npos);
    REQUIRE(&fixture.activeBuffer.Get() == secondOpen);
}

TEST_CASE("FinishVcsCommitMessage strips the comment template, fires RequestCommit, and closes the buffer",
          "[BufferView][Vcs]") {
    Fixture                    fixture;
    CommitTempFileGuard        tempGuard;
    ProjectRootGuard           rootGuard("/repo");
    ned::editor::vcs::ClearRegistry(); // no provider -- RequestCommit's own guard resolves synchronously
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    commitBuffer->SetPoint(0);
    commitBuffer->InsertAtPoint("Fix the thing\n");

    view.FinishVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "vcs commit: no vcs provider registered for this project");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath()) == nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original); // only other buffer left in bufferList_
    std::error_code ec;
    REQUIRE_FALSE(std::filesystem::exists(ned::editor::vcs::VcsCommitMessagePath(), ec));
}

TEST_CASE("FinishVcsCommitMessage with only the template (no real message) doesn't call RequestCommit",
          "[BufferView][Vcs]") {
    Fixture                    fixture;
    CommitTempFileGuard        tempGuard;
    ProjectRootGuard           rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    view.FinishVcsCommitMessageForTesting(); // nothing typed above the template's comment block

    REQUIRE(fixture.statusMessage == "Empty commit message -- not committing.");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath()) == nullptr);
}

TEST_CASE("AbortVcsCommitMessage discards the buffer without committing", "[BufferView][Vcs]") {
    Fixture                    fixture;
    CommitTempFileGuard        tempGuard;
    ProjectRootGuard           rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::VcsRunner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    commitBuffer->SetPoint(0);
    commitBuffer->InsertAtPoint("A message nobody will ever see\n");

    view.AbortVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "Commit aborted.");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::VcsCommitMessagePath()) == nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original);
}
