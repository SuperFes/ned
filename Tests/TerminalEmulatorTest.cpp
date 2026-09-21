//
// Emulator (Source/Editor/Terminal/) -- deterministic byte-stream-in /
// cell-grid-out coverage of the libvterm wrapper, no pty involved anywhere.
// SendKey fixtures reuse TestEvents.h's ncinput factories, which construct
// the exact shapes Notcurses delivers live (pre-uppercased Ctrl letters,
// the legacy `alt`-bool-only Alt press, kitty RELEASE events) -- see
// KeyTranslation.cpp/TestEvents.cpp for the provenance of each shape.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Terminal/Emulator.h"
#include "TestEvents.h"

using ned::editor::terminal::Emulator;
using ned::ui::Color;

namespace {

// Collects a whole emulated row's characters, trailing blanks trimmed.
std::string RowText(const Emulator& emulator, int row) {
    std::string text;
    for (int col = 0; col < emulator.Cols(); ++col) {
        text += emulator.CellAt(row, col).character;
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

} // namespace

TEST_CASE("Emulator places plain text into cells", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("hello");

    REQUIRE(RowText(emulator, 0) == "hello");
    REQUIRE(emulator.CellAt(0, 0).character == "h");
    REQUIRE(emulator.CellAt(0, 4).character == "o");
    REQUIRE(emulator.CellAt(1, 0).character == " ");

    const auto cursor = emulator.Cursor();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->y == 0);
    REQUIRE(cursor->x == 5);
}

TEST_CASE("Emulator maps SGR attributes and colors", "[TerminalEmulator]") {
    Emulator emulator(4, 40);
    emulator.Feed("\x1b[31mr\x1b[0m\x1b[1mb\x1b[0m\x1b[38;2;10;20;30mt\x1b[0m\x1b[4mu\x1b[0m\x1b[7mi\x1b[0m");

    REQUIRE(emulator.CellAt(0, 0).foreground == Color::Palette(1));
    REQUIRE(emulator.CellAt(0, 1).bold);
    REQUIRE(emulator.CellAt(0, 2).foreground == Color::RGB(10, 20, 30));
    REQUIRE(emulator.CellAt(0, 3).underlined);
    REQUIRE(emulator.CellAt(0, 4).inverted);

    // Unstyled cells report the terminal defaults, which TerminalPanel
    // resolves against ned's own theme at paint time.
    REQUIRE(emulator.CellAt(1, 0).foreground == Color::Default);
    REQUIRE(emulator.CellAt(1, 0).background == Color::Default);
}

TEST_CASE("Emulator honors cursor positioning", "[TerminalEmulator]") {
    Emulator emulator(6, 20);
    emulator.Feed("\x1b[3;5Hx");

    REQUIRE(emulator.CellAt(2, 4).character == "x");
    const auto cursor = emulator.Cursor();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->y == 2);
    REQUIRE(cursor->x == 5);
}

TEST_CASE("Emulator hides and re-shows the cursor via DECTCEM", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    emulator.Feed("\x1b[?25l");
    REQUIRE_FALSE(emulator.Cursor().has_value());

    emulator.Feed("\x1b[?25h");
    REQUIRE(emulator.Cursor().has_value());
}

TEST_CASE("Emulator wraps at the right edge", "[TerminalEmulator]") {
    Emulator emulator(4, 5);
    emulator.Feed("abcdefg");

    REQUIRE(RowText(emulator, 0) == "abcde");
    REQUIRE(RowText(emulator, 1) == "fg");
}

TEST_CASE("Emulator restores the primary screen after the alternate screen", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("primary");

    emulator.Feed("\x1b[?1049h\x1b[2J\x1b[Halternate");
    REQUIRE(RowText(emulator, 0) == "alternate");

    emulator.Feed("\x1b[?1049l");
    REQUIRE(RowText(emulator, 0) == "primary");
}

TEST_CASE("Emulator pushes scrolled-off lines into scrollback", "[TerminalEmulator]") {
    Emulator emulator(3, 20);
    emulator.Feed("one\r\ntwo\r\nthree\r\nfour\r\nfive");

    REQUIRE(emulator.ScrollbackSize() == 2);
    std::string oldest;
    for (int col = 0; col < 3; ++col) {
        oldest += emulator.ScrollbackCellAt(0, col).character;
    }
    REQUIRE(oldest == "one");
    REQUIRE(RowText(emulator, 0) == "three");
}

