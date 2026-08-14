#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ox/ox.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ScratchPad.h"
#include "Editor/TabWidth.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/ProjectSidebar.h"
#include "UI/Theme.h"

namespace {

// esc::Key's graphic-character range (Space=32 .. Tilde=126) matches ASCII
// exactly, so a plain char converts straight across -- used to type out a
// whole path/name into a find-file/switch-to-buffer prompt one key_press at
// a time, the same way a real keyboard would feed it.
void TypeText(ned::ui::BufferView& view, std::string_view text) {
    for (const char ch : text) {
        view.key_press(static_cast<esc::Key>(static_cast<unsigned char>(ch)));
    }
}

// project-search/project-replace always search ned::editor::ProjectRoot()
// (no directory prompt in v1 -- see ProjectSearch.h), so tests that exercise
// them need to temporarily relocate both the process's cwd and the project
// root to a controlled scratch directory. Restores both on scope exit even
// if a REQUIRE fails partway through. Mirrors ProjectSidebarTest.cpp's own
// CurrentPathGuard exactly; see its comment for why ProjectRoot() needs
// explicit handling here rather than just following cwd automatically.
class CurrentPathGuard {
  public:
    explicit CurrentPathGuard(const std::filesystem::path& newPath) : previous_(std::filesystem::current_path()), previousRoot_(ned::editor::ProjectRoot()) {
        std::filesystem::current_path(newPath);
        ned::editor::SetProjectRoot(newPath);
    }
    ~CurrentPathGuard() {
        std::filesystem::current_path(previous_);
        ned::editor::SetProjectRoot(previousRoot_);
    }
    CurrentPathGuard(const CurrentPathGuard&)            = delete;
    CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;

  private:
    std::filesystem::path previous_;
    std::filesystem::path previousRoot_;
};

// TabWidth is process-wide state (see Editor/TabWidth.h's own doc comment);
// every test that sets one must restore the default for the next test.
struct TabWidthGuard {
    ~TabWidthGuard() {
        ned::editor::SetTabWidth(4);
    }
};

// find-scratch reads Editor/ScratchPad.h's ScratchDirectory(), which resolves
// from XDG_DATA_HOME/HOME -- mirrors InitFileTest.cpp/ScratchPadTest.cpp's own
// EnvVarGuard exactly, so a scratch created by these tests never lands in the
// real user's actual scratch directory.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;

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

    ned::ui::BufferView View() {
        return ned::ui::BufferView(activeBuffer, killRing, bufferList, dispatcher, statusMessage, mode, theme);
    }
};

std::u32string RowText(ox::ScreenBuffer& screen, int row, int width) {
    std::u32string out;
    for (int col = 0; col < width; ++col) {
        out.push_back(screen[{.x = col, .y = row}].symbol);
    }
    return out;
}

// Mirrors BufferView::GutterWidth's formula: digits in the last line number,
// plus one separating column. Content starts at this column, not 0.
int GutterWidth(std::size_t totalLines) {
    return static_cast<int>(std::to_string(totalLines).size()) + 1;
}

// Row text starting right after the gutter, rather than from column 0.
std::u32string ContentRowText(ox::ScreenBuffer& screen, int row, int width, std::size_t totalLines) {
    std::u32string out;
    const int      gutter = GutterWidth(totalLines);
    for (int col = 0; col < width; ++col) {
        out.push_back(screen[{.x = gutter + col, .y = row}].symbol);
    }
    return out;
}

} // namespace

TEST_CASE("BufferView paints the buffer's first line and positions the cursor", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    ox::ScreenBuffer screen({.width = 10, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 10, .height = 3}};

    view.paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 5, 1) == U"hello");
    REQUIRE(view.cursor.has_value());
    REQUIRE(*view.cursor == ox::Point{.x = GutterWidth(1) + 5, .y = 0});
}

TEST_CASE("A tab character expands to TabWidth() space columns, not one raw codepoint", "[BufferView]") {
    // A raw tab byte sent straight to a real terminal is interpreted as
    // "jump to the next tab stop" rather than "print one glyph," which
    // desyncs Terminal::commit_changes()'s own per-cell diff bookkeeping
    // from what the terminal actually did -- this is the root cause behind
    // a scroll-triggered rendering-corruption bug found via manual testing
    // on a real Makefile. Expanding tabs to literal spaces here is the fix.
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    // "a", 4 space columns for the tab, then "b" -- never a literal U+0009.
    REQUIRE(ContentRowText(screen, 0, 6, 1) == U"a    b");
}

TEST_CASE("Cursor position accounts for tab expansion, not a plain codepoint count", "[BufferView]") {
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");
    fixture.buffer.SetPoint(2); // right before 'b': byte offset 1 for 'a' + 1 for the tab byte itself

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    REQUIRE(view.cursor.has_value());
    // Visual column: 1 ('a') + 4 (the expanded tab) = 5, not 2 (a plain
    // byte/codepoint count of "a\t").
    REQUIRE(*view.cursor == ox::Point{.x = GutterWidth(1) + 5, .y = 0});
}

TEST_CASE("A configured tab width other than the default is respected when painting", "[BufferView]") {
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(2);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 4, 1) == U"a  b");
}

