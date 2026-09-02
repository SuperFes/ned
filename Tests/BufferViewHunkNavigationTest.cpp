#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::vcs::VcsDiffHunk;
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
    view.DispatchDiffForTesting({VcsDiffHunk{1, 0, 2, 1}, VcsDiffHunk{3, 1, 3, 1}, VcsDiffHunk{5, 1, 3, 0}});

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
    view.DispatchDiffForTesting({VcsDiffHunk{1, 0, 2, 1}, VcsDiffHunk{3, 1, 3, 1}, VcsDiffHunk{5, 1, 3, 0}});

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
