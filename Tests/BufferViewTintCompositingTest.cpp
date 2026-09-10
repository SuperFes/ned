#include <catch2/catch_test_macros.hpp>

#include <string>

#include <vector>
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/ThemeSetting.h"
#include "Editor/WhitespaceSettings.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Compositing.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/ThemeResolve.h"

using ned::ui::BufferView;
using ned::ui::Color;

namespace {

// BufferViewStickyScrollTest.cpp's own Fixture shape, minus the tags query
// this file has no use for.
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
    ned::editor::Mode            mode  = ned::editor::Mode{.name = "fundamental-mode"};
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

struct WhitespaceGuard {
    // Saved, not assumed: the setting defaults to *disabled*, and restoring
    // it to true instead left WhitespaceSettingsTest's own default-value
    // assertion failing for the rest of the single-process run.
    const bool previous = ned::editor::TrailingWhitespaceHighlightEnabled();

    WhitespaceGuard() {
        ned::editor::SetTrailingWhitespaceHighlightEnabled(true);
    }
    ~WhitespaceGuard() {
        ned::editor::SetTrailingWhitespaceHighlightEnabled(previous);
    }
};

// The background of the first trailing-whitespace cell on row 0. "ab   " puts
// the trailing run at buffer columns 2..4, well clear of the gutter.
Color TrailingWhitespaceBackground(Fixture& fixture) {
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});

    ned::ui::Screen screen(40, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});
    view.Paint(canvas);

    // Anchored to the glyph rather than to a colour: the gutter carries a
    // background of its own, so "first cell that differs" finds that instead.
    // The cell after 'b' is the first of the trailing run.
    for (int x = 0; x + 1 < 40; ++x) {
        if (screen.PixelAt(x, 0).character == "b") {
            return screen.PixelAt(x + 1, 0).background_color;
        }
    }
    FAIL("no 'b' glyph on row 0 -- the fixture never painted its own content");
    return fixture.theme.background;
}

} // namespace

TEST_CASE("An opaque tint paints exactly the colour the theme names", "[BufferView][Tints]") {
    const WhitespaceGuard guard;
    Fixture               fixture;
    fixture.buffer.InsertAtPoint("ab   \ncd\n");
    fixture.theme.background                   = Color::RGB(0x20, 0x20, 0x28);
    fixture.theme.trailingWhitespaceBackground = Color::RGB(0x80, 0x20, 0x20);

    // Routing these through OverlayBackground must not change what an
    // existing theme paints: an opaque overlay composites to itself.
    REQUIRE(TrailingWhitespaceBackground(fixture) == Color::RGB(0x80, 0x20, 0x20));
}

TEST_CASE("A tint carrying alpha composites over the buffer background", "[BufferView][Tints]") {
    const WhitespaceGuard guard;
    Fixture               fixture;
    fixture.buffer.InsertAtPoint("ab   \ncd\n");
    fixture.theme.background                   = Color::RGB(0x20, 0x20, 0x28);
    fixture.theme.trailingWhitespaceBackground = Color::RGB(0x80, 0x20, 0x20).WithAlpha(0x40);

    // Before this composited, the alpha byte went straight into the Cell and
    // Screen::Flush read only the RGB -- so a translucent tint rendered as
    // the solid colour, indistinguishable from an opaque one.
    const Color painted = TrailingWhitespaceBackground(fixture);
    REQUIRE(painted == ned::ui::BlendOver(fixture.theme.background, fixture.theme.trailingWhitespaceBackground));
    REQUIRE_FALSE(painted == Color::RGB(0x80, 0x20, 0x20));

    // A composited result is always opaque -- compositing resolves alpha
    // away rather than accumulating it, since the destination is a real cell.
    REQUIRE(painted.Opaque());
}