TEST_CASE("mouse_press accounts for tab expansion, not a plain codepoint count", "[BufferView]") {
    // ByteOffsetForMouse follow-up: a click past a tab used to land a column
    // or two off from where it visually looks, since the screen column was
    // fed straight into ByteOffsetForLineAndColumn as if it were a plain
    // codepoint count instead of a tab-expanded visual column.
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tbc"); // 'a'=0, tab spans [1,5), 'b'=5, 'c'=6

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    // Visual column 5 ('b') -- a plain codepoint count would have clamped
    // this to the buffer's actual length (4 codepoints) and landed on 'c'
    // instead of the correct 'b', right after the tab's 4-column span.
    view.mouse_press(ox::Mouse{.at = {.x = GutterWidth(1) + 5, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(fixture.buffer.Point() == 2); // 'b'

    // Visual column 0 ('a') -- unaffected either way, sanity check.
    view.mouse_press(ox::Mouse{.at = {.x = GutterWidth(1) + 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(fixture.buffer.Point() == 0); // 'a'
}

TEST_CASE("A control byte renders as a 4-column hex placeholder, not the raw byte", "[BufferView]") {
    // Binary-rendering follow-up: a raw control byte sent straight to a real
    // terminal isn't "print one glyph" -- some are actual terminal control
    // codes, which desyncs Terminal::commit_changes()'s per-cell diff
    // bookkeeping the exact same way an unexpanded tab byte used to (see the
    // tab-rendering-fix follow-up). Rendered as a safe, printable "◁XX▷"
    // placeholder instead.
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x0E' + "b");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter, .y = 0}].symbol == U'a');
    REQUIRE(screen[{.x = gutter + 1, .y = 0}].symbol == U'◁');
    REQUIRE(screen[{.x = gutter + 2, .y = 0}].symbol == U'0');
    REQUIRE(screen[{.x = gutter + 3, .y = 0}].symbol == U'E');
    REQUIRE(screen[{.x = gutter + 4, .y = 0}].symbol == U'▷');
    REQUIRE(screen[{.x = gutter + 5, .y = 0}].symbol == U'b');
    REQUIRE(screen[{.x = gutter + 1, .y = 0}].brush.foreground == fixture.theme.binaryForeground);
}

TEST_CASE("DEL (0x7F) also renders as a hex placeholder", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x7F' + "b");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 2, .y = 0}].symbol == U'7');
    REQUIRE(screen[{.x = gutter + 3, .y = 0}].symbol == U'F');
}

TEST_CASE("Cursor position accounts for a binary placeholder's 4-column width", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x0E' + "b");
    fixture.buffer.SetPoint(2); // right before 'b': byte offset 1 for 'a' + 1 for the control byte itself

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    REQUIRE(view.cursor.has_value());
    // Visual column: 1 ('a') + 4 (the hex placeholder) = 5.
    REQUIRE(*view.cursor == ox::Point{.x = GutterWidth(1) + 5, .y = 0});
}

TEST_CASE("key_press for a plain character self-inserts and advances the cursor", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    view.key_press(esc::Key::a);
    view.key_press(esc::Key::b);
    view.key_press(esc::Key::c);

    REQUIRE(fixture.buffer.Text() == "abc");
    REQUIRE(fixture.buffer.Point() == 3);
}

TEST_CASE("key_press for an untranslatable key is a safe no-op", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    view.key_press(esc::Key::LCtrl); // raw-mode-only sentinel; TranslateKey returns nullopt

    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("key_press for RET inserts a newline via the bound command", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    view.key_press(esc::Key::a);
    view.key_press(esc::Key::Enter);
    view.key_press(esc::Key::b);

    REQUIRE(fixture.buffer.Text() == "a\nb");
}

TEST_CASE("BufferView renders multiple lines and scrolls to keep point visible", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\nfive");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 2}; // only 2 lines visible at a time

    ox::ScreenBuffer screen({.width = 10, .height = 2});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 10, .height = 2}};

    view.paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 3, 5) == U"one");
    REQUIRE(ContentRowText(screen, 1, 3, 5) == U"two");

    // Move point down to the last line ("five", buffer line index 4) and feed
    // a key press so ScrollToShowPoint runs.
    fixture.buffer.SetPoint(fixture.buffer.Size());
    view.key_press(esc::Key::ArrowRight); // any bound key; forward-char at end of buffer is a no-op edit

    view.paint(canvas);
    REQUIRE(ContentRowText(screen, 1, 4, 5) == U"five"); // last visible row now shows the line point is on
}

TEST_CASE("An exception from a command is caught and reported via the status message, not propagated", "[BufferView]") {
    Fixture fixture;
    fixture.registry.Register("throwing-command", "", [](ned::editor::CommandContext&) {
        throw std::runtime_error("boom");
    });
    fixture.keymap.Bind(ned::editor::ParseKeySequence("C-t"), "throwing-command");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    view.key_press(esc::Key::DeviceControlFour); // Ctrl+t -- must not throw out of key_press
    REQUIRE(fixture.statusMessage == "boom");
}

TEST_CASE("C-x C-c (quit) does not crash key_press", "[BufferView]") {
    // ox::Application::quit() just sets a static flag with no accessible read
    // path from a test; this only confirms the request routes through
    // key_press safely (see Tests/CommandsTest.cpp for proof "quit" itself
    // sets CommandContext::quit).
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    view.key_press(esc::Key::Cancel);    // Ctrl+x
    view.key_press(esc::Key::EndOfText); // Ctrl+c
}

TEST_CASE("Isearch: C-s enters search mode, typing narrows the match, RET accepts", "[BufferView]") {
    // Ctrl+s = DeviceControlThree, Ctrl+r = DeviceControlTwo, Ctrl+g = Bell,
    // per esc::Key's C0-control-code layout (a=StartOfHeading=1, ..., so
    // s is the 19th letter -> DeviceControlThree).
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::DeviceControlThree); // C-s: start isearch-forward
    REQUIRE(fixture.statusMessage == "I-search: ");

    view.key_press(esc::Key::f);
    view.key_press(esc::Key::o);
    view.key_press(esc::Key::x);
    REQUIRE(fixture.statusMessage == "I-search: fox");
    REQUIRE(fixture.buffer.Point() == 19); // right after "fox" (starts at 16, len 19)

    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.buffer.Point() == 19); // point stays at the match

    // Back to normal editing: this must self-insert, not feed the search.
    view.key_press(esc::Key::a);
    REQUIRE(fixture.buffer.Text() == "the quick brown foxa");
}

TEST_CASE("Isearch: Escape cancels and restores the original point", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::DeviceControlThree);
    view.key_press(esc::Key::f);
    view.key_press(esc::Key::o);
    view.key_press(esc::Key::x);
    REQUIRE(fixture.buffer.Point() != 0);

    view.key_press(esc::Key::Escape);
    REQUIRE(fixture.buffer.Point() == 0);

    // Back to normal editing.
    view.key_press(esc::Key::z);
    REQUIRE(fixture.buffer.Text() == "zthe quick brown fox");
}

TEST_CASE("Isearch: C-r starts a backward search", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    // point defaults to end of buffer after InsertAtPoint

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::DeviceControlTwo); // C-r: isearch-backward
    REQUIRE(fixture.statusMessage == "Backward I-search: ");

    view.key_press(esc::Key::f);
    view.key_press(esc::Key::o);
    view.key_press(esc::Key::x);
    REQUIRE(fixture.buffer.Point() == 16); // start of "fox"
}

TEST_CASE("Query-replace: ESC % walks pattern, replacement, and confirmation to completion", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("cat sat on the cat mat");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::Escape);
    view.key_press(esc::Key::Percent);
    REQUIRE(fixture.statusMessage.find("Query replace:") == 0);

    view.key_press(esc::Key::c);
    view.key_press(esc::Key::a);
    view.key_press(esc::Key::t);
    view.key_press(esc::Key::Enter); // confirm pattern "cat"
    REQUIRE(fixture.statusMessage.find("with:") != std::string::npos);

    view.key_press(esc::Key::d);
    view.key_press(esc::Key::o);
    view.key_press(esc::Key::g);
    view.key_press(esc::Key::Enter); // confirm replacement "dog"
    REQUIRE(fixture.statusMessage.find("(y/n/!/q)?") != std::string::npos);

    view.key_press(esc::Key::y); // replace first match
    REQUIRE(fixture.buffer.Text() == "dog sat on the cat mat");

    view.key_press(esc::Key::y); // replace second match, no more after
    REQUIRE(fixture.buffer.Text() == "dog sat on the dog mat");
    REQUIRE(fixture.statusMessage.find("Replaced 2") == 0);

    // Session ended: back to normal editing. Point followed the first
    // replacement (it started at the very position that got replaced), so it
    // now sits right after that "dog", not back at the buffer start.
    view.key_press(esc::Key::z);
    REQUIRE(fixture.buffer.Text() == "dogz sat on the dog mat");
}

