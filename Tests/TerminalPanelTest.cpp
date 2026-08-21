//
// TerminalPanel (Source/UI/TerminalPanel.h) -- headless coverage of the
// drawer widget over its two test seams: Feed() injects "pty output" bytes
// deterministically (no shell, no EventLoop -- EnsureStarted is a no-op with
// no EventLoop set) and SetWriteSinkForTesting captures the bytes a keypress
// would have sent to the pty. Painting is asserted per-cell via
// Screen::PixelAt, the BufferViewTest convention.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Terminal/Config.h"
#include "TestEvents.h"
#include "UI/TerminalPanel.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Screen;
using ned::ui::TerminalPanel;
using ned::ui::Theme;

constexpr int kWidth  = 44; // wide enough for the longest title suffix plus the [▼][▲][×] buttons
constexpr int kHeight = 5; // 1 title row + 4 content rows

struct Fixture {
    Theme         theme = ned::ui::DarkTheme();
    TerminalPanel panel{theme};
    Screen        screen{kWidth, kHeight};

    Fixture() {
        panel.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
    }

    void Paint() {
        panel.Paint(Canvas(screen, panel.Box_()));
    }

    [[nodiscard]] std::string RowText(int y) {
        std::string text;
        for (int x = 0; x < kWidth; ++x) {
            text += screen.PixelAt(x, y).character;
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        return text;
    }
};

ned::ui::Event ShiftPage(bool up) {
    ncinput input{};
    input.id        = up ? NCKEY_PGUP : NCKEY_PGDOWN;
    input.modifiers = NCKEY_MOD_SHIFT;
    input.evtype    = NCTYPE_PRESS;
    return ned::ui::Event(input);
}

} // namespace

TEST_CASE("TerminalPanel paints a title row and fed content below it", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("hi");
    f.Paint();

    // Row 0 is the divider/title: line glyphs with the embedded title text.
    REQUIRE(f.screen.PixelAt(0, 0).character == "─");
    REQUIRE(f.screen.PixelAt(3, 0).character == "T");
    REQUIRE(f.RowText(0).find("Terminal") != std::string::npos);

    // Content starts on screen row 1.
    REQUIRE(f.screen.PixelAt(0, 1).character == "h");
    REQUIRE(f.screen.PixelAt(1, 1).character == "i");
}

TEST_CASE("TerminalPanel title brush follows focus", "[TerminalPanel]") {
    Fixture f;

    f.Paint();
    REQUIRE(f.screen.PixelAt(0, 0).foreground_color == f.theme.border.foreground);

    f.panel.TakeFocus();
    f.Paint();
    REQUIRE(f.screen.PixelAt(0, 0).foreground_color == f.theme.borderAccent.foreground);
}

TEST_CASE("TerminalPanel resolves colors against the theme and passes SGR through", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("\x1b[31mr\x1b[0md");
    f.Paint();

    REQUIRE(f.screen.PixelAt(0, 1).foreground_color == ned::ui::Color::Palette(1));
    // Terminal-default colors become the theme's own -- including for blank
    // cells, which is what makes the drawer opaque.
    REQUIRE(f.screen.PixelAt(1, 1).foreground_color == f.theme.defaultForeground);
    REQUIRE(f.screen.PixelAt(1, 1).background_color == f.theme.background);
    REQUIRE(f.screen.PixelAt(10, 3).background_color == f.theme.background);
}

TEST_CASE("TerminalPanel reports the cursor below the title row", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("ab");

    const auto cursor = f.panel.CursorPosition();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->x == 2);
    REQUIRE(cursor->y == 1); // emulator row 0 + the title row

    f.panel.Feed("\x1b[?25l");
    REQUIRE_FALSE(f.panel.CursorPosition().has_value());
}

TEST_CASE("TerminalPanel forwards keys to the write sink", "[TerminalPanel]") {
    Fixture     f;
    std::string sent;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });

    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('a')));
    REQUIRE(sent == "a");

    sent.clear();
    REQUIRE(f.panel.OnEvent(ned::ui::test::ArrowUp()));
    REQUIRE(sent == "\x1b[A");

    // C-c belongs to the shell, not the editor, while the panel has focus.
    sent.clear();
    REQUIRE(f.panel.OnEvent(ned::ui::test::Ctrl('c')));
    REQUIRE(sent == "\x03");
}

TEST_CASE("TerminalPanel reserves the toggle chord and never forwards it", "[TerminalPanel]") {
    Fixture     f;
    std::string sent;
    int         toggles = 0;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });
    f.panel.SetOnToggleRequest([&toggles] { ++toggles; });

    ncinput input{};
    input.id        = U'`';
    input.modifiers = NCKEY_MOD_CTRL;
    input.evtype    = NCTYPE_PRESS;
    REQUIRE(f.panel.OnEvent(ned::ui::Event(input)));

    REQUIRE(toggles == 1);
    REQUIRE(sent.empty());
}

TEST_CASE("TerminalPanel propagates its box size into the emulator", "[TerminalPanel]") {
    Fixture f;
    // Shrink to 6 columns: fed text must now wrap at the new width.
    f.panel.SetBox_(Box{.x_min = 0, .x_max = 5, .y_min = 0, .y_max = kHeight - 1});
    f.panel.Feed("abcdefgh");

    Screen narrow(6, kHeight);
    f.panel.Paint(Canvas(narrow, f.panel.Box_()));
    REQUIRE(narrow.PixelAt(5, 1).character == "f");
    REQUIRE(narrow.PixelAt(0, 2).character == "g");
}

