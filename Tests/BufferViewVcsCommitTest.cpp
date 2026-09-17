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
#include <memory>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/Provider.h"
#include "Editor/Vcs/ProviderRegistry.h"
#include "Editor/Vcs/Runner.h"
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
    ned::ui::Theme               theme = ned::ui::DarkTheme();

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
        std::filesystem::remove(ned::editor::vcs::CommitMessagePath(), ec);
    }
    ~CommitTempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(ned::editor::vcs::CommitMessagePath(), ec);
    }
};

// VcsPanel amend follow-up: Detect-only, otherwise default-throwing --
// CommitArgv/AmendCommitArgv's own distinct default error text ("commit
// not supported"/"amend commit not supported") is what lets the tests
// below tell RequestCommit and RequestAmendCommit apart synchronously,
// without a live EventLoop (same technique JanetVcsProviderTest.cpp's own
// "not supported" tests and VcsRunnerTest.cpp already use).
class DetectOnlyProvider : public ned::editor::vcs::Provider {
  public:
    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }
};

} // namespace

TEST_CASE("BeginVcsCommitMessage without a wired Runner reports and creates nothing", "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    BufferView          view = fixture.View();

    view.BeginVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("BeginVcsCommitMessage opens a fresh, template-seeded buffer with point at 0 and switches to it",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();

    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == commitBuffer);
    REQUIRE(commitBuffer->Text().find("# Please enter the commit message") != std::string::npos);
    REQUIRE(commitBuffer->Point() == 0);
}

TEST_CASE("BeginVcsCommitMessage reuses an already-open commit buffer, preserving in-progress edits",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* firstOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(firstOpen != nullptr);
    firstOpen->SetPoint(0);
    firstOpen->InsertAtPoint("My in-progress message\n");

    fixture.activeBuffer.Set(fixture.original); // simulate switching away
    view.BeginVcsCommitMessageForTesting();     // and re-running vcs-commit

    ned::text::Buffer* secondOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(secondOpen == firstOpen); // same buffer, not a fresh re-seed
    REQUIRE(secondOpen->Text().find("My in-progress message") != std::string::npos);
    REQUIRE(&fixture.activeBuffer.Get() == secondOpen);
}

TEST_CASE("FinishVcsCommitMessage strips the comment template, fires RequestCommit, and closes the buffer",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry(); // no provider -- RequestCommit's own guard resolves synchronously
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    commitBuffer->SetPoint(0);
    commitBuffer->InsertAtPoint("Fix the thing\n");

    view.FinishVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "vcs commit: no vcs provider registered for this project");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original); // only other buffer left in bufferList_
    std::error_code ec;
    REQUIRE_FALSE(std::filesystem::exists(ned::editor::vcs::CommitMessagePath(), ec));
}

TEST_CASE("FinishVcsCommitMessage with only the template (no real message) doesn't call RequestCommit",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    view.FinishVcsCommitMessageForTesting(); // nothing typed above the template's comment block

    REQUIRE(fixture.statusMessage == "Empty commit message -- not committing.");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("AbortVcsCommitMessage discards the buffer without committing", "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting();
    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    commitBuffer->SetPoint(0);
    commitBuffer->InsertAtPoint("A message nobody will ever see\n");

    view.AbortVcsCommitMessageForTesting();

    REQUIRE(fixture.statusMessage == "Commit aborted.");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original);
}

// VcsPanel amend follow-up: BeginVcsCommitMessage(amend=true)'s own
// synchronous guard paths, mirroring the plain-commit cases above. The real
// async previous-commit-message fetch (seeding the buffer) needs a live
// EventLoop -- exercised end to end against real git in
// GitVcsPluginTest.cpp instead, matching this file's own header comment on
// why *ForTesting seams don't try to cover the async tail.

TEST_CASE("BeginVcsCommitMessage(amend) without a wired Runner reports and creates nothing", "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    BufferView          view = fixture.View();

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Amend);

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("BeginVcsCommitMessage(amend) with no vcs provider reports the runner's error and creates no buffer",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry(); // no provider registered at all
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Amend);

    REQUIRE(fixture.statusMessage == "vcs commit amend: no vcs provider registered for this project");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("Re-running vcs-commit-amend on an already-open commit buffer just marks it for amend, "
          "without re-fetching or touching its content",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Commit);
    ned::text::Buffer* firstOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(firstOpen != nullptr);
    firstOpen->SetPoint(0);
    firstOpen->InsertAtPoint("My in-progress message\n");

    fixture.activeBuffer.Set(fixture.original); // simulate switching away
    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Amend); // re-run as vcs-commit-amend instead

    // Already open -- no fetch attempted (statusMessage_ never became
    // "Fetching previous commit message..."), content untouched, just
    // switched back to it.
    ned::text::Buffer* secondOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(secondOpen == firstOpen);
    REQUIRE(secondOpen->Text().find("My in-progress message") != std::string::npos);
    REQUIRE(&fixture.activeBuffer.Get() == secondOpen);

    // The pending amend flag itself only shows up in which Provider method
    // FinishVcsCommitMessage calls -- DetectOnlyProvider's CommitArgv and
    // AmendCommitArgv default-throw distinct text, so this proves
    // RequestAmendCommit fired, not RequestCommit.
    ned::editor::vcs::RegisterProvider("fake", std::make_unique<DetectOnlyProvider>());
    view.FinishVcsCommitMessageForTesting();
    REQUIRE(fixture.statusMessage == "vcs commit: amend commit not supported by this provider");
}