TEST_CASE("A tint over a transparent theme resolves against the assumed backdrop", "[BufferView][Tints]") {
    const WhitespaceGuard guard;
    Fixture               fixture;
    fixture.buffer.InsertAtPoint("ab   \ncd\n");

    // DarkTheme's background is Color::Default: there is nothing in the cell
    // to blend with, so a translucent tint resolves against whatever backdrop
    // was detected rather than landing as the solid slab it exists to avoid.
    REQUIRE_FALSE(fixture.theme.background.Composable());
    const std::optional<Color> previous = ned::ui::AssumedBackground();
    ned::ui::SetAssumedBackground(Color::RGB(0x10, 0x10, 0x14));

    fixture.theme.trailingWhitespaceBackground = Color::RGB(0x80, 0x20, 0x20).WithAlpha(0x40);
    const Color painted                        = TrailingWhitespaceBackground(fixture);

    ned::ui::SetAssumedBackground(previous);

    REQUIRE(painted == ned::ui::BlendOver(Color::RGB(0x10, 0x10, 0x14), Color::RGB(0x80, 0x20, 0x20).WithAlpha(0x40)));
}

// Translucency phase 6: the selection and search washes are themed Surfaces,
// so a theme can make either a gradient or a pattern rather than one colour.

namespace {

struct SurfaceGuard {
    SurfaceGuard() {
        Clear();
    }
    ~SurfaceGuard() {
        Clear();
    }
    static void Clear() {
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
    }
};

// Paints a fixture whose whole first line is selected, and returns the
// backgrounds of the selected cells on row 0, left to right.
std::vector<Color> SelectedRowBackgrounds(Fixture& fixture, int width) {
    fixture.buffer.InsertAtPoint("aaaaaaaaaaaaaaaaaaaa\nbb\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(20); // the whole first line

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 3});

    ned::ui::Screen screen(width, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 3});
    view.Paint(canvas);

    std::vector<Color> backgrounds;
    for (int x = 0; x < width; ++x) {
        if (screen.PixelAt(x, 0).character == "a") {
            backgrounds.push_back(screen.PixelAt(x, 0).background_color);
        }
    }
    return backgrounds;
}

} // namespace

TEST_CASE("An unthemed selection paints the one colour it always did", "[BufferView][Tints]") {
    const SurfaceGuard guard;
    Fixture            fixture;
    fixture.theme.background          = Color::RGB(0x20, 0x20, 0x28);
    fixture.theme.selectionBackground = Color::RGB(0x40, 0x50, 0x80).WithAlpha(0x60);

    const std::vector<Color> backgrounds = SelectedRowBackgrounds(fixture, 60);
    REQUIRE(backgrounds.size() > 4);

    // The derived default is a solid, so every selected cell matches, and
    // matches the composite the buffer painted before surfaces existed.
    const Color expected = ned::ui::BlendOver(fixture.theme.background, ned::ui::SelectionFill(fixture.theme));
    for (const Color& background : backgrounds) {
        REQUIRE(background == expected);
    }
}

TEST_CASE("A gradient selection washes across the viewport", "[BufferView][Tints]") {
    const SurfaceGuard guard;
    Fixture            fixture;
    fixture.theme.background = Color::RGB(0x20, 0x20, 0x28);

    ned::editor::AddSurfacePaint("buffer.selection", "fill", "x #ff000080 #0000ff80");
    ned::ui::ApplyPaintOverrides(fixture.theme);

    const std::vector<Color> backgrounds = SelectedRowBackgrounds(fixture, 60);
    REQUIRE(backgrounds.size() > 4);

    // Sampled across the whole viewport, so the run of selected cells reveals
    // part of one wash rather than getting a ramp of its own.
    REQUIRE(backgrounds.front().red > backgrounds.back().red);
    REQUIRE(backgrounds.back().blue > backgrounds.front().blue);

    // Still composited, so still opaque -- a selection is a tint over the
    // buffer, never a hole.
    for (const Color& background : backgrounds) {
        REQUIRE(background.Opaque());
    }
}

TEST_CASE("The selection surface default is what the buffer actually paints", "[BufferView][Tints]") {
    const SurfaceGuard guard;
    ned::ui::Theme     theme  = ned::ui::DarkTheme();
    theme.selectionBackground = Color::RGB(0x40, 0x50, 0x80); // fully opaque, as pre-alpha themes wrote it

    // SelectionFill softens an opaque selection to a tint. The derived
    // surface has to agree, or M-x theme-gallery shows a swatch that nothing
    // on screen matches -- which is exactly what it did before this.
    const ned::ui::Surface selection = ned::ui::SurfaceFor(theme, "buffer.selection");
    REQUIRE(selection.fill.stops.front().colour == ned::ui::SelectionFill(theme));
    REQUIRE_FALSE(selection.fill.stops.front().colour == theme.selectionBackground);
}