TEST_CASE("Query-replace: an invalid pattern reports an error and stays in EnteringPattern", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("text");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::Escape);
    view.key_press(esc::Key::Percent);
    view.key_press(esc::Key::LeftParenthesis); // "(" with no closing paren -> invalid regex
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);

    // Still entering the pattern: DEL should edit it, not do anything else.
    view.key_press(esc::Key::Backspace);
    view.key_press(esc::Key::Escape); // now cancel out entirely
    view.key_press(esc::Key::z);      // back to normal editing
    REQUIRE(fixture.buffer.Text() == "ztext");
}

TEST_CASE("BufferView consults the active Mode's highlightLine hook when painting", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JanetMode();
    fixture.buffer.InsertAtPoint("# a comment");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 1};

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};

    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 0, .y = 0}].brush.foreground == ox::Color{ox::XColor::BrightBlack});
    REQUIRE(screen[{.x = gutter + 5, .y = 0}].brush.foreground == ox::Color{ox::XColor::BrightBlack}); // still inside the comment
}

TEST_CASE("BufferView renders with no highlighting under FundamentalMode", "[BufferView]") {
    Fixture fixture; // FundamentalMode by default
    fixture.buffer.InsertAtPoint("# not actually a comment here");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    view.paint(canvas);

    REQUIRE(screen[{.x = GutterWidth(1), .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::Default));
}

TEST_CASE("BufferView renders JsonMode's tree-sitter highlighting for strings, numbers, and constants",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1, "b": true})");
    // Byte layout: {"a": 1, "b": true}
    //               0123456789...
    // '"' at 1, 'a' at 2, '"' at 3 -- "a" is a String span [1,4)
    // '1' at 6 -- a Number span [6,7)
    // 't' at 14..17 -- "true" is a ConstantBuiltin span [14,18)

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 2, .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::String)); // 'a'
    REQUIRE(screen[{.x = gutter + 6, .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::Number)); // '1'
    REQUIRE(screen[{.x = gutter + 15, .y = 0}].brush ==
            fixture.theme.BrushFor(ned::editor::SyntaxClass::ConstantBuiltin));                                    // 'r' in "true"
    REQUIRE(screen[{.x = gutter + 0, .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::Default)); // '{'
}

TEST_CASE("BufferView's highlight cache updates after an edit changes the buffer's content", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"("a")"); // just a string literal

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    view.paint(canvas);
    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 0, .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::String));

    // Replace the whole buffer with something that has no string at all --
    // if the highlight cache failed to invalidate on this edit, the first
    // column would still incorrectly render as String.
    fixture.buffer.DeleteRange(0, fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("1");

    view.paint(canvas);
    REQUIRE(screen[{.x = gutter + 0, .y = 0}].brush == fixture.theme.BrushFor(ned::editor::SyntaxClass::Number));
}

TEST_CASE("BufferView highlights the region background when a mark is set", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(5); // region = [0, 5) -> "hello"

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 1};

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};

    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 0, .y = 0}].brush.background == ox::Color{fixture.theme.selectionBackground});
    REQUIRE(screen[{.x = gutter + 4, .y = 0}].brush.background == ox::Color{fixture.theme.selectionBackground});
    REQUIRE(screen[{.x = gutter + 5, .y = 0}].brush.background == ox::Color{fixture.theme.background}); // " " -- outside the region
}

TEST_CASE("BufferView highlights the current isearch match", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 1};

    view.key_press(esc::Key::DeviceControlThree); // C-s
    view.key_press(esc::Key::f);
    view.key_press(esc::Key::o);
    view.key_press(esc::Key::x);
    REQUIRE(fixture.buffer.Point() == 19); // right after "fox" (starts at byte 16)

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};
    view.paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen[{.x = gutter + 16, .y = 0}].brush.background == ox::Color{fixture.theme.isearchMatchBackground}); // 'f'
    REQUIRE(screen[{.x = gutter + 18, .y = 0}].brush.background == ox::Color{fixture.theme.isearchMatchBackground}); // 'x'
    REQUIRE(screen[{.x = gutter + 15, .y = 0}].brush.background == ox::Color{fixture.theme.background});             // ' ' before match
}

TEST_CASE("key_press propagates the widget's real height as CommandContext::viewportHeight for paging", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 10}; // floor(10 * 0.65) -> 6 lines

    view.key_press(esc::Key::PageDown);

    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 6);
}

TEST_CASE("mouse_press moves point to the clicked position and clears any existing selection", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(20);
    REQUIRE(fixture.buffer.HasMark());

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.mouse_press(ox::Mouse{.at = {.x = GutterWidth(1) + 4, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(fixture.buffer.Point() == 4);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("mouse_press then mouse_move selects a region from the press position", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    const int gutter = GutterWidth(1);
    view.mouse_press(ox::Mouse{.at = {.x = gutter + 4, .y = 0}, .button = ox::Mouse::Button::Left});
    view.mouse_move(ox::Mouse{.at = {.x = gutter + 10, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 10});

    // Dragging further extends the same selection, anchored at the press position.
    view.mouse_move(ox::Mouse{.at = {.x = gutter + 16, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 16});
}

TEST_CASE("mouse_move with no button held is ignored", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.mouse_move(ox::Mouse{.at = {.x = 10, .y = 0}, .button = ox::Mouse::Button::None});
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("mouse_wheel scrolls the viewport without moving point", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line" + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 5};

    ox::ScreenBuffer screen({.width = 40, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 5}};

    view.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    view.paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(fixture.buffer.Point() == 0);                          // wheel never moves point
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == U"line3"); // scrolled down by 3 lines

    view.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollUp});
    view.paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == U"line0"); // back at the top
}

TEST_CASE("Mouse input is ignored while an isearch session is active", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    view.key_press(esc::Key::DeviceControlThree); // C-s: start isearch-forward
    view.mouse_press(ox::Mouse{.at = {.x = 10, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(fixture.buffer.Point() == 0); // click did not move point mid-session
}

TEST_CASE("The line-number gutter shows right-aligned, 1-indexed line numbers", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\nb\nc\nd\ne\nf\ng\nh\ni\nj"); // 10 lines -> gutter width 3 ("10" + 1 space)
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 10};

    ox::ScreenBuffer screen({.width = 20, .height = 10});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 10}};
    view.paint(canvas);

    REQUIRE(GutterWidth(10) == 3);

    // Row 0 -> line 1: right-aligned in 2 digit columns, then a separator, then content.
    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U' ');
    REQUIRE(screen[{.x = 1, .y = 0}].symbol == U'1');
    REQUIRE(screen[{.x = 2, .y = 0}].symbol == U' ');
    REQUIRE(ContentRowText(screen, 0, 1, 10) == U"a");

    // Row 9 -> line 10: both digit columns used.
    REQUIRE(screen[{.x = 0, .y = 9}].symbol == U'1');
    REQUIRE(screen[{.x = 1, .y = 9}].symbol == U'0');
    REQUIRE(screen[{.x = 2, .y = 9}].symbol == U' ');
    REQUIRE(ContentRowText(screen, 9, 1, 10) == U"j");
}

TEST_CASE("The gutter widens as the buffer grows past a power of ten lines", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 150; ++i) {
        content += "x\n";
    }
    fixture.buffer.InsertAtPoint(content); // 151 lines -> 3 digits + 1 separator
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 1};

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};
    view.paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(totalLines == 151);
    REQUIRE(GutterWidth(totalLines) == 4);
    REQUIRE(ContentRowText(screen, 0, 1, totalLines) == U"x");
}

