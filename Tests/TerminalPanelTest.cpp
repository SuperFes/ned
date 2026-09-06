//
// TerminalPanel (Source/UI/TerminalPanel.h) -- headless coverage of the
// drawer widget over its two test seams: Feed() injects "pty output" bytes
// deterministically (no shell, no EventLoop -- EnsureStarted is a no-op with
// no EventLoop set) and SetWriteSinkForTesting captures the bytes a keypress
// would have sent to the pty. Painting is asserted per-cell via
// Screen::PixelAt, the BufferViewTest convention.
//
// tabbed-bottom-dock-overlays follow-up: this panel no longer owns a title
// row or any button chrome of its own -- PanelDock.h's shared tab strip does
// that now (see PanelDockTest.cpp), including the search icon that used to
// live in this panel's own title row (EnterSearch() is called directly here
// instead of via a button click) and the minimize/maximize/close buttons
// (Maximized()/SetOnLayoutChange moved to PanelDock; CloseSession() is still
// this panel's own API, now reached via a dock tab-strip action instead of a
// button this panel drew itself). Every Canvas this panel is handed is pure
// content, so painted/clicked coordinates below need no "+1 past the title
// row" adjustment the way they used to.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Clipboard.h"
#include "Editor/Terminal/Config.h"
#include "TestEvents.h"
#include "UI/TerminalPanel.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::MouseEvent;
using ned::ui::Screen;
using ned::ui::TerminalPanel;
using ned::ui::Theme;

// scrollback-search-and-selection follow-up: Tests/ClipboardTestGuard.cpp
// forces ClipboardEnabled() false for the whole ned_tests binary -- mirrors
// ClipboardTest.cpp's own RestoreClipboardDisabled exactly (kept file-local
// here too, the same small-duplication precedent BufferViewTest.cpp's own
// RestorePrimaryPasteDisabled follows).
struct RestoreClipboardDisabled {
    ~RestoreClipboardDisabled() {
        ned::editor::SetClipboardCopyCommand({});
        ned::editor::SetClipboardEnabled(false);
    }
};

constexpr int kWidth  = 60;
constexpr int kHeight = 4; // pure content rows -- no title row of its own anymore

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

TEST_CASE("TerminalPanel paints fed content starting at row 0", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("hi");
    f.Paint();

    REQUIRE(f.screen.PixelAt(0, 0).character == "h");
    REQUIRE(f.screen.PixelAt(1, 0).character == "i");
    REQUIRE(f.panel.TitleText() == "Terminal");
}

TEST_CASE("TerminalPanel resolves colors against the theme and passes SGR through", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("\x1b[31mr\x1b[0md");
    f.Paint();

    REQUIRE(f.screen.PixelAt(0, 0).foreground_color == ned::ui::Color::Palette(1));
    // Terminal-default colors become the theme's own -- including for blank
    // cells, which is what makes the drawer opaque.
    REQUIRE(f.screen.PixelAt(1, 0).foreground_color == f.theme.defaultForeground);
    REQUIRE(f.screen.PixelAt(1, 0).background_color == f.theme.background);
    REQUIRE(f.screen.PixelAt(10, 2).background_color == f.theme.background);
}

TEST_CASE("TerminalPanel reports the cursor at the emulator's own position", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("ab");

    const auto cursor = f.panel.CursorPosition();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->x == 2);
    REQUIRE(cursor->y == 0);

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

TEST_CASE("SendText writes raw bytes straight to the write sink, no key encoding involved", "[TerminalPanel]") {
    Fixture     f;
    std::string sent;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });

    f.panel.SendText("cd '/some/dir'\n");

    REQUIRE(sent == "cd '/some/dir'\n");
}

TEST_CASE("SendText is a safe no-op with no write sink set", "[TerminalPanel]") {
    Fixture f;

    f.panel.SendText("cd /tmp\n"); // must not crash
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
    REQUIRE(narrow.PixelAt(5, 0).character == "f");
    REQUIRE(narrow.PixelAt(0, 1).character == "g");
}

