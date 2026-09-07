#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewHunkNavigationTest.cpp's own Fixture shape.
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

TEST_CASE("M-o resolves the conflict hunk at point by taking ours", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("ours"));
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Alt('o'));
    REQUIRE(fixture.buffer.Text() == "before\nours\nafter\n");
}

TEST_CASE("M-t/M-b/M-d/M-k resolve via theirs/both/neither/keep-base", "[BufferView][ConflictResolution]") {
    {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
        fixture.buffer.SetPoint(0); // InsertAtPoint leaves point at the end, outside the hunk
        BufferView view = fixture.View();
        view.OnEvent(ned::ui::test::Alt('t'));
        REQUIRE(fixture.buffer.Text() == "theirs\n");
    }
    {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
        fixture.buffer.SetPoint(0);
        BufferView view = fixture.View();
        view.OnEvent(ned::ui::test::Alt('b'));
        REQUIRE(fixture.buffer.Text() == "ours\ntheirs\n");
    }
    {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
        fixture.buffer.SetPoint(fixture.buffer.Text().find("ours"));
        BufferView view = fixture.View();
        view.OnEvent(ned::ui::test::Alt('d'));
        REQUIRE(fixture.buffer.Text() == "before\nafter\n");
    }
    {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n||||||| base\nbase\n=======\ntheirs\n>>>>>>> b\n");
        fixture.buffer.SetPoint(0);
        BufferView view = fixture.View();
        view.OnEvent(ned::ui::test::Alt('k'));
        REQUIRE(fixture.buffer.Text() == "base\n");
    }
}

TEST_CASE("M-n/M-p navigate between unresolved conflict hunks, wrapping", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    const std::string text = "<<<<<<< a\nx\n=======\ny\n>>>>>>> b\n"
                              "middle\n"
                              "<<<<<<< a\np\n=======\nq\n>>>>>>> b\n";
    fixture.buffer.InsertAtPoint(text);
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    const std::size_t secondHunk = text.rfind("<<<<<<<");

    view.OnEvent(ned::ui::test::Alt('n'));
    REQUIRE(fixture.buffer.Point() == secondHunk);

    view.OnEvent(ned::ui::test::Alt('n')); // wraps back to the first
    REQUIRE(fixture.buffer.Point() == 0);

    view.OnEvent(ned::ui::test::Alt('p')); // wraps to the last
    REQUIRE(fixture.buffer.Point() == secondHunk);
}

TEST_CASE("M-o falls through to its ordinary global binding once no conflicts remain", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("plain text, no markers\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Alt('o')); // ordinarily switch-header-source; must NOT touch buffer text
    REQUIRE(fixture.buffer.Text() == "plain text, no markers\n");
}

TEST_CASE("Ctrl-Alt-o is not a conflict quick key even with an unresolved hunk", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    const std::string text = "<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n";
    fixture.buffer.InsertAtPoint(text);
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::CtrlAlt('o'));
    REQUIRE(fixture.buffer.Text() == text); // untouched -- Control rules out the quick-key path
}