TEST_CASE("The current line's gutter number is styled distinctly from the rest", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // point on line 1 ("two")

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    const int gutter = GutterWidth(3);
    REQUIRE(screen[{.x = gutter - 2, .y = 0}].brush.foreground == ox::Color{fixture.theme.lineNumberForeground});
    REQUIRE(screen[{.x = gutter - 2, .y = 1}].brush.foreground == ox::Color{fixture.theme.currentLineNumberForeground});
    REQUIRE(screen[{.x = gutter - 2, .y = 2}].brush.foreground == ox::Color{fixture.theme.lineNumberForeground});
}

TEST_CASE("Gutter highlights lines fully or partially inside the selected region, distinctly", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(6); // region = [0, 6) -> all of "one", just "tw" of "two"

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    ox::ScreenBuffer screen({.width = 20, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 3}};
    view.paint(canvas);

    const int gutter = GutterWidth(3);

    // Line 0 ("one") is fully inside the region: both the digit and the gap column highlight.
    REQUIRE(screen[{.x = gutter - 2, .y = 0}].brush.background == ox::Color{fixture.theme.selectionBackground});
    REQUIRE(screen[{.x = gutter - 1, .y = 0}].brush.background == ox::Color{fixture.theme.selectionBackground});

    // Line 1 ("two") is only partially inside: the digit stays plain, only the gap column highlights.
    REQUIRE(screen[{.x = gutter - 2, .y = 1}].brush.background == ox::Color{fixture.theme.background});
    REQUIRE(screen[{.x = gutter - 1, .y = 1}].brush.background == ox::Color{fixture.theme.selectionBackground});

    // Line 2 ("three") is untouched by the region: no highlight anywhere in the gutter.
    REQUIRE(screen[{.x = gutter - 2, .y = 2}].brush.background == ox::Color{fixture.theme.background});
    REQUIRE(screen[{.x = gutter - 1, .y = 2}].brush.background == ox::Color{fixture.theme.background});
}

TEST_CASE("Gutter highlighting is absent when no mark is set", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 2};

    ox::ScreenBuffer screen({.width = 20, .height = 2});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 2}};
    view.paint(canvas);

    const int gutter = GutterWidth(2);
    REQUIRE(screen[{.x = gutter - 2, .y = 0}].brush.background == ox::Color{fixture.theme.background});
    REQUIRE(screen[{.x = gutter - 1, .y = 0}].brush.background == ox::Color{fixture.theme.background});
    REQUIRE(screen[{.x = gutter - 2, .y = 1}].brush.background == ox::Color{fixture.theme.background});
    REQUIRE(screen[{.x = gutter - 1, .y = 1}].brush.background == ox::Color{fixture.theme.background});
}

TEST_CASE("Gutter fully highlights a line selected through to the very end of the buffer", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo");
    fixture.buffer.SetMark(fixture.buffer.Content().LineToByteOffset(1)); // start of "two"
    fixture.buffer.SetPoint(fixture.buffer.Content().ByteLength());       // end of buffer, no trailing newline

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 2};

    ox::ScreenBuffer screen({.width = 20, .height = 2});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 2}};
    view.paint(canvas);

    const int gutter = GutterWidth(2);
    REQUIRE(screen[{.x = gutter - 2, .y = 1}].brush.background == ox::Color{fixture.theme.selectionBackground});
    REQUIRE(screen[{.x = gutter - 1, .y = 1}].brush.background == ox::Color{fixture.theme.selectionBackground});
}

TEST_CASE("Clicking inside the gutter moves point to the start of that line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 3};

    view.mouse_press(ox::Mouse{.at = {.x = 0, .y = 2}, .button = ox::Mouse::Button::Left}); // inside the gutter, row 2 ("three")

    REQUIRE(fixture.buffer.Point() == fixture.buffer.Content().LineToByteOffset(2));
}

TEST_CASE("A keyboard navigation key after a mouse-drag selection collapses it instead of extending it", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    const int gutter = GutterWidth(1);
    view.mouse_press(ox::Mouse{.at = {.x = gutter + 4, .y = 0}, .button = ox::Mouse::Button::Left});
    view.mouse_move(ox::Mouse{.at = {.x = gutter + 10, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 10});

    // Mouse release itself doesn't clear the mark -- it's the next real
    // navigation keypress that does, same as any mouse-drag selection.
    view.mouse_release(ox::Mouse{.at = {.x = gutter + 10, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(fixture.buffer.HasMark());

    view.key_press(esc::Key::ArrowRight);
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Point() == 11); // moved from the drag's endpoint (10), not from the mark
}

// These three tests build their own BufferView from a buffer created via
// fixture.bufferList.CreateBuffer(...) rather than fixture.View(): quit's
// unsaved-changes check reads context.bufferList, and fixture.buffer (unlike
// a real app's buffer) is never actually a member of fixture.bufferList.
TEST_CASE("C-x C-c prompts for confirmation when a buffer has unsaved changes", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");
    REQUIRE(buffer.Modified());

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);    // Ctrl+x
    view.key_press(esc::Key::EndOfText); // Ctrl+c

    REQUIRE(fixture.statusMessage.find("Unsaved changes in: scratch") == 0);
}

TEST_CASE("'n' cancels the quit-confirmation prompt and returns to normal editing", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::n);

    REQUIRE(fixture.statusMessage == "Quit cancelled.");

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(buffer.Text() == "editz");
}

TEST_CASE("'y' at the quit-confirmation prompt does not crash key_press", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::y);
}

TEST_CASE("Rendered content stays aligned through many mixed scroll-up/scroll-down steps", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 60; ++i) {
        content += "#line" + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 20, .height = 8};

    ox::ScreenBuffer screen({.width = 20, .height = 8});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 8}};

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    const int         gutter     = GutterWidth(totalLines);

    // Every step below (a scroll direction), applied in order; after each,
    // verify every visible row's '#' lands at the same screen column and
    // the row's number suffix matches topLine_ + row exactly.
    const std::vector<ox::Mouse::Button> steps = {
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp,
        ox::Mouse::Button::ScrollUp, // overshoots back to 0
        ox::Mouse::Button::ScrollDown,
        ox::Mouse::Button::ScrollUp,
    };

    for (std::size_t step = 0; step < steps.size(); ++step) {
        view.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = steps[step]});
        view.paint(canvas);

        for (int row = 0; row < 8; ++row) {
            const std::u32string rowText = ContentRowText(screen, row, 12, totalLines);
            const std::u32string prefix  = U"#";
            INFO("step " << step << " row " << row << " text [" << std::string(rowText.begin(), rowText.end()) << "]");
            // '#' must be exactly at the gutter boundary (column 0 of content) if this row has real content.
            if (rowText[0] != U' ') { // blank filler rows (past EOF) are all spaces
                REQUIRE(screen[{.x = gutter, .y = row}].symbol == U'#');
            }
        }
    }
}

