#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own GutterWidthWithDiff helper --
// see BufferView::GutterWidth's real formula for the layout this tracks:
// [status][diagnostic][gap][digits][gap][symbol]. Content in these tests is
// deliberately kept to a single line per definition (see CMode's own "A
// block written entirely on one line gets no fold icon" precedent in
// BufferViewTest.cpp) so mode_.fold never also reserves a column here --
// this file is about the symbol column in isolation.
int GutterWidthWithSymbol(std::size_t totalLines, bool symbolActive) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    constexpr int kSymbolWidth     = 1;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + static_cast<int>(std::to_string(totalLines).size()) +
           kLineNumberGap + (symbolActive ? kSymbolWidth : 0);
}

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

TEST_CASE("BufferView reserves no symbol column for a mode with no symbolKind support", "[BufferView][Symbol]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("int add(int a, int b) { return a + b; }\n"); // FundamentalMode: no highlighting at all
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithSymbol(fixture.buffer.Content().LineCount(), false));
}

TEST_CASE("BufferView reserves a symbol column and renders the function glyph on a definition line",
          "[BufferView][Symbol]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int add(int a, int b) { return a + b; }\n");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const std::size_t totalLines  = fixture.buffer.Content().LineCount();
    const int          symbolStart = GutterWidthWithSymbol(totalLines, true) - 1;
    REQUIRE(screen.PixelAt(symbolStart, 0).character == "ƒ");
    // Point sits right after the (now symbol-column-widened) gutter.
    REQUIRE(view.CursorPosition().has_value());
}

TEST_CASE("BufferView renders no symbol glyph on a line that isn't a definition site", "[BufferView][Symbol]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("// a comment, not a definition\nint add(int a, int b) { return a + b; }\n");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});
    ned::ui::Screen screen = ned::ui::Screen(40, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});
    view.Paint(canvas);

    const std::size_t totalLines  = fixture.buffer.Content().LineCount();
    const int          symbolStart = GutterWidthWithSymbol(totalLines, true) - 1;
    REQUIRE(screen.PixelAt(symbolStart, 0).character == " "); // comment line -- no marker
    REQUIRE(screen.PixelAt(symbolStart, 1).character == "ƒ"); // the function's own line
}

TEST_CASE("Symbol gutter column is not reserved for a read-only buffer even with symbolKind configured",
          "[BufferView][Symbol]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int add(int a, int b) { return a + b; }\n");
    fixture.buffer.SetReadOnly(true);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithSymbol(fixture.buffer.Content().LineCount(), false));
}

TEST_CASE("Symbol gutter cache recomputes after an edit that adds a new definition", "[BufferView][Symbol]") {
    Fixture fixture;
    // PhpMode, not CMode -- PhpMode has no fold query (PhpMode() passes
    // nullptr for foldQuerySource, see Mode.cpp), so FoldGutterActive()
    // never contributes its own unconditional (content-independent, unlike
    // symbol's own data-driven gate) kMaxFoldDepthColumns reservation here.
    // CMode does have one, which would otherwise widen CursorPosition()'s
    // total gutter width regardless of this test's own content -- confirmed
    // live, not assumed, when this test first failed against CMode.
    fixture.mode = ned::editor::PhpMode();
    fixture.buffer.InsertAtPoint("<?php\n$x = 1;\n");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // A plain assignment isn't a definition site -- PHP's own tags.scm has
    // no pattern matching it, so no column is reserved yet.
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithSymbol(fixture.buffer.Content().LineCount(), false));

    fixture.buffer.InsertAtPoint("function add($a, $b) { return $a + $b; }\n");
    view.Paint(canvas);

    const std::size_t totalLines  = fixture.buffer.Content().LineCount();
    const int          symbolStart = GutterWidthWithSymbol(totalLines, true) - 1;
    REQUIRE(screen.PixelAt(symbolStart, 2).character == "ƒ");
}
