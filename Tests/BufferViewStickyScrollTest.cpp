#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/StickyScrollSettings.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture shape, using CppMode
// (needs a real tags query for sticky scroll to have anything to show).
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
    ned::editor::Mode            mode  = ned::editor::CppMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

struct StickyScrollSettingsGuard {
    ~StickyScrollSettingsGuard() {
        ned::editor::SetStickyScrollEnabled(true);
        ned::editor::SetStickyScrollMaxRows(4);
    }
};

const char* kSource =
    "namespace outer {\n" // line 0
    "class Widget {\n"    // line 1
    "public:\n"           // line 2
    "    void run() {\n"  // line 3
    "        line1;\n"    // line 4
    "        line2;\n"    // line 5
    "        line3;\n"    // line 6
    "    }\n"              // line 7
    "};\n"                // line 8
    "}\n";                // line 9

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed);
}

} // namespace

TEST_CASE("Sticky scroll pins the enclosing namespace/class/method chain once scrolled into a body",
          "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5); // "        line2;" -- deep inside run()'s body

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Three pinned rows -- outer, Widget, run -- then real content starting
    // with line2 (topLine_) pushed down to row 3.
    REQUIRE(RowText(screen, 0, 40).find("outer") != std::string::npos);
    REQUIRE(RowText(screen, 1, 40).find("Widget") != std::string::npos);
    REQUIRE(RowText(screen, 2, 40).find("run") != std::string::npos);
    REQUIRE(RowText(screen, 3, 40).find("line2") != std::string::npos);
    REQUIRE(RowText(screen, 4, 40).find("line3") != std::string::npos);
}

TEST_CASE("Sticky scroll shows nothing when the viewport top is the outermost header itself",
          "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(0); // "namespace outer {" itself -- nothing has scrolled away

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("namespace") != std::string::npos);
}

TEST_CASE("Sticky scroll respects ned/set-sticky-scroll-max-rows", "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    ned::editor::SetStickyScrollMaxRows(2); // outer/Widget/run is 3 deep -- cap keeps the innermost 2

    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5);

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Nearest ancestors (Widget, run) win over the outermost (outer) when
    // capped.
    REQUIRE(RowText(screen, 0, 40).find("Widget") != std::string::npos);
    REQUIRE(RowText(screen, 1, 40).find("run") != std::string::npos);
    REQUIRE(RowText(screen, 2, 40).find("line2") != std::string::npos);
}

TEST_CASE("Sticky scroll draws nothing when disabled via ned/set-sticky-scroll-enabled", "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    ned::editor::SetStickyScrollEnabled(false);

    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5);

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // No pinned rows -- row 0 is real content (line2, topLine_ itself).
    REQUIRE(RowText(screen, 0, 40).find("line2") != std::string::npos);
}

TEST_CASE("CursorPosition accounts for however many sticky rows the last Paint() drew",
          "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5);
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(5)); // point on line2, the visible top line

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // 3 sticky rows (outer/Widget/run) were drawn -- point's own line (the
    // viewport's un-shifted row 0) should now report screen row 3.
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->y == 3);
}

TEST_CASE("Clicking below the sticky rows resolves to the buffer line actually drawn there",
          "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5);

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas); // stickyRowCount_ (3: outer/Widget/run) is now live

    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(5));
    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x; // point at line2's own column 0

    // Screen row 4 is line3 (topLine_ + 1) once the 3 sticky rows above it
    // are accounted for -- without the stickyRowCount_ correction in
    // ByteOffsetForPoint, this click would have resolved to whatever line
    // sits at the UNSHIFTED row 4 instead.
    view.OnEvent(MousePress(gutterWidth + 4, 4));

    const std::size_t line = fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point());
    REQUIRE(line == 6); // "        line3;"
}

TEST_CASE("Clicking a pinned sticky row jumps to that ancestor's own header line",
          "[BufferView][StickyScroll]") {
    const StickyScrollSettingsGuard guard;
    Fixture fixture;
    fixture.buffer.InsertAtPoint(kSource);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.SetTopLine(5);

    ned::ui::Screen screen = ned::ui::Screen(40, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 5});
    view.Paint(canvas); // stickyRowCount_ (3: outer/Widget/run) is now live

    // Row 1 of the pinned band is "Widget".
    view.OnEvent(MousePress(5, 1));

    const std::size_t line = fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point());
    REQUIRE(line == 1); // "class Widget {"
}