TEST_CASE("C-x C-f prompts for a path, then find-file opens an existing file and switches buffers", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_findfile.txt";
    std::filesystem::remove(path);
    {
        std::ofstream(path) << "hello from disk";
    }

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);      // C-x
    view.key_press(esc::Key::Acknowledge); // C-f
    REQUIRE(fixture.statusMessage == "Find file: ");

    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "hello from disk");
    REQUIRE(fixture.statusMessage.find("Opened") == 0);

    std::filesystem::remove(path);
}

TEST_CASE("find-file on a path that doesn't exist yet creates a new buffer and reports (New file)", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_findfile_new.txt";
    std::filesystem::remove(path);

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::Acknowledge);
    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text().empty());
    REQUIRE(fixture.statusMessage == "(New file)");

    // Back to normal editing in the new buffer.
    view.key_press(esc::Key::z);
    REQUIRE(activeBuffer.Get().Text() == "z");
}

TEST_CASE("Escape cancels the find-file prompt and returns to normal editing on the original buffer", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::Acknowledge);
    TypeText(view, "/nonexistent");
    view.key_press(esc::Key::Escape);

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE(fixture.statusMessage.empty());

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(scratch.Text() == "z");
}

TEST_CASE("C-x b switches to another already-open buffer by name", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("from the other buffer");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel); // C-x
    view.key_press(esc::Key::b);      // b (plain, not Ctrl)
    REQUIRE(fixture.statusMessage == "Switch to buffer: ");

    TypeText(view, "other");
    view.key_press(esc::Key::Enter);

    REQUIRE(&activeBuffer.Get() == &other);
    REQUIRE(activeBuffer.Get().Text() == "from the other buffer");
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("switch-to-buffer reports an error and stays put for an unknown buffer name", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::b);
    TypeText(view, "no-such-buffer");
    view.key_press(esc::Key::Enter);

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE(fixture.statusMessage == "No buffer named \"no-such-buffer\"");
}

TEST_CASE("Tab in switch-to-buffer completes a unique prefix and confirms with Enter", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other-buffer");
    other.InsertAtPoint("hi");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel); // C-x
    view.key_press(esc::Key::b);
    TypeText(view, "oth");
    view.key_press(esc::Key::Tab);
    REQUIRE(fixture.statusMessage == "Switch to buffer: other-buffer");

    view.key_press(esc::Key::Enter);
    REQUIRE(&activeBuffer.Get() == &other);
}

TEST_CASE("Tab in switch-to-buffer with ambiguous matches completes to the common prefix and lists candidates",
          "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    fixture.bufferList.CreateBuffer("alpha");
    fixture.bufferList.CreateBuffer("alphabet");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::b);
    TypeText(view, "al");
    view.key_press(esc::Key::Tab);

    // Common prefix of "alpha"/"alphabet" is "alpha" -- extends past what was typed.
    REQUIRE(fixture.statusMessage == "Switch to buffer: alpha  {alpha alphabet}");

    // Prompt is still live: still on the original buffer, no crash from the ambiguous Tab.
    REQUIRE(&activeBuffer.Get() == &scratch);
}

TEST_CASE("Tab with no matches leaves the prompt untouched", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 40, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::b);
    TypeText(view, "no-such-prefix");
    view.key_press(esc::Key::Tab);

    REQUIRE(fixture.statusMessage == "Switch to buffer: no-such-prefix");
}

TEST_CASE("Tab in find-file completes a unique file path and Enter opens it", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_tab_unique";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "onlyfile.txt") << "unique file contents";
    }

    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 60, .height = 3};

    view.key_press(esc::Key::Cancel);      // C-x
    view.key_press(esc::Key::Acknowledge); // C-f
    TypeText(view, (dir / "only").string());
    view.key_press(esc::Key::Tab);
    REQUIRE(fixture.statusMessage == "Find file: " + (dir / "onlyfile.txt").string());

    view.key_press(esc::Key::Enter);
    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "unique file contents");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Tab in find-file with ambiguous matches completes to the common prefix and lists candidates",
          "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_tab_ambiguous";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "apple.txt") << "x";
    }
    {
        std::ofstream(dir / "apricot.txt") << "x";
    }

    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 60, .height = 3};

    view.key_press(esc::Key::Cancel);
    view.key_press(esc::Key::Acknowledge);
    TypeText(view, (dir / "ap").string());
    view.key_press(esc::Key::Tab);

    // Common prefix of "apple.txt"/"apricot.txt" is just "ap" -- unchanged.
    REQUIRE(fixture.statusMessage ==
            "Find file: " + (dir / "ap").string() + "  {" + (dir / "apple.txt").string() + " " +
                (dir / "apricot.txt").string() + "}");
    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SetTopLine clamps so the buffer's last line stops at the bottom of the viewport, not past it",
          "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 10; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 3};

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    view.SetTopLine(1000);
    REQUIRE(view.TopLine() == totalLines - 3); // last line ends up on the bottom row, not scrolled past it

    view.SetTopLine(2);
    REQUIRE(view.TopLine() == 2);
}

TEST_CASE("mouse_wheel scrolling down repeatedly stops with the last line at the bottom row", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 10; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 4};

    ox::ScreenBuffer screen({.width = 40, .height = 4});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 4}};

    for (int i = 0; i < 20; ++i) { // way more than enough wheel ticks to hit the end
        view.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }
    view.paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(view.TopLine() == totalLines - 4);
    REQUIRE(ContentRowText(screen, 3, 5, totalLines) == U"line9"); // last line on the bottom row
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == U"line6"); // no blank filler rows below it
}

TEST_CASE("SetScrollBar makes paint() keep the bar's scrollable_length/position/item_visual_length in sync",
          "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 5};

    ox::ScrollBar scrollBar;
    view.SetScrollBar(&scrollBar);

    ox::ScreenBuffer screen({.width = 40, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 5}};
    view.paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(scrollBar.scrollable_length == static_cast<int>(totalLines - 5) + 1); // MaxTopLine() + 1
    REQUIRE(scrollBar.position == 0);
    REQUIRE(scrollBar.item_visual_length == 1);

    view.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    view.paint(canvas);

    REQUIRE(scrollBar.position == static_cast<int>(view.TopLine()));
    REQUIRE(view.TopLine() > 0);
}

TEST_CASE("paint() without a scroll bar set is a safe no-op for the sync step", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 10, .height = 3};

    ox::ScreenBuffer screen({.width = 10, .height = 3});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 10, .height = 3}};

    view.paint(canvas); // no SetScrollBar call -- must not crash
    REQUIRE(view.TopLine() == 0);
}

TEST_CASE("SetScrollArrows disables both arrows when the whole buffer fits on screen", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 10}; // plenty of room for 3 lines

    const ox::Brush            enabledBrush{.foreground = ox::XColor::White};
    const ox::Brush            disabledBrush{.foreground = ox::XColor::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    ox::ScreenBuffer screen({.width = 40, .height = 10});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 10}};
    view.paint(canvas);

    ox::ScreenBuffer arrowScreen({.width = 1, .height = 1});
    ox::Canvas       arrowCanvas{.buffer = arrowScreen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    up.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == disabledBrush);
    down.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == disabledBrush);
}

