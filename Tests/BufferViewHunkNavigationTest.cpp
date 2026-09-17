#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/Provider.h"
#include "Editor/Vim/Settings.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::vcs::DiffHunk;
using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture shape.
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
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// ModeEnabled is process-wide state (Editor/Vim/Settings.h) -- same guard shape as
// BufferViewTest.cpp's own VimModeGuard, restoring whatever was configured before the
// test ran (default false) rather than unconditionally disabling it.
class VimModeGuard {
  public:
    VimModeGuard() : previous_(ned::editor::vim::ModeEnabled()) {
        ned::editor::vim::SetModeEnabled(true);
    }
    ~VimModeGuard() {
        ned::editor::vim::SetModeEnabled(previous_);
    }
    VimModeGuard(const VimModeGuard&)            = delete;
    VimModeGuard& operator=(const VimModeGuard&) = delete;

  private:
    bool previous_;
};

} // namespace

TEST_CASE("JumpToNextHunk/JumpToPreviousHunk are a no-op with no diff loaded", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.JumpToNextHunkForTesting();
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE(fixture.statusMessage == "No changes in this buffer.");

    view.JumpToPreviousHunkForTesting();
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE(fixture.statusMessage == "No changes in this buffer.");
}

TEST_CASE("JumpToNextHunk walks forward through every hunk then stops", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    // Same hunk set BufferViewDiffGutterTest.cpp's own gutter-rendering test
    // uses: Added at 0-indexed line 1, Modified at line 2, Removed boundary
    // at line 3.
    view.DispatchDiffForTesting({DiffHunk{1, 0, 2, 1}, DiffHunk{3, 1, 3, 1}, DiffHunk{5, 1, 3, 0}});

    const ned::text::ITextStorage& content = fixture.buffer.Content();

    view.JumpToNextHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 1);
    REQUIRE(fixture.statusMessage.empty());

    view.JumpToNextHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 2);

    view.JumpToNextHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 3);

    // No more hunks below point -- a no-op, point stays put.
    view.JumpToNextHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 3);
    REQUIRE(fixture.statusMessage == "No more changed hunks below point.");
}

TEST_CASE("JumpToPreviousHunk walks backward through every hunk then stops", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    BufferView view = fixture.View();
    view.DispatchDiffForTesting({DiffHunk{1, 0, 2, 1}, DiffHunk{3, 1, 3, 1}, DiffHunk{5, 1, 3, 0}});

    const ned::text::ITextStorage& content = fixture.buffer.Content();
    fixture.buffer.SetPoint(content.LineToByteOffset(3));

    view.JumpToPreviousHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 2);
    REQUIRE(fixture.statusMessage.empty());

    view.JumpToPreviousHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 1);

    // No more hunks above point -- a no-op, point stays put.
    view.JumpToPreviousHunkForTesting();
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 1);
    REQUIRE(fixture.statusMessage == "No more changed hunks above point.");
}

// vim-hunk-nav follow-up: "]c"/"[c" (gitsigns' own convention) as native Vim Normal-mode
// triggers for the same navigation "vcs-next-hunk"/"vcs-previous-hunk" (C-c v N/P)
// already perform -- JumpToNextHunk/JumpToPreviousHunk themselves are already covered
// above, this only exercises the vim key path (Engine::HandleBracketPrefixed ->
// TakePendingHunkNavigation -> BufferView::HandleVimKey).
TEST_CASE("]c and [c walk hunks under Vim mode", "[BufferView][Vcs][Vim]") {
    VimModeGuard vimGuard;
    Fixture      fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.DispatchDiffForTesting({DiffHunk{1, 0, 2, 1}, DiffHunk{3, 1, 3, 1}, DiffHunk{5, 1, 3, 0}});

    const ned::text::ITextStorage& content = fixture.buffer.Content();

    view.OnEvent(ned::ui::test::Character(']'));
    view.OnEvent(ned::ui::test::Character('c'));
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 1);
    REQUIRE(fixture.statusMessage.empty());

    view.OnEvent(ned::ui::test::Character(']'));
    view.OnEvent(ned::ui::test::Character('c'));
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 2);

    view.OnEvent(ned::ui::test::Character('['));
    view.OnEvent(ned::ui::test::Character('c'));
    REQUIRE(content.ByteOffsetToLine(fixture.buffer.Point()) == 1);
    REQUIRE(fixture.statusMessage.empty());
}

// A bare "[" or "]" with no following "c" (or followed by anything else) is a silent
// no-op -- no other bracket-suffix command exists yet, matching HandleGPrefixed/
// HandleZPrefixed's own fall-through for an unrecognized suffix.
TEST_CASE("A bracket suffix other than c does not navigate hunks", "[BufferView][Vcs][Vim]") {
    VimModeGuard vimGuard;
    Fixture      fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.DispatchDiffForTesting({DiffHunk{1, 0, 2, 1}, DiffHunk{3, 1, 3, 1}, DiffHunk{5, 1, 3, 0}});

    view.OnEvent(ned::ui::test::Character(']'));
    view.OnEvent(ned::ui::test::Character('x'));
    REQUIRE(fixture.buffer.Point() == 0);
}