TEST_CASE("Emulator restores scrollback lines when regrown", "[TerminalEmulator]") {
    Emulator emulator(3, 20);
    emulator.Feed("one\r\ntwo\r\nthree\r\nfour\r\nfive");
    REQUIRE(emulator.ScrollbackSize() == 2);

    emulator.Resize(5, 20);
    REQUIRE(emulator.ScrollbackSize() == 0);
    REQUIRE(RowText(emulator, 0) == "one");
    REQUIRE(RowText(emulator, 4) == "five");
}

TEST_CASE("Emulator reflows content through a shrink", "[TerminalEmulator]") {
    Emulator emulator(4, 10);
    emulator.Feed("abc");

    emulator.Resize(4, 6);
    REQUIRE(emulator.Cols() == 6);
    REQUIRE(RowText(emulator, 0) == "abc");
}

TEST_CASE("Emulator caps the scrollback ring", "[TerminalEmulator]") {
    Emulator    emulator(2, 4);
    std::string feed;
    for (int i = 0; i < Emulator::kScrollbackLines + 40; ++i) {
        feed += "x\r\n";
    }
    emulator.Feed(feed);

    REQUIRE(emulator.ScrollbackSize() == Emulator::kScrollbackLines);
}

TEST_CASE("Emulator encodes plain and special keys for the pty", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    REQUIRE(emulator.SendKey(ned::ui::test::Character('a').raw()));
    REQUIRE(emulator.TakeOutput() == "a");

    REQUIRE(emulator.SendKey(ned::ui::test::Return().raw()));
    REQUIRE(emulator.TakeOutput() == "\r");

    REQUIRE(emulator.SendKey(ned::ui::test::ArrowUp().raw()));
    REQUIRE(emulator.TakeOutput() == "\x1b[A");

    REQUIRE(emulator.SendKey(ned::ui::test::F(5).raw()));
    REQUIRE(emulator.TakeOutput() == "\x1b[15~");
}

TEST_CASE("Emulator honors DECCKM for cursor keys", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("\x1b[?1h");

    REQUIRE(emulator.SendKey(ned::ui::test::ArrowUp().raw()));
    REQUIRE(emulator.TakeOutput() == "\x1bOA");
}

TEST_CASE("Emulator encodes Ctrl letters from the pre-uppercased Notcurses shape", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    REQUIRE(emulator.SendKey(ned::ui::test::Ctrl('c').raw()));
    REQUIRE(emulator.TakeOutput() == "\x03");

    REQUIRE(emulator.SendKey(ned::ui::test::Ctrl('d').raw()));
    REQUIRE(emulator.TakeOutput() == "\x04");
}

TEST_CASE("Emulator encodes Alt from the legacy alt-bool-only shape", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    REQUIRE(emulator.SendKey(ned::ui::test::LegacyAlt('f').raw()));
    REQUIRE(emulator.TakeOutput() == "\x1b"
                                     "f");
}

TEST_CASE("Emulator ignores input with no terminal meaning", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    ncinput release{};
    release.id     = 'a';
    release.evtype = NCTYPE_RELEASE;
    REQUIRE_FALSE(emulator.SendKey(release));

    ncinput bareShift{};
    bareShift.id     = NCKEY_LSHIFT;
    bareShift.evtype = NCTYPE_PRESS;
    REQUIRE_FALSE(emulator.SendKey(bareShift));

    REQUIRE_FALSE(emulator.SendKey(ncinput{}));
    REQUIRE(emulator.TakeOutput().empty());
}

// terminal-mouse-and-osc-relay follow-up. SGR reporting (DECSET 1006) is
// enabled alongside every mouse mode below so the encoded report is legible
// in the assertion rather than a run of raw high bytes; the coordinates in
// it are 1-based, so a press at row 2 / column 3 reads as `3;4`.
namespace {

void Drain(Emulator& emulator) {
    static_cast<void>(emulator.TakeOutput());
}

ned::ui::MouseEvent MouseAt(int column, int row, ned::ui::MouseEvent::Button button, ned::ui::MouseEvent::Motion motion) {
    return ned::ui::MouseEvent{.at = ned::ui::Point{.x = column, .y = row}, .button = button, .motion = motion};
}

} // namespace