TEST_CASE("SetScrollArrows enables only the down arrow at the top of a scrollable buffer", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 5};

    const ox::Brush            enabledBrush{.foreground = ox::XColor::White};
    const ox::Brush            disabledBrush{.foreground = ox::XColor::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    ox::ScreenBuffer screen({.width = 40, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 5}};
    view.paint(canvas);

    ox::ScreenBuffer arrowScreen({.width = 1, .height = 1});
    ox::Canvas       arrowCanvas{.buffer = arrowScreen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    up.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == disabledBrush); // already at the top
    down.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == enabledBrush); // more content below
}

TEST_CASE("SetScrollArrows enables only the up arrow at the bottom of a scrollable buffer", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 40, .height = 5};

    const ox::Brush            enabledBrush{.foreground = ox::XColor::White};
    const ox::Brush            disabledBrush{.foreground = ox::XColor::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    view.SetTopLine(1000); // clamps to MaxTopLine() -- bottom of the buffer

    ox::ScreenBuffer screen({.width = 40, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 5}};
    view.paint(canvas);

    ox::ScreenBuffer arrowScreen({.width = 1, .height = 1});
    ox::Canvas       arrowCanvas{.buffer = arrowScreen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    up.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == enabledBrush); // more content above
    down.paint(arrowCanvas);
    REQUIRE(arrowScreen[{.x = 0, .y = 0}].brush == disabledBrush); // already at the bottom
}

TEST_CASE("C-c C-s prompts for a pattern, then project-search opens a results buffer", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_search";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "match.txt") << "before\nneedle here\nafter\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);          // C-c
    view.key_press(esc::Key::DeviceControlThree); // C-s
    REQUIRE(fixture.statusMessage == "Project search: ");

    TypeText(view, "needle");
    view.key_press(esc::Key::Enter);

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Name().find("*search results*") == 0);
    REQUIRE(fixture.activeBuffer.Get().Text().find((dir / "match.txt").string() + ":2: needle here") !=
            std::string::npos);
    REQUIRE(fixture.statusMessage.find("1 match for \"needle\"") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-search reports no matches without switching buffers", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_search_none";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "nothing to find here\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlThree);
    TypeText(view, "needle");
    view.key_press(esc::Key::Enter);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage == "No matches for \"needle\"");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-search reports an invalid regex without switching buffers", "[BufferView]") {
    const CurrentPathGuard cwdGuard(std::filesystem::temp_directory_path());

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlThree);
    view.key_press(esc::Key::LeftParenthesis); // "(" with no closing paren -> invalid regex
    view.key_press(esc::Key::Enter);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);
}

TEST_CASE("Escape cancels project-search and returns to normal editing", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlThree);
    TypeText(view, "needle");
    view.key_press(esc::Key::Escape);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage.empty());

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("C-c C-v jumps to the file:line under point in a project-search-shaped line", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_visit_target.txt";
    {
        std::ofstream(path) << "one\ntwo\nthree\n";
    }

    Fixture fixture;
    fixture.buffer.InsertAtPoint("some text\n" + path.string() + ":2: two\nmore text");
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // point on the results-shaped line

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);       // C-c
    view.key_press(esc::Key::SynchronousIdle); // C-v

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text() == "one\ntwo\nthree\n");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1);

    std::filesystem::remove(path);
}

TEST_CASE("C-c C-v is a no-op on a line that isn't shaped like a search result", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some ordinary text");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::SynchronousIdle);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.buffer.Text() == "just some ordinary text");
}

TEST_CASE("C-c C-r walks pattern, replacement, and confirmation, rewriting matched files on 'y'", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);        // C-c
    view.key_press(esc::Key::DeviceControlTwo); // C-r
    REQUIRE(fixture.statusMessage == "Project replace regex: ");

    TypeText(view, "needle");
    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.statusMessage.find("Replace \"needle\" with:") == 0);
    // The preview buffer is switched to as soon as the pattern is confirmed,
    // not just at the final y/n -- visible the whole time the replacement
    // text is being typed.
    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text().find((dir / "a.txt").string() + ":1: needle") != std::string::npos);

    TypeText(view, "found");
    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.statusMessage.find("Replace matches on 1 line across 1 file with \"found\"? (y/n)") == 0);

    view.key_press(esc::Key::y);
    REQUIRE(fixture.statusMessage == "Replaced 1 occurrence in 1 file.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "found\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("'n' at the project-replace confirmation cancels without touching any file", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_cancel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlTwo);
    TypeText(view, "needle");
    view.key_press(esc::Key::Enter);
    TypeText(view, "found");
    view.key_press(esc::Key::Enter);

    view.key_press(esc::Key::n);
    REQUIRE(fixture.statusMessage == "Project replace cancelled.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "needle\n"); // untouched

    std::filesystem::remove_all(dir);
}

TEST_CASE("Escape cancels project-replace at any stage, touching no file", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_escape";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlTwo);
    TypeText(view, "needle");
    view.key_press(esc::Key::Escape);

    REQUIRE(fixture.statusMessage == "Project replace cancelled.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "needle\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-replace reports an invalid regex and stays in the pattern prompt", "[BufferView]") {
    const CurrentPathGuard cwdGuard(std::filesystem::temp_directory_path());

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlTwo);
    view.key_press(esc::Key::LeftParenthesis); // invalid regex
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);

    // Still entering the pattern: DEL edits it, Escape cancels out cleanly.
    view.key_press(esc::Key::Backspace);
    view.key_press(esc::Key::Escape);
    REQUIRE(fixture.statusMessage == "Project replace cancelled.");
}

TEST_CASE("project-replace with no matches ends the session without creating a preview buffer", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_nomatch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "nothing relevant\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DeviceControlTwo);
    TypeText(view, "needle");
    view.key_press(esc::Key::Enter);
    TypeText(view, "found");
    view.key_press(esc::Key::Enter);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // no preview buffer -- nothing to preview
    REQUIRE(fixture.statusMessage.find("No matches") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RequestCloseBuffer closes an unmodified, non-active buffer immediately", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("other") == nullptr);
    REQUIRE(&activeBuffer.Get() == &scratch); // untouched -- "other" wasn't active
}

TEST_CASE("RequestCloseBuffer closing the active buffer switches to another remaining one", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(scratch);

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(&activeBuffer.Get() == &other);
}

TEST_CASE("RequestCloseBuffer closing the only remaining buffer replaces it with a fresh scratch buffer",
          "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    only = fixture.bufferList.CreateBuffer("only");
    ned::ui::ActiveBuffer activeBuffer(only);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(only);

    // Never left with zero buffers -- a brand new one replaces it, and it's
    // the active buffer, not just sitting unopened in the list.
    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("only") == nullptr);
    REQUIRE(&activeBuffer.Get() != &only);
    REQUIRE(activeBuffer.Get().Name() == "scratch");
}

TEST_CASE("RequestCloseBuffer on a modified buffer prompts, 'y' confirms the close", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("unsaved");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);
    REQUIRE(fixture.bufferList.Count() == 2); // not closed yet -- awaiting confirmation
    REQUIRE(fixture.statusMessage.find("unsaved changes") != std::string::npos);

    view.key_press(static_cast<esc::Key>(static_cast<unsigned char>('y')));

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("other") == nullptr);
}