TEST_CASE("TerminalPanel shows scrollback via Shift+PageUp and snaps back on typing", "[TerminalPanel]") {
    Fixture f;
    // 4 content rows; 8 lines pushes 4 into the ring.
    f.panel.Feed("l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8");
    f.Paint();
    REQUIRE(f.RowText(1) == "l5");

    REQUIRE(f.panel.OnEvent(ShiftPage(true)));
    f.Paint();
    REQUIRE(f.RowText(1) == "l2");
    REQUIRE(f.RowText(0).find("(scrollback)") != std::string::npos);
    REQUIRE_FALSE(f.panel.CursorPosition().has_value());

    f.panel.SetWriteSinkForTesting([](std::string_view) {});
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('x')));
    f.Paint();
    REQUIRE(f.RowText(1) == "l5"); // snapped back to live
}

TEST_CASE("TerminalPanel paints the exited state and swallows keys in it", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("done");
    f.panel.HandleExitForTesting();
    f.Paint();

    REQUIRE(f.RowText(0).find("(exited)") != std::string::npos);
    bool found = false;
    for (int y = 1; y < kHeight; ++y) {
        if (f.RowText(y).find("[process exited]") != std::string::npos) {
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE_FALSE(f.panel.CursorPosition().has_value());
    REQUIRE_FALSE(f.panel.ShellRunning());

    // Keys are swallowed, not forwarded -- there is nothing to forward to.
    std::string sent;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('a')));
    REQUIRE(sent.empty());
}

TEST_CASE("TerminalHeightPercent clamps to a sane range", "[TerminalPanel]") {
    namespace terminal = ned::editor::terminal;
    const int original = terminal::TerminalHeightPercent();

    terminal::SetTerminalHeightPercent(55);
    REQUIRE(terminal::TerminalHeightPercent() == 55);
    terminal::SetTerminalHeightPercent(0);
    REQUIRE(terminal::TerminalHeightPercent() == 10);
    terminal::SetTerminalHeightPercent(400);
    REQUIRE(terminal::TerminalHeightPercent() == 90);

    terminal::SetTerminalHeightPercent(original);
}

TEST_CASE("TerminalPanel draws bracketed minimize/maximize/close buttons", "[TerminalPanel]") {
    Fixture f;
    f.Paint();

    // Right-aligned [▼][▲][×], one column short of the row's edge.
    REQUIRE(f.screen.PixelAt(kWidth - 10, 0).character == "[");
    REQUIRE(f.screen.PixelAt(kWidth - 9, 0).character == "▼");
    REQUIRE(f.screen.PixelAt(kWidth - 8, 0).character == "]");
    REQUIRE(f.screen.PixelAt(kWidth - 6, 0).character == "▲");
    REQUIRE(f.screen.PixelAt(kWidth - 4, 0).character == "[");
    REQUIRE(f.screen.PixelAt(kWidth - 3, 0).character == "×");
    REQUIRE(f.screen.PixelAt(kWidth - 2, 0).character == "]");
}

TEST_CASE("TerminalPanel close button kills the session, minimize keeps it", "[TerminalPanel]") {
    Fixture f;
    int     toggles = 0;
    f.panel.SetOnToggleRequest([&toggles] { ++toggles; });
    f.panel.Feed("hi");

    // Minimize: toggle fires, session content survives.
    f.panel.OnEvent(ned::ui::test::Mouse(kWidth - 9, 0, ned::ui::MouseEvent::Button::Left,
                                         ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(toggles == 1);
    f.Paint();
    REQUIRE(f.RowText(1) == "hi");

    // Close: toggle fires and the session is gone -- fresh emulator.
    f.panel.OnEvent(ned::ui::test::Mouse(kWidth - 3, 0, ned::ui::MouseEvent::Button::Left,
                                         ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(toggles == 2);
    f.Paint();
    REQUIRE(f.RowText(1).empty());

    // A click elsewhere on the title row focuses instead.
    f.panel.OnEvent(ned::ui::test::Mouse(4, 0, ned::ui::MouseEvent::Button::Left,
                                         ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(toggles == 2);
    REQUIRE(f.panel.Focused());
}

TEST_CASE("TerminalPanel maximize button toggles state and requests a re-layout", "[TerminalPanel]") {
    Fixture f;
    int     layoutChanges = 0;
    f.panel.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });
    REQUIRE_FALSE(f.panel.Maximized());

    f.panel.OnEvent(ned::ui::test::Mouse(kWidth - 6, 0, ned::ui::MouseEvent::Button::Left,
                                         ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(f.panel.Maximized());
    REQUIRE(layoutChanges == 1);

    f.panel.OnEvent(ned::ui::test::Mouse(kWidth - 6, 0, ned::ui::MouseEvent::Button::Left,
                                         ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE_FALSE(f.panel.Maximized());
    REQUIRE(layoutChanges == 2);
}

TEST_CASE("TerminalPanel forwards terminal-query replies without waiting for a keypress", "[TerminalPanel]") {
    Fixture     f;
    std::string sent;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });

    // fish's startup Primary Device Attributes query -- the reply libvterm
    // queues must reach the pty from Feed itself, not sit until a key is
    // pressed (the real, reported 10-second fish timeout).
    f.panel.Feed("\x1b[c");
    REQUIRE(sent == "\x1b[?1;2c");

    sent.clear();
    f.panel.Feed("\x1b[6n"); // cursor-position report
    REQUIRE(sent.find('R') != std::string::npos);
}