TEST_CASE("Emulator tracks the mouse reporting mode an application asks for", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    REQUIRE(emulator.MouseReporting() == Emulator::MouseMode::None);

    emulator.Feed("\x1b[?1000h");
    REQUIRE(emulator.MouseReporting() == Emulator::MouseMode::Click);

    emulator.Feed("\x1b[?1002h");
    REQUIRE(emulator.MouseReporting() == Emulator::MouseMode::Drag);

    emulator.Feed("\x1b[?1003h");
    REQUIRE(emulator.MouseReporting() == Emulator::MouseMode::Move);

    emulator.Feed("\x1b[?1003l\x1b[?1002l\x1b[?1000l");
    REQUIRE(emulator.MouseReporting() == Emulator::MouseMode::None);
}

TEST_CASE("Emulator emits nothing for a mouse event no application asked for", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    REQUIRE_FALSE(emulator.SendMouse(MouseAt(3, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(emulator.TakeOutput().empty());
}

TEST_CASE("Emulator encodes a click and its release for a reporting application", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("\x1b[?1000h\x1b[?1006h");
    Drain(emulator); // libvterm's own DECSET acknowledgements, if any

    REQUIRE(emulator.SendMouse(MouseAt(3, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(emulator.TakeOutput() == "\x1b[<0;4;3M");

    REQUIRE(emulator.SendMouse(MouseAt(3, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released)));
    REQUIRE(emulator.TakeOutput() == "\x1b[<0;4;3m");
}

TEST_CASE("Emulator carries modifiers into a mouse report", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("\x1b[?1000h\x1b[?1006h");
    Drain(emulator);

    ned::ui::MouseEvent mouse = MouseAt(6, 5, ned::ui::MouseEvent::Button::Right, ned::ui::MouseEvent::Motion::Pressed);
    mouse.control             = true;

    REQUIRE(emulator.SendMouse(mouse));
    REQUIRE(emulator.TakeOutput() == "\x1b[<18;7;6M"); // button 3 (2) + Ctrl (16)
}

TEST_CASE("Emulator encodes a wheel notch with no release half", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("\x1b[?1000h\x1b[?1006h");
    Drain(emulator);

    REQUIRE(emulator.SendMouse(MouseAt(6, 5, ned::ui::MouseEvent::Button::WheelUp, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(emulator.TakeOutput() == "\x1b[<64;7;6M");

    // A terminal that reports one anyway must not scroll the application
    // twice for one notch.
    REQUIRE(emulator.SendMouse(MouseAt(6, 5, ned::ui::MouseEvent::Button::WheelUp, ned::ui::MouseEvent::Motion::Released)));
    REQUIRE(emulator.TakeOutput().empty());
}

TEST_CASE("Emulator reports motion only once an application asks for drags", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    emulator.Feed("\x1b[?1000h\x1b[?1006h");
    Drain(emulator);

    REQUIRE(emulator.SendMouse(MouseAt(3, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    Drain(emulator);
    // Click mode: the position is recorded, nothing is emitted.
    REQUIRE(emulator.SendMouse(MouseAt(8, 7, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved)));
    REQUIRE(emulator.TakeOutput().empty());

    emulator.Feed("\x1b[?1002h");
    Drain(emulator);
    REQUIRE(emulator.SendMouse(MouseAt(9, 8, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved)));
    REQUIRE(emulator.TakeOutput() == "\x1b[<32;10;9M"); // 32: motion with button 1 held
}

TEST_CASE("Emulator captures the title an application sets", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    REQUIRE(emulator.Title().empty());

    emulator.Feed("\x1b]0;first\x07");
    REQUIRE(emulator.Title() == "first");

    emulator.Feed("\x1b]2;second\x1b\\");
    REQUIRE(emulator.Title() == "second");
}

TEST_CASE("Emulator hands over an OSC 52 clipboard write, decoded", "[TerminalEmulator]") {
    Emulator emulator(4, 20);
    REQUIRE_FALSE(emulator.TakeClipboardText().has_value());

    emulator.Feed("\x1b]52;c;aGVsbG8gdGhlcmU=\x1b\\");

    const std::optional<std::string> text = emulator.TakeClipboardText();
    REQUIRE(text.has_value());
    REQUIRE(*text == "hello there");
    // Drained, not latched.
    REQUIRE_FALSE(emulator.TakeClipboardText().has_value());
}

TEST_CASE("Emulator never answers an OSC 52 clipboard query", "[TerminalEmulator]") {
    Emulator emulator(4, 20);

    emulator.Feed("\x1b]52;c;aGk=\x1b\\");
    REQUIRE(emulator.TakeClipboardText().has_value());
    Drain(emulator);

    emulator.Feed("\x1b]52;c;?\x1b\\");

    REQUIRE(emulator.TakeOutput().empty());
    REQUIRE_FALSE(emulator.TakeClipboardText().has_value());
}