TEST_CASE("RequestCloseBuffer on a modified buffer prompts, 'n' cancels and keeps it", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("unsaved");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);
    view.key_press(static_cast<esc::Key>(static_cast<unsigned char>('n')));

    REQUIRE(fixture.bufferList.Count() == 2);
    REQUIRE(fixture.bufferList.Find("other") == &other);
    REQUIRE(fixture.statusMessage.find("cancelled") != std::string::npos);
}

TEST_CASE("RequestCloseBuffer is a no-op while another interactive session is already active", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 60, .height = 3};

    view.key_press(esc::Key::DeviceControlThree); // C-s: isearch-forward -- an interactive session is now active

    view.RequestCloseBuffer(other);

    REQUIRE(fixture.bufferList.Count() == 2); // untouched
}

TEST_CASE("C-c C-p toggles the registered project sidebar's active flag", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    ned::ui::ProjectSidebar sidebar(fixture.activeBuffer, fixture.bufferList, fixture.statusMessage, fixture.theme);
    REQUIRE(sidebar.active); // starts visible
    view.SetProjectSidebar(&sidebar);

    view.key_press(esc::Key::EndOfText);      // C-c
    view.key_press(esc::Key::DataLinkEscape); // C-p
    REQUIRE_FALSE(sidebar.active);

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DataLinkEscape);
    REQUIRE(sidebar.active);
}

TEST_CASE("Toggling with a registered sidebarRow reflows widths immediately, not just on the next terminal resize",
          "[BufferView]") {
    Fixture fixture;

    // Mirrors main.cpp's own Row{ProjectSidebar, BufferView, ...} construction
    // pattern: both are built as temporaries directly inside the Row
    // initializer, then accessed afterward via structured bindings -- Widget's
    // deleted copy constructor rules out constructing them standalone first
    // and assembling the Row around named lvalues.
    ox::Row row{
        ned::ui::ProjectSidebar(fixture.activeBuffer, fixture.bufferList, fixture.statusMessage, fixture.theme) |
            ox::SizePolicy::fixed(20),
        fixture.View(),
    };
    auto& [sidebar, view] = row.children;
    row.size              = {.width = 60, .height = 3};
    row.resize(row.size);

    REQUIRE(sidebar.active);
    REQUIRE(sidebar.size.width == 20);
    REQUIRE(view.size.width == 40);

    view.SetProjectSidebar(&sidebar);
    view.SetSidebarRow(&row);

    view.key_press(esc::Key::EndOfText);      // C-c
    view.key_press(esc::Key::DataLinkEscape); // C-p

    REQUIRE_FALSE(sidebar.active);
    // BufferView reclaimed the sidebar's 20 columns without any separate
    // resize() call from the test -- proving SetSidebarRow's own resize()
    // call, not just the .active flip, is what made this happen.
    REQUIRE(view.size.width == 60);

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DataLinkEscape);

    REQUIRE(sidebar.active);
    REQUIRE(sidebar.size.width == 20);
    REQUIRE(view.size.width == 40);
}

TEST_CASE("A growing sidebar resize drag hands off to BufferView's mouse_move/mouse_release", "[BufferView]") {
    Fixture fixture;

    // Same in-place Row construction pattern as the toggle-reflow test above.
    ox::Row row{
        ned::ui::ProjectSidebar(fixture.activeBuffer, fixture.bufferList, fixture.statusMessage, fixture.theme) |
            ox::SizePolicy::fixed(20),
        fixture.View(),
    };
    auto& [sidebar, view] = row.children;
    row.size              = {.width = 60, .height = 3};
    row.resize(row.size);

    REQUIRE(sidebar.size.width == 20);
    REQUIRE(view.size.width == 40);
    REQUIRE(view.at.x == 20);

    sidebar.SetSidebarRow(&row);
    view.SetProjectSidebar(&sidebar);
    view.SetSidebarRow(&row);

    sidebar.mouse_press(ox::Mouse{.at = {.x = 19, .y = 0}, .button = ox::Mouse::Button::Left}); // divider column
    REQUIRE(sidebar.IsResizing());

    // The cursor has moved 5 columns into BufferView's own territory -- with
    // no mouse-capture in TermOx, this event is hit-tested to BufferView,
    // not ProjectSidebar, purely because view.at.x (20) puts it there.
    view.mouse_move(ox::Mouse{.at = {.x = 5, .y = 0}});

    REQUIRE(sidebar.size.width == 26); // grew by (view.at.x + 5) - 19 == 6
    REQUIRE(view.size.width == 34);

    view.mouse_release(ox::Mouse{.at = {.x = 5, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(sidebar.IsResizing());
}

TEST_CASE("toggle-project-sidebar is a safe no-op when no sidebar is registered", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::DataLinkEscape); // must not crash
}

TEST_CASE("C-c C-d prompts for a path, then create-directory creates it on disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir";
    std::filesystem::remove_all(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);         // C-c
    view.key_press(esc::Key::EndOfTransmission); // C-d
    REQUIRE(fixture.statusMessage == "Create directory: ");

    TypeText(view, dir.string());
    view.key_press(esc::Key::Enter);

    REQUIRE(std::filesystem::is_directory(dir));
    REQUIRE(fixture.statusMessage == "Created directory " + dir.string());

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("create-directory reports an error rather than crashing on failure", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir_conflict";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::EndOfTransmission);
    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);

    REQUIRE_FALSE(fixture.statusMessage.empty());
    REQUIRE(fixture.statusMessage != "Created directory " + path.string());

    std::filesystem::remove_all(path);
}

TEST_CASE("Escape cancels the create-directory prompt without touching disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir_cancel";
    std::filesystem::remove_all(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::EndOfTransmission);
    TypeText(view, dir.string());
    view.key_press(esc::Key::Escape);

    REQUIRE_FALSE(std::filesystem::exists(dir));
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("C-c C-k prompts for a path, confirms with y, and delete-file removes it", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_file.txt";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);   // C-c
    view.key_press(esc::Key::VerticalTab); // C-k
    REQUIRE(fixture.statusMessage == "Delete file: ");

    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.statusMessage == "Delete \"" + path.string() + "\"? (y/n)");

    view.key_press(esc::Key::y);

    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(fixture.statusMessage == "Deleted " + path.string());

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("delete-file confirmation: 'n' cancels and leaves the path untouched", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_file_cancel.txt";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::VerticalTab);
    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);

    view.key_press(esc::Key::n);

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(fixture.statusMessage == "Delete cancelled.");

    std::filesystem::remove_all(path);
}

TEST_CASE("delete-file reports an error and ends the session for a path that doesn't exist", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_missing.txt";
    std::filesystem::remove_all(path);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::VerticalTab);
    TypeText(view, path.string());
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.statusMessage == "No such file or directory: " + path.string());

    view.key_press(esc::Key::z); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("DeleteProjectPath recursively removes a directory through delete-file", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "file.txt") << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::VerticalTab);
    TypeText(view, dir.string());
    view.key_press(esc::Key::Enter);
    view.key_press(esc::Key::y);

    REQUIRE_FALSE(std::filesystem::exists(dir));
}