TEST_CASE("TerminalPanel shows scrollback via Shift+PageUp and snaps back on typing", "[TerminalPanel]") {
    Fixture f;
    // 4 content rows; 8 lines pushes 4 into the ring.
    f.panel.Feed("l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8");
    f.Paint();
    REQUIRE(f.RowText(0) == "l5");

    REQUIRE(f.panel.OnEvent(ShiftPage(true)));
    f.Paint();
    REQUIRE(f.RowText(0) == "l2");
    REQUIRE(f.panel.TitleText().find("(scrollback)") != std::string::npos);
    REQUIRE_FALSE(f.panel.CursorPosition().has_value());

    f.panel.SetWriteSinkForTesting([](std::string_view) {});
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('x')));
    f.Paint();
    REQUIRE(f.RowText(0) == "l5"); // snapped back to live
}

TEST_CASE("TerminalPanel paints the exited state and swallows keys in it", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("done");
    f.panel.HandleExitForTesting();
    f.Paint();

    REQUIRE(f.panel.TitleText().find("(exited)") != std::string::npos);
    bool found = false;
    for (int y = 0; y < kHeight; ++y) {
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

TEST_CASE("TerminalPanel::CloseSession kills the session and starts fresh", "[TerminalPanel]") {
    Fixture f;
    f.panel.Feed("hi");
    f.Paint();
    REQUIRE(f.RowText(0) == "hi");

    // Reached today via a PanelDock tab-strip action (this tab's own
    // contributed extra action), not a button this panel draws itself.
    f.panel.CloseSession();
    f.Paint();
    REQUIRE(f.RowText(0).empty());
    REQUIRE_FALSE(f.panel.ShellRunning());
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

TEST_CASE("TerminalPanel drag-selects text, highlights it, and copies it on release", "[TerminalPanel]") {
    const RestoreClipboardDisabled restore;

    Fixture f;
    f.panel.Feed("hello world");

    // "world" spans columns 6-10 on content row 0.
    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(6, 0, MouseEvent::Button::Left, MouseEvent::Motion::Pressed)));
    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(10, 0, MouseEvent::Button::Left, MouseEvent::Motion::Moved)));

    f.Paint();
    REQUIRE(f.screen.PixelAt(6, 0).background_color == f.theme.selectionBackground);
    REQUIRE(f.screen.PixelAt(10, 0).background_color == f.theme.selectionBackground);
    // Outside the dragged range: untouched.
    REQUIRE(f.screen.PixelAt(5, 0).background_color != f.theme.selectionBackground);

    const std::filesystem::path fakeClipboard = std::filesystem::temp_directory_path() / "ned_terminal_panel_test_fake_clipboard";
    std::filesystem::remove(fakeClipboard);
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardCopyCommand({"sh", "-c", "cat > " + fakeClipboard.string()});

    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(10, 0, MouseEvent::Button::Left, MouseEvent::Motion::Released)));

    REQUIRE(std::filesystem::exists(fakeClipboard));
    std::ifstream copied(fakeClipboard);
    std::string   copiedText((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    REQUIRE(copiedText == "world");
    std::filesystem::remove(fakeClipboard);

    // The highlight survives the release (real-terminal convention) and
    // only clears once the user types again.
    f.Paint();
    REQUIRE(f.screen.PixelAt(6, 0).background_color == f.theme.selectionBackground);
    f.panel.SetWriteSinkForTesting([](std::string_view) {});
    f.panel.OnEvent(ned::ui::test::Character('x'));
    f.Paint();
    REQUIRE(f.screen.PixelAt(6, 0).background_color != f.theme.selectionBackground);
}

TEST_CASE("TerminalPanel selection spans multiple lines in reading order", "[TerminalPanel]") {
    const RestoreClipboardDisabled restore;

    Fixture f;
    f.panel.Feed("ab\r\ncd\r\nef");

    // Drag from column 1 of row 0 ('b') down to column 0 of row 2 ('e').
    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(1, 0, MouseEvent::Button::Left, MouseEvent::Motion::Pressed)));
    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(0, 2, MouseEvent::Button::Left, MouseEvent::Motion::Moved)));

    const std::filesystem::path fakeClipboard = std::filesystem::temp_directory_path() / "ned_terminal_panel_test_fake_clipboard_multiline";
    std::filesystem::remove(fakeClipboard);
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardCopyCommand({"sh", "-c", "cat > " + fakeClipboard.string()});

    REQUIRE(f.panel.OnEvent(ned::ui::test::Mouse(0, 2, MouseEvent::Button::Left, MouseEvent::Motion::Released)));

    std::ifstream copied(fakeClipboard);
    std::string   copiedText((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    REQUIRE(copiedText == "b\ncd\ne");
    std::filesystem::remove(fakeClipboard);
}

TEST_CASE("TerminalPanel plain click clears any prior selection without copying", "[TerminalPanel]") {
    const RestoreClipboardDisabled restore;

    Fixture f;
    f.panel.Feed("hello world");
    f.panel.OnEvent(ned::ui::test::Mouse(6, 0, MouseEvent::Button::Left, MouseEvent::Motion::Pressed));
    f.panel.OnEvent(ned::ui::test::Mouse(10, 0, MouseEvent::Button::Left, MouseEvent::Motion::Moved));
    f.panel.OnEvent(ned::ui::test::Mouse(10, 0, MouseEvent::Button::Left, MouseEvent::Motion::Released));

    // A fresh press-and-release with no drag in between is a plain click --
    // it must not re-copy the stale selection, and it clears the highlight.
    const std::filesystem::path fakeClipboard = std::filesystem::temp_directory_path() / "ned_terminal_panel_test_fake_clipboard_no_copy";
    std::filesystem::remove(fakeClipboard);
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardCopyCommand({"sh", "-c", "cat > " + fakeClipboard.string()});
    f.panel.OnEvent(ned::ui::test::Mouse(2, 1, MouseEvent::Button::Left, MouseEvent::Motion::Pressed));
    f.panel.OnEvent(ned::ui::test::Mouse(2, 1, MouseEvent::Button::Left, MouseEvent::Motion::Released));

    REQUIRE_FALSE(std::filesystem::exists(fakeClipboard));
    f.Paint();
    REQUIRE(f.screen.PixelAt(6, 0).background_color != f.theme.selectionBackground);
}

TEST_CASE("TerminalPanel::EnterSearch finds a scrolled-back line and Escape restores the view", "[TerminalPanel]") {
    Fixture f;
    // 4 content rows; 8 lines pushes l1-l4 into the ring, l5-l8 stay live.
    f.panel.Feed("l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8");
    f.Paint();
    REQUIRE(f.RowText(0) == "l5");

    // Reached today via a PanelDock tab-strip action, not a button this
    // panel draws itself.
    f.panel.EnterSearch();

    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('l')));
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('3')));

    f.Paint();
    REQUIRE(f.panel.TitleText().find("Backward I-search: l3") != std::string::npos);
    REQUIRE(f.RowText(2) == "l3");
    REQUIRE(f.screen.PixelAt(0, 2).background_color == f.theme.isearchMatchBackground);

    // While a session is active, every key is consumed rather than
    // forwarded to the shell -- see the header comment's modality rule.
    std::string sent;
    f.panel.SetWriteSinkForTesting([&sent](std::string_view data) { sent += data; });
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('z')));
    REQUIRE(sent.empty());

    REQUIRE(f.panel.OnEvent(ned::ui::test::Escape()));
    f.Paint();
    REQUIRE(f.RowText(0) == "l5"); // back to the live view
    REQUIRE(f.panel.TitleText().find("I-search") == std::string::npos);

    // Now that the session ended, typing forwards to the shell again.
    REQUIRE(f.panel.OnEvent(ned::ui::test::Character('z')));
    REQUIRE(sent == "z");
}