TEST_CASE("AbortVcsCommitMessage clears the pending amend flag for the next plain vcs-commit",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Commit);
    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Amend); // already open -- just marks pending amend
    view.AbortVcsCommitMessageForTesting();

    // A fresh vcs-commit (not amend) after the abort must not still carry
    // the aborted session's amend flag.
    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Commit);
    ned::text::Buffer* commitBuffer = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(commitBuffer != nullptr);
    commitBuffer->SetPoint(0);
    commitBuffer->InsertAtPoint("Fix the thing\n");

    ned::editor::vcs::RegisterProvider("fake", std::make_unique<DetectOnlyProvider>());
    view.FinishVcsCommitMessageForTesting();
    REQUIRE(fixture.statusMessage == "vcs commit: commit not supported by this provider");
}

// Reword follow-up: BeginVcsCommitMessage(VcsCommitMode::Reword)'s own
// synchronous guard paths, mirroring the amend cases above.

TEST_CASE("BeginVcsCommitMessage(reword) without a wired Runner reports and creates nothing", "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    BufferView          view = fixture.View();

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Reword);

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("BeginVcsCommitMessage(reword) with no vcs provider reports the runner's error and creates no buffer",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry(); // no provider registered at all
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Reword);

    REQUIRE(fixture.statusMessage == "vcs commit reword: no vcs provider registered for this project");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("Re-running vcs-commit as vcs-reword-commit on an already-open commit buffer just marks it for "
          "reword, without re-fetching or touching its content",
          "[BufferView][Vcs]") {
    Fixture             fixture;
    CommitTempFileGuard tempGuard;
    ProjectRootGuard    rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Commit);
    ned::text::Buffer* firstOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(firstOpen != nullptr);
    firstOpen->SetPoint(0);
    firstOpen->InsertAtPoint("My in-progress message\n");

    fixture.activeBuffer.Set(fixture.original);                              // simulate switching away
    view.BeginVcsCommitMessageForTesting(BufferView::VcsCommitMode::Reword); // re-run as vcs-reword-commit instead

    ned::text::Buffer* secondOpen = fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath());
    REQUIRE(secondOpen == firstOpen);
    REQUIRE(secondOpen->Text().find("My in-progress message") != std::string::npos);
    REQUIRE(&fixture.activeBuffer.Get() == secondOpen);

    // The pending mode itself only shows up in which Provider method
    // FinishVcsCommitMessage calls -- DetectOnlyProvider's CommitArgv and
    // RewordCommitArgv default-throw distinct text, so this proves
    // RequestRewordCommit fired, not RequestCommit.
    ned::editor::vcs::RegisterProvider("fake", std::make_unique<DetectOnlyProvider>());
    view.FinishVcsCommitMessageForTesting();
    REQUIRE(fixture.statusMessage == "vcs commit: reword commit not supported by this provider");
}

// VcsPanel commit-variants follow-up: ExtendCommit's own synchronous guard
// paths -- unlike Commit/AmendCommit it never opens a buffer at all, so
// there's no *ForTesting seam split into Begin/Finish, just one call.

TEST_CASE("ExtendCommit without a wired Runner reports and touches no buffer", "[BufferView][Vcs]") {
    Fixture    fixture;
    BufferView view = fixture.View();

    view.ExtendCommitForTesting();

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr);
}

TEST_CASE("ExtendCommit with no vcs provider reports the runner's own error", "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    ned::editor::vcs::ClearRegistry(); // no provider registered at all
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.ExtendCommitForTesting();

    REQUIRE(fixture.statusMessage == "vcs extend commit: no vcs provider registered for this project");
    REQUIRE(fixture.bufferList.FindByPath(ned::editor::vcs::CommitMessagePath()) == nullptr); // no buffer, ever
}

TEST_CASE("ExtendCommit with a provider missing extend-commit reports its own 'not supported' answer",
          "[BufferView][Vcs]") {
    Fixture          fixture;
    ProjectRootGuard rootGuard("/repo");
    ned::editor::vcs::ClearRegistry();
    ned::editor::vcs::RegisterProvider("fake", std::make_unique<DetectOnlyProvider>());
    ned::ui::EventLoop          eventLoop;
    ned::editor::vcs::Runner runner(eventLoop);
    BufferView                  view = fixture.View();
    view.SetVcsRunner(&runner);

    view.ExtendCommitForTesting();

    REQUIRE(fixture.statusMessage == "vcs extend commit: extend commit not supported by this provider");
}