TEST_CASE("C-c C-n renames a file on disk and follows the buffer that had it open", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "old.txt";
    const std::filesystem::path to   = dir / "new.txt";
    {
        std::ofstream(from) << "content";
    }

    Fixture               fixture;
    ned::text::Buffer&    opened = fixture.bufferList.OpenOrCreateFile(from);
    ned::ui::ActiveBuffer activeBuffer(opened);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText); // C-c
    view.key_press(esc::Key::ShiftOut);  // C-n
    REQUIRE(fixture.statusMessage == "Rename file: ");

    TypeText(view, from.string());
    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.statusMessage == "Rename \"" + from.string() + "\" to: ");

    TypeText(view, to.string());
    view.key_press(esc::Key::Enter);

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to));
    REQUIRE(fixture.statusMessage == "Renamed to " + to.string());
    REQUIRE(activeBuffer.Get().Path() == to);
    REQUIRE(activeBuffer.Get().Name() == "new.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file on a directory relocates every open buffer nested inside it, not just the active one",
          "[BufferView]") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_dir";
    std::filesystem::remove_all(base);
    const std::filesystem::path from = base / "old";
    const std::filesystem::path to   = base / "new";
    std::filesystem::create_directories(from / "sub");
    {
        std::ofstream(from / "a.txt") << "a";
    }
    {
        std::ofstream(from / "sub" / "b.txt") << "b";
    }

    Fixture               fixture;
    ned::text::Buffer&    bufferA = fixture.bufferList.OpenOrCreateFile(from / "a.txt");
    ned::text::Buffer&    bufferB = fixture.bufferList.OpenOrCreateFile(from / "sub" / "b.txt");
    ned::ui::ActiveBuffer activeBuffer(bufferA); // bufferB is open but not active
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.size = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText); // C-c
    view.key_press(esc::Key::ShiftOut);  // C-n
    TypeText(view, from.string());
    view.key_press(esc::Key::Enter);
    TypeText(view, to.string());
    view.key_press(esc::Key::Enter);

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to / "a.txt"));
    REQUIRE(std::filesystem::exists(to / "sub" / "b.txt"));
    REQUIRE(fixture.statusMessage == "Renamed to " + to.string());

    // Both buffers follow -- bufferB despite never having been the active
    // one -- and each keeps its own filename, since only the ancestor
    // directory moved, not the files themselves.
    REQUIRE(bufferA.Path() == to / "a.txt");
    REQUIRE(bufferA.Name() == "a.txt");
    REQUIRE(bufferB.Path() == to / "sub" / "b.txt");
    REQUIRE(bufferB.Name() == "b.txt");

    std::filesystem::remove_all(base);
}

TEST_CASE("rename-file on a path with no open buffer just renames on disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_no_buffer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "old.txt";
    const std::filesystem::path to   = dir / "new.txt";
    {
        std::ofstream(from) << "content";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftOut);
    TypeText(view, from.string());
    view.key_press(esc::Key::Enter);
    TypeText(view, to.string());
    view.key_press(esc::Key::Enter);

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to));
    REQUIRE(fixture.buffer.Path() == std::nullopt); // untouched -- fixture.buffer never had `from` open

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file reports an error and ends the session when the source doesn't exist", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftOut);
    TypeText(view, (dir / "nope.txt").string());
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.statusMessage == "No such file or directory: " + (dir / "nope.txt").string());

    view.key_press(esc::Key::z); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file reports an error when the destination already exists, leaving the source in place",
          "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_conflict";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "from.txt";
    const std::filesystem::path to   = dir / "to.txt";
    {
        std::ofstream(from) << "x";
    }
    {
        std::ofstream(to) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftOut);
    TypeText(view, from.string());
    view.key_press(esc::Key::Enter);
    TypeText(view, to.string());
    view.key_press(esc::Key::Enter);

    REQUIRE(std::filesystem::exists(from));
    REQUIRE_FALSE(fixture.statusMessage.empty());
    REQUIRE(fixture.statusMessage != "Renamed to " + to.string());

    std::filesystem::remove_all(dir);
}

TEST_CASE("C-c C-o prompts for a name and find-scratch creates a new scratch note", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_new";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText); // C-c
    view.key_press(esc::Key::ShiftIn);   // C-o
    REQUIRE(fixture.statusMessage == "Find scratch: ");

    TypeText(view, "todo");
    view.key_press(esc::Key::Enter);

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text().empty());
    REQUIRE(fixture.activeBuffer.Get().Path() == ned::editor::ScratchPathForName("todo"));
    REQUIRE(fixture.statusMessage == "Scratch: todo");
    REQUIRE(std::filesystem::is_directory(ned::editor::ScratchDirectory()));

    // Back to normal editing in the new scratch buffer.
    view.key_press(esc::Key::z);
    REQUIRE(fixture.activeBuffer.Get().Text() == "z");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("find-scratch reopens an existing scratch note by name", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_existing";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    std::filesystem::create_directories(ned::editor::ScratchDirectory());
    {
        std::ofstream(ned::editor::ScratchPathForName("todo")) << "buy milk";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftIn);
    TypeText(view, "todo");
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.activeBuffer.Get().Text() == "buy milk");
    REQUIRE(fixture.statusMessage == "Scratch: todo");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("find-scratch rejects a name containing a path separator", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_invalid";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftIn);
    TypeText(view, "../escape");
    view.key_press(esc::Key::Enter);

    REQUIRE(fixture.statusMessage == "Invalid scratch name: \"../escape\"");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // never switched

    view.key_press(esc::Key::z); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("Escape cancels the find-scratch prompt without creating anything", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_cancel";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftIn);
    TypeText(view, "todo");
    view.key_press(esc::Key::Escape);

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage.empty());
    REQUIRE_FALSE(std::filesystem::exists(dataDir));

    view.key_press(esc::Key::z); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("Tab in find-scratch completes a unique scratch name and Enter opens it", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_tab";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    std::filesystem::create_directories(ned::editor::ScratchDirectory());
    {
        std::ofstream(ned::editor::ScratchPathForName("todo-list")) << "unique scratch contents";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftIn);
    TypeText(view, "todo");
    view.key_press(esc::Key::Tab);
    REQUIRE(fixture.statusMessage == "Find scratch: todo-list");

    view.key_press(esc::Key::Enter);
    REQUIRE(fixture.activeBuffer.Get().Text() == "unique scratch contents");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("timer() auto-saves a modified scratch buffer", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_timer";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.size                = {.width = 60, .height = 3};

    view.key_press(esc::Key::EndOfText);
    view.key_press(esc::Key::ShiftIn);
    TypeText(view, "todo");
    view.key_press(esc::Key::Enter);

    view.key_press(esc::Key::z); // dirty the new scratch buffer
    REQUIRE(fixture.activeBuffer.Get().Modified());

    view.timer(); // simulates an auto-save tick, without a real sleep

    REQUIRE_FALSE(fixture.activeBuffer.Get().Modified());
    REQUIRE(std::filesystem::exists(ned::editor::ScratchPathForName("todo")));
    {
        std::ifstream in(ned::editor::ScratchPathForName("todo"));
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "z");
    }

    std::filesystem::remove_all(dataDir);
}
