//
// Inline colour swatches: the cell painted in the colour a literal names.
//
// Two styles with opposite column behaviour, which is the whole reason both
// exist -- Block spends a column before the literal, Underlay spends none and
// washes the literal's own cells instead. The cursor-column assertions are the
// load-bearing ones: a swatch is virtual text, and virtual text that the
// column arithmetic does not count drifts the cursor left of the character it
// is on (see Tests/VirtualTextColumnTest.cpp for that bug's own history).
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/ColorSwatchSettings.h"
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

// The swatch settings are process-wide.
struct SwatchGuard {
    ~SwatchGuard() {
        ned::editor::SetColorSwatchesEnabled(true);
        ned::editor::SetColorSwatchStyle(ned::editor::ColorSwatchStyle::Block);
    }
};

struct Painted {
    ned::ui::Screen screen{30, 5};
    // Read off a line with no colour literal on it: a swatch shifts point's
    // own column too (that is the whole point of counting it), so measuring
    // the gutter from the literal's own line would move the goalposts with
    // the thing under test.
    int gutterWidth = 0;
};

// Every fixture buffer opens with a plain line, so row 1 is the one carrying
// the literal and row 0 is where the gutter width is measured.
constexpr int kLiteralRow = 1;

Painted Paint(Fixture& fixture, BufferView& view) {
    const ned::ui::Box box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4};
    view.SetBox_(box);

    fixture.buffer.SetPoint(0);
    REQUIRE(view.CursorPosition().has_value());

    Painted painted;
    painted.gutterWidth = view.CursorPosition()->x;
    ned::ui::Canvas canvas(painted.screen, box);
    view.Paint(canvas);
    return painted;
}

constexpr ned::ui::Color kMagenta = ned::ui::Color::RGB(0xff00aa);

} // namespace

TEST_CASE("A hex literal earns a swatch cell before it", "[BufferView][ColorSwatch]") {
    const SwatchGuard guard;
    ned::editor::SetColorSwatchesEnabled(true);
    ned::editor::SetColorSwatchStyle(ned::editor::ColorSwatchStyle::Block);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("plain\n#ff00aa\n");
    BufferView view    = fixture.View();
    Painted    painted = Paint(fixture, view);

    CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).background_color == kMagenta);
    // The literal itself still renders, one column further right.
    CHECK(painted.screen.PixelAt(painted.gutterWidth + 1, kLiteralRow).character == "#");
}

TEST_CASE("The cursor lands past the swatch, not on it", "[BufferView][ColorSwatch]") {
    const SwatchGuard guard;

    const auto columnAtLineStart = [](bool swatches) {
        ned::editor::SetColorSwatchesEnabled(swatches);
        ned::editor::SetColorSwatchStyle(ned::editor::ColorSwatchStyle::Block);

        Fixture fixture;
        fixture.buffer.InsertAtPoint("plain\n#ff00aa\n");
        fixture.buffer.SetPoint(6); // the literal line's first byte
        BufferView view = fixture.View();
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
        REQUIRE(view.CursorPosition().has_value());
        return view.CursorPosition()->x;
    };

    // The swatch renders before the real byte still at the literal's own
    // offset, so point's
    // own column has to sit past it -- exactly one column further right than
    // the same buffer with swatches off.
    CHECK(columnAtLineStart(true) == columnAtLineStart(false) + 1);
}

TEST_CASE("Underlay washes the literal's own cells and spends no column", "[BufferView][ColorSwatch]") {
    const SwatchGuard guard;
    ned::editor::SetColorSwatchesEnabled(true);
    ned::editor::SetColorSwatchStyle(ned::editor::ColorSwatchStyle::Underlay);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("plain\n#ff00aa\n");
    BufferView view    = fixture.View();
    Painted    painted = Paint(fixture, view);

    CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).character == "#");
    for (int column = 0; column < 7; ++column) {
        INFO("column " << column);
        CHECK(painted.screen.PixelAt(painted.gutterWidth + column, kLiteralRow).background_color == kMagenta);
    }
    // The character after the literal is back to the ordinary background.
    CHECK(painted.screen.PixelAt(painted.gutterWidth + 7, kLiteralRow).background_color != kMagenta);
}

TEST_CASE("Turning swatches off paints neither style", "[BufferView][ColorSwatch]") {
    const SwatchGuard guard;
    ned::editor::SetColorSwatchesEnabled(false);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("plain\n#ff00aa\n");
    BufferView view    = fixture.View();
    Painted    painted = Paint(fixture, view);

    CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).character == "#");
    CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).background_color != kMagenta);
}

TEST_CASE("Short hex draws a swatch only where the mode admits that spelling",
          "[BufferView][ColorSwatch]") {
    const SwatchGuard guard;
    ned::editor::SetColorSwatchesEnabled(true);
    ned::editor::SetColorSwatchStyle(ned::editor::ColorSwatchStyle::Block);

    SECTION("a language that does not declare it sees a comment, not a colour") {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("plain\n#f0a\n");
        BufferView view    = fixture.View();
        Painted    painted = Paint(fixture, view);
        CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).character == "#");
    }

    SECTION("a stylesheet declares it and gets the swatch") {
        Fixture fixture;
        fixture.mode.colorLiterals.shortHex = true;
        fixture.buffer.InsertAtPoint("plain\n#f0a\n");
        BufferView view    = fixture.View();
        Painted    painted = Paint(fixture, view);
        CHECK(painted.screen.PixelAt(painted.gutterWidth, kLiteralRow).background_color == kMagenta);
    }
}
