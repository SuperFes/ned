//
// Phase 5 of Docs/Translucency.md: the chrome widgets paint through themed
// Surfaces now. The property worth pinning is not "gradients work" -- it is
// that an *unthemed* editor is byte-identical to what it painted before, so
// the migration is invisible until a theme asks for something.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Mode.h"
#include "Editor/ThemeSetting.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"
#include "UI/ActiveBuffer.h"
#include "UI/EchoArea.h"
#include "UI/ListPopup.h"
#include "UI/ModeLine.h"
#include "UI/Paint.h"
#include "UI/PaintParse.h"
#include "UI/ScrollBar.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/Widget.h"

using ned::ui::Canvas;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Surface;
using ned::ui::Theme;

namespace {

struct SurfaceGuard {
    // The detected accent is process-wide and now feeds a derived surface
    // (buffer.current_line), so it is part of what a surface test has to put
    // back -- restored rather than cleared, since the process may have been
    // started with one.
    std::optional<ned::ui::Color> accent = ned::ui::DetectedAccent();

    SurfaceGuard() {
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
    }
    ~SurfaceGuard() {
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
        ned::ui::SetDetectedAccent(accent);
    }
};

Screen PaintModeLine(const Theme& theme, int width) {
    static ned::text::Buffer buffer("chrome.txt", ned::text::Rope("hello"));
    ned::ui::ActiveBuffer    active(buffer);
    ned::editor::Mode        mode;
    ned::ui::ModeLine        modeLine(active, mode, theme);

    Screen screen(width, 1);
    modeLine.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
    Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
    modeLine.Paint(canvas);
    return screen;
}

Screen PaintEchoArea(const Theme& theme, const std::string& message, int width) {
    ned::ui::EchoArea  echoArea(message, theme);
    Screen             screen(width, 1);
    const ned::ui::Box box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0};
    echoArea.SetBox_(box);
    Canvas canvas(screen, box);
    echoArea.Paint(canvas);
    return screen;
}

Screen PaintScrollBar(const Theme& theme, int height) {
    ned::ui::ScrollBar scrollBar(theme);
    scrollBar.scrollable_length  = 100;
    scrollBar.item_visual_length = 10;
    scrollBar.position           = 0;

    Screen             screen(1, height);
    const ned::ui::Box box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = height - 1};
    scrollBar.SetBox_(box);
    Canvas canvas(screen, box);
    scrollBar.Paint(canvas);
    return screen;
}

ned::ui::Paint ParseOrDie(std::string_view spec, const Theme& theme) {
    const auto paint = ned::ui::ParsePaint(spec, ned::ui::PaintContextFor(theme));
    REQUIRE(paint.has_value());
    return *paint;
}

} // namespace

TEST_CASE("An unthemed mode line paints the same gradient it always did", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme  = DarkTheme();
    Screen             screen = PaintModeLine(theme, 40);

    // Endpoints are exact in both the old hand-rolled interpolation and the
    // Surface path; the interior differs only in rounding.
    REQUIRE(screen.PixelAt(0, 0).background_color == theme.modeLineGradientStart);
    REQUIRE(screen.PixelAt(39, 0).background_color == theme.modeLineGradientEnd);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);

    SECTION("and the interior is still a monotone ramp between them") {
        const int left  = screen.PixelAt(5, 0).background_color.red;
        const int mid   = screen.PixelAt(20, 0).background_color.red;
        const int right = screen.PixelAt(35, 0).background_color.red;
        const int from  = theme.modeLineGradientStart.red;
        const int to    = theme.modeLineGradientEnd.red;
        if (from <= to) {
            REQUIRE(left <= mid);
            REQUIRE(mid <= right);
        }
        else {
            REQUIRE(left >= mid);
            REQUIRE(mid >= right);
        }
    }
}

TEST_CASE("A themed mode line paints the theme's own paint", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #000000 #ffffff", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x000000));
    REQUIRE(screen.PixelAt(39, 0).background_color == Color::RGB(0xffffff));
    REQUIRE(static_cast<int>(screen.PixelAt(20, 0).background_color.red) == 131);

    SECTION("the row's text survives the fill -- a wash never erases glyphs") {
        std::string row;
        for (int x = 0; x < 40; ++x) {
            row += screen.PixelAt(x, 0).character;
        }
        REQUIRE(row.find("chrome.txt") != std::string::npos);
    }
}

TEST_CASE("A translucent mode line keeps the terminal's background", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme = DarkTheme();
    theme.background         = Color::Default;

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #40c08080 #40c08080", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);

    // Over a transparent background there is nothing to blend into, so the
    // wash reaches the desktop the only way a cell can: dithered coverage in
    // the blank cells, and a foreground tint where the row has text.
    bool anyDithered = false;
    for (int x = 0; x < 40; ++x) {
        const auto& cell = screen.PixelAt(x, 0);
        REQUIRE(cell.background_color == Color::Default);
        anyDithered = anyDithered || (cell.character != " " && cell.character != "");
    }
    REQUIRE(anyDithered);
}

TEST_CASE("A mode-line text fade modulates the glyphs after they are painted", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #000000 #000000", theme);
    modeline.text    = ParseOrDie("x 100% 0%", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);

    // Left edge keeps the whole foreground; the right edge has faded all the
    // way into the background it sits on.
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);
    REQUIRE(screen.PixelAt(39, 0).foreground_color == Color::RGB(0x000000));
}

namespace {

// A derived surface part is either a one-stop solid of the theme colour it
// came from, or -- when that colour is the terminal's own background, which
// has no RGB to paint with -- a paint that paints nothing at all.
void RequireDerivedFrom(const ned::ui::Paint& paint, const Color& colour) {
    if (colour.Composable()) {
        REQUIRE(paint.stops.size() == 1);
        REQUIRE(paint.stops.front().colour == colour);
    }
    else {
        REQUIRE(paint.stops.empty());
        REQUIRE_FALSE(ned::ui::PaintsColour(paint));
    }
}

} // namespace

TEST_CASE("Surface defaults keep every chrome widget's pre-migration colours", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab").fill, theme.tabBar.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab").text, theme.tabBar.foreground);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab.active").fill, theme.activeTab.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab.active.focused").fill, theme.modeLineFocusedGradientStart);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "panel").fill, theme.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "panel").text, theme.defaultForeground);

    SECTION("a theme whose background is the terminal's own paints no panel fill") {
        // DarkTheme is exactly this case: the sidebar shows the terminal's
        // background through, which is what makes a transparent theme work
        // at all.
        REQUIRE_FALSE(theme.background.Composable());
        REQUIRE(ned::ui::SurfaceFor(theme, "panel").fill.stops.empty());
    }
}

TEST_CASE("A surface spec referencing a bundled preset applies the way startup applies it", "[ChromeSurface]") {
    // Mirrors main.cpp's own application order exactly: bundled presets are
    // registered first, then a surface spec that references one by name.
    // This is the path a live run takes, and it is worth pinning here
    // because a live run is the only other place it is exercised.
    const SurfaceGuard       guard;
    const Theme              theme = DarkTheme();
    ned::janet::Environment& env   = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    ned::editor::ClearNamedPaintOverrides();
    ned::editor::ClearSurfacePaintOverrides();
    ned::janet::LoadBundledPlugins(env);
    env.DoString(R"((ned/surface "modeline" :fill [:x "$spectrum"]))");

    for (const auto& [name, spec] : ned::editor::NamedPaintOverrides()) {
        const auto paint = ned::ui::ParsePaint(spec, ned::ui::PaintContextFor(theme));
        INFO(name << " = " << spec);
        REQUIRE(paint.has_value());
        ned::ui::RegisterNamedPaint(name, *paint);
    }

    const auto surfaces = ned::editor::SurfacePaintOverrides();
    REQUIRE(surfaces.size() == 1);
    REQUIRE(surfaces[0].spec == "x $spectrum");

    const auto paint = ned::ui::ParsePaint(surfaces[0].spec, ned::ui::PaintContextFor(theme));
    REQUIRE(paint.has_value());
    REQUIRE(paint->stops.size() == 4); // the preset's own stops, expanded
    REQUIRE(ned::ui::PaintsColour(*paint));

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = *paint;
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x2859dc));
    REQUIRE(screen.PixelAt(39, 0).background_color == Color::RGB(0xe13c6e));

    ned::editor::ClearNamedPaintOverrides();
    ned::editor::ClearSurfacePaintOverrides();
}

TEST_CASE("A translucent overlay composites against the assumed backdrop", "[ChromeSurface]") {
    // The case a transparent theme creates: the buffer background is the
    // terminal's own, so a translucent selection has nothing in the cell to
    // blend with and would land as the solid slab it exists to avoid.
    const SurfaceGuard guard;
    struct BackdropGuard {
        ~BackdropGuard() {
            ned::ui::SetAssumedBackground(std::nullopt);
        }
    } backdropGuard;

    Theme theme               = DarkTheme();
    theme.background          = Color::Default;
    theme.selectionBackground = Color::RGB(0xff0000).WithAlpha(128);

    SECTION("with no backdrop known it stays opaque, as before") {
        ned::ui::SetAssumedBackground(std::nullopt);
        REQUIRE(ned::ui::OverlayBackground(theme, theme.selectionBackground) == Color::RGB(0xff0000));
    }

    SECTION("with one, it tints") {
        ned::ui::SetAssumedBackground(Color::RGB(0x000000));
        const Color tinted = ned::ui::OverlayBackground(theme, theme.selectionBackground);
        REQUIRE(static_cast<int>(tinted.red) == 128);
        REQUIRE(static_cast<int>(tinted.green) == 0);
    }
}

TEST_CASE("An opaque selection colour is softened; an authored alpha is obeyed", "[ChromeSurface]") {
    // Every theme written before the format had alpha says 255 by default,
    // and a solid bar over text is what that produces -- so a fully opaque
    // value is treated as unspecified rather than as a deliberate slab.
    Theme theme               = DarkTheme();
    theme.selectionBackground = Color::RGB(0x2e4a80);
    REQUIRE(static_cast<int>(ned::ui::SelectionFill(theme).alpha) == 110);

    theme.selectionBackground = Color::RGB(0x2e4a80).WithAlpha(200);
    REQUIRE(static_cast<int>(ned::ui::SelectionFill(theme).alpha) == 200);

    theme.selectionBackground = Color::Default;
    REQUIRE(ned::ui::SelectionFill(theme) == Color::Default);
}

TEST_CASE("A mode line only fades where there is something to fade into", "[ChromeSurface]") {
    const SurfaceGuard guard;
    struct BackdropGuard {
        ~BackdropGuard() {
            ned::ui::SetAssumedBackground(std::nullopt);
        }
    } backdropGuard;

    SECTION("an opaque theme fades toward its own background") {
        Theme theme      = DarkTheme();
        theme.background = Color::RGB(0x101014);
        REQUIRE_FALSE(ned::ui::SurfaceFor(theme, "modeline").fill.stops.back().colour.Opaque());
    }

    SECTION("a transparent theme with no backdrop keeps the flat bar rather than dithering") {
        ned::ui::SetAssumedBackground(std::nullopt);
        Theme theme      = DarkTheme();
        theme.background = Color::Default;
        REQUIRE(ned::ui::SurfaceFor(theme, "modeline").fill.stops.back().colour == theme.modeLineGradientEnd);
    }

    SECTION("a transparent theme with a detected backdrop fades again") {
        Theme theme      = DarkTheme();
        theme.background = Color::Default;
        ned::ui::SetAssumedBackground(Color::RGB(0x101014));
        REQUIRE_FALSE(ned::ui::SurfaceFor(theme, "modeline").fill.stops.back().colour.Opaque());
    }
}

TEST_CASE("A transparent theme's tab strip is chrome, not a hole", "[ChromeSurface]") {
    const SurfaceGuard guard;

    Theme opaque      = DarkTheme();
    opaque.background = Color::RGB(0x101014);
    REQUIRE(ned::ui::SurfaceFor(opaque, "tab.strip").fill.stops.front().colour == opaque.background);

    Theme transparent      = DarkTheme();
    transparent.background = Color::Default;
    const auto strip       = ned::ui::SurfaceFor(transparent, "tab.strip").fill;
    REQUIRE(strip.stops.size() == 1);
    REQUIRE(strip.stops.front().colour.Composable());   // something to see...
    REQUIRE_FALSE(strip.stops.front().colour.Opaque()); // ...but not a solid band
}

TEST_CASE("The current line paints into the backing layer, not the text cells", "[ChromeSurface]") {
    // The backing layer is a second grid flushed to a plane *below* the text
    // one, so a row highlight sits behind the glyphs instead of in the same
    // cell as them. That is what lets a wash exist without the text layer
    // having to choose between covering the syntax colour and being visible.
    Screen screen(8, 2);

    SECTION("a backing cell is independent of the text cell above it") {
        screen.PixelAt(0, 0).character          = "x";
        screen.PixelAt(0, 0).foreground_color   = Color::RGB(0x00ff00);
        screen.BackingAt(0, 0).background_color = Color::RGB(0xff0000);

        REQUIRE(screen.PixelAt(0, 0).character == "x");
        REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0x00ff00));
        REQUIRE(screen.PixelAt(0, 0).background_color == Color::Default); // untouched
        REQUIRE(screen.BackingAt(0, 0).background_color == Color::RGB(0xff0000));
    }

    SECTION("ClearBacking resets it, since both layers persist between frames") {
        screen.BackingAt(3, 1).background_color = Color::RGB(0xff0000);
        screen.ClearBacking();
        REQUIRE(screen.BackingAt(3, 1).background_color == Color::Default);
    }

    SECTION("a Canvas addresses the backing layer in its own local coordinates") {
        Canvas canvas(screen, ned::ui::Box{.x_min = 2, .x_max = 7, .y_min = 1, .y_max = 1});
        canvas.Backing({.x = 0, .y = 0}).background_color = Color::RGB(0x0000ff);
        REQUIRE(screen.BackingAt(2, 1).background_color == Color::RGB(0x0000ff));

        // Out of bounds discards rather than corrupting a neighbour, exactly
        // like operator[].
        REQUIRE_NOTHROW(canvas.Backing({.x = 99, .y = 0}).background_color = Color::RGB(0x00ff00));
        REQUIRE(screen.BackingAt(7, 1).background_color == Color::Default);
    }
}

TEST_CASE("buffer.current_line defaults to the desktop accent, well under selection strength", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    // The default follows the detected desktop accent when there is one, so
    // the highlight belongs to the same palette as the rest of the chrome.
    ned::ui::SetDetectedAccent(Color::RGB(0x3f7fbf));
    const Surface derived = ned::ui::SurfaceFor(theme, "buffer.current_line");
    REQUIRE(ned::ui::PaintsColour(derived.fill));
    REQUIRE(derived.fill.stops.front().colour.Opaque() == false);
    REQUIRE(derived.fill.stops.front().colour.WithAlpha(255) == Color::RGB(0x3f7fbf));

    // It is up the whole time you are typing, so it has to lose to the
    // overlays that mean something momentary.
    REQUIRE(derived.fill.stops.front().colour.alpha < ned::ui::SelectionFill(theme).alpha);

    ned::ui::SetDetectedAccent(std::nullopt);
    REQUIRE(ned::ui::PaintsColour(ned::ui::SurfaceFor(theme, "buffer.current_line").fill));

    Surface current;
    current.fill = ned::ui::SolidPaint(Color::RGB(0x2a2a40));
    ned::ui::SetSurfaceOverride("buffer.current_line", current);
    REQUIRE(ned::ui::PaintsColour(ned::ui::SurfaceFor(theme, "buffer.current_line").fill));
}

// Translucency phase 5 remainder: the two chrome widgets that were never in a
// phase. Both painted a flat Brush per cell; both go through their own
// Surface now, and their derived defaults have to be what they always were.

TEST_CASE("An unthemed echo area paints exactly the Brush it always did", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme  = DarkTheme();
    theme.echoArea.background = ned::ui::Color::RGB(0x20, 0x20, 0x28);
    theme.echoArea.foreground = ned::ui::Color::RGB(0xc0, 0xc0, 0xd0);

    Screen screen = PaintEchoArea(theme, "hi", 12);
    for (int x = 0; x < 12; ++x) {
        INFO("column " << x);
        REQUIRE(screen.PixelAt(x, 0).background_color == theme.echoArea.background);
        REQUIRE(screen.PixelAt(x, 0).foreground_color == theme.echoArea.foreground);
    }
    REQUIRE(screen.PixelAt(0, 0).character == "h");
    // Padding runs to the full width -- a half-painted bar would show the
    // buffer through its own row.
    REQUIRE(screen.PixelAt(11, 0).character == " ");
}

TEST_CASE("A themed echo area paints the theme's own paint under its text", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    ned::ui::Surface echo;
    echo.fill = ParseOrDie("x #400000 #004000", theme);
    ned::ui::SetSurfaceOverride("echo", echo);

    Screen screen = PaintEchoArea(theme, "hi", 12);
    // A gradient, so the two ends differ -- and the glyphs are still there.
    REQUIRE_FALSE(screen.PixelAt(0, 0).background_color == screen.PixelAt(11, 0).background_color);
    REQUIRE(screen.PixelAt(0, 0).character == "h");
    REQUIRE(screen.PixelAt(1, 0).character == "i");
}

TEST_CASE("Echo-area dim reads the painted background, not the flat Brush", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    // Dimming interpolates toward what the fill actually put down. Under a
    // gradient the two ends dim toward different colours, which a
    // flat-Brush reading could not express.
    ned::ui::Surface echo;
    echo.fill = ParseOrDie("x #400000 #004000", theme);
    ned::ui::SetSurfaceOverride("echo", echo);

    const std::string dimmed = ned::ui::DimForEchoArea("aaaaaaaaaaaa");
    Screen            screen = PaintEchoArea(theme, dimmed, 12);
    REQUIRE_FALSE(screen.PixelAt(0, 0).foreground_color == screen.PixelAt(11, 0).foreground_color);
}

TEST_CASE("An unthemed scroll bar paints exactly the Brush it always did", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme   = DarkTheme();
    theme.scrollBar.background = ned::ui::Color::RGB(0x18, 0x18, 0x20);
    theme.scrollBar.foreground = ned::ui::Color::RGB(0x60, 0x60, 0x80);

    Screen screen = PaintScrollBar(theme, 10);
    for (int y = 0; y < 10; ++y) {
        INFO("row " << y);
        REQUIRE(screen.PixelAt(0, y).background_color == theme.scrollBar.background);
        REQUIRE(screen.PixelAt(0, y).foreground_color == theme.scrollBar.foreground);
    }
    // The thumb still reads by inverting, so it survives any fill.
    REQUIRE(screen.PixelAt(0, 0).inverted);
    REQUIRE_FALSE(screen.PixelAt(0, 9).inverted);
}

TEST_CASE("A themed scroll bar paints the theme's own paint", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    ned::ui::Surface bar;
    bar.fill = ParseOrDie("y #400000 #004000", theme);
    ned::ui::SetSurfaceOverride("scrollbar", bar);

    Screen screen = PaintScrollBar(theme, 10);
    REQUIRE_FALSE(screen.PixelAt(0, 0).background_color == screen.PixelAt(0, 9).background_color);
    REQUIRE(screen.PixelAt(0, 0).inverted); // thumb still distinguishable
}

// Translucency phase 7a: the popup body as a themed Surface. The interesting
// one is blur -- an overlay paints after the tree beneath it, so the cells
// under a popup already hold what it is covering, which is exactly what
// FillBlur samples.

namespace {

Screen PaintPopupOver(const Theme& theme, const ned::ui::Color& beneath, int width, int height) {
    Screen screen(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // A hard split down the middle, so a blur has a real edge to
            // smear and a uniform fill provably cannot reproduce it.
            screen.PixelAt(x, y).background_color = (x < width / 2) ? beneath : ned::ui::Color::RGB(0xF0, 0xF0, 0xF0);
        }
    }

    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.title = "T", .rows = {{.main = "one"}, {.main = "two"}}});
    const ned::ui::Box box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1};
    popup.SetBox_(box);
    Canvas canvas(screen, box);
    popup.Paint(canvas);
    return screen;
}

} // namespace

TEST_CASE("An unthemed popup paints the flat background it always did", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme = DarkTheme();
    theme.background         = ned::ui::Color::RGB(0x20, 0x20, 0x28);

    Screen screen = PaintPopupOver(theme, ned::ui::Color::RGB(0x80, 0x00, 0x00), 20, 8);
    for (int y = 1; y < 7; ++y) {
        for (int x = 1; x < 19; ++x) {
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(screen.PixelAt(x, y).background_color == theme.background);
        }
    }
    REQUIRE(screen.PixelAt(4, 1).character == "o"); // the row text is still there (left column empty -> main at 4)
}

TEST_CASE("A themed popup paints its own fill under the rows", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    ned::ui::Surface popup;
    popup.fill = ParseOrDie("x #400000 #004000", theme);
    ned::ui::SetSurfaceOverride("popup", popup);

    Screen screen = PaintPopupOver(theme, ned::ui::Color::RGB(0x80, 0x00, 0x00), 20, 8);
    REQUIRE_FALSE(screen.PixelAt(1, 1).background_color == screen.PixelAt(18, 1).background_color);
    // Row text survives the fill: the glyph pass no longer writes background.
    REQUIRE(screen.PixelAt(4, 1).character == "o");
    REQUIRE(screen.PixelAt(4, 2).character == "t");
}

TEST_CASE("A blurred popup samples what it is covering", "[ChromeSurface]") {
    const SurfaceGuard   guard;
    const Theme          theme   = DarkTheme();
    const ned::ui::Color beneath = ned::ui::Color::RGB(0x80, 0x00, 0x00);

    ned::ui::Surface popup;
    popup.fill = ParseOrDie("blur 2", theme);
    ned::ui::SetSurfaceOverride("popup", popup);

    Screen screen = PaintPopupOver(theme, beneath, 20, 8);

    // The interior is neither of the two colours it covered, and not uniform
    // -- it is a smear of the edge between them. A fill that ignored the
    // destination could produce neither.
    const ned::ui::Color left  = screen.PixelAt(2, 3).background_color;
    const ned::ui::Color right = screen.PixelAt(17, 3).background_color;
    // Away from the edge a box blur of a uniform region *is* that region, so
    // each side reproduces exactly what it covers -- which is the proof that
    // the fill read the destination rather than painting a colour of its own.
    REQUIRE(left == beneath);
    REQUIRE(right == ned::ui::Color::RGB(0xF0, 0xF0, 0xF0));

    // At the seam it is genuinely smeared: strictly between the two, on every
    // channel. A fill ignoring the destination could produce neither result.
    const ned::ui::Color seam = screen.PixelAt(10, 3).background_color;
    REQUIRE(seam.red > left.red);
    REQUIRE(seam.red < right.red);
    REQUIRE(seam.green > left.green);
    REQUIRE(seam.green < right.green);
}

// Translucency phase 7b: a border can be a gradient. RecolourBorder walks the
// frame ring only, and only its foreground -- a border is a line.

TEST_CASE("An unthemed popup border is the flat colour DrawBorder always used", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme = DarkTheme();
    theme.border.foreground  = ned::ui::Color::RGB(0x3a, 0x3a, 0x50);

    Screen screen = PaintPopupOver(theme, ned::ui::Color::RGB(0x80, 0x00, 0x00), 20, 8);
    // The derived default is a solid, so recolouring is a no-op.
    REQUIRE(screen.PixelAt(5, 0).foreground_color == theme.border.foreground);
    REQUIRE(screen.PixelAt(0, 4).foreground_color == theme.border.foreground);
    REQUIRE(screen.PixelAt(19, 4).foreground_color == theme.border.foreground);
}

TEST_CASE("A gradient border runs along the frame", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    ned::ui::Surface popup;
    popup.border = ParseOrDie("x #ff0000 #0000ff", theme);
    ned::ui::SetSurfaceOverride("popup", popup);

    Screen screen = PaintPopupOver(theme, ned::ui::Color::RGB(0x80, 0x00, 0x00), 20, 8);

    // An x gradient sweeps across the top edge...
    const Color topLeft  = screen.PixelAt(1, 0).foreground_color;
    const Color topRight = screen.PixelAt(18, 0).foreground_color;
    REQUIRE(topLeft.red > topRight.red);
    REQUIRE(topRight.blue > topLeft.blue);

    // ...and is constant down a side, which is the same column throughout.
    REQUIRE(screen.PixelAt(0, 2).foreground_color == screen.PixelAt(0, 5).foreground_color);

    // The bottom edge sweeps the same way the top does.
    REQUIRE(screen.PixelAt(1, 7).foreground_color == topLeft);
}

TEST_CASE("A gradient border leaves the interior and the title alone", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    ned::ui::Surface popup;
    popup.border = ParseOrDie("x #ff0000 #0000ff", theme);
    ned::ui::SetSurfaceOverride("popup", popup);

    Screen screen = PaintPopupOver(theme, ned::ui::Color::RGB(0x80, 0x00, 0x00), 20, 8);

    // The title is content, not frame: recolouring runs before it is drawn,
    // so it keeps its own accent rather than being swept through.
    // Column 2 is the title's leading pad space; the text starts at 3.
    REQUIRE(screen.PixelAt(3, 0).character == "T");
    REQUIRE(screen.PixelAt(3, 0).foreground_color == theme.borderAccent.foreground);

    // And nothing inside the ring was touched.
    REQUIRE(screen.PixelAt(4, 1).character == "o");
    REQUIRE(screen.PixelAt(4, 1).foreground_color == theme.defaultForeground);
}

// Brush::ApplyTextTo -- ApplyTo minus the background a surface fill owns.
// Nine widgets were unrolling ApplyTo by hand to drop that one line.

TEST_CASE("ApplyTextTo carries every trait but leaves the background", "[ChromeSurface]") {
    ned::ui::Brush brush{.background    = ned::ui::Color::RGB(0x10, 0x20, 0x30),
                         .foreground    = ned::ui::Color::RGB(0xAA, 0xBB, 0xCC),
                         .bold          = true,
                         .italic        = true,
                         .underlined    = true,
                         .strikethrough = true};

    ned::ui::Cell cell;
    cell.background_color = ned::ui::Color::RGB(0x99, 0x88, 0x77); // a fill already put this down
    cell.inverted         = true;                                  // a stale flag from a prior frame

    brush.ApplyTextTo(cell);

    REQUIRE(cell.background_color == ned::ui::Color::RGB(0x99, 0x88, 0x77)); // untouched
    REQUIRE(cell.foreground_color == brush.foreground);
    REQUIRE(cell.bold);
    REQUIRE(cell.italic);
    REQUIRE(cell.underlined);
    REQUIRE(cell.strikethrough);
    // Reset for ApplyTo's own reason: the Screen is repainted in place and
    // never blanked, so a stale inversion would otherwise stick forever.
    REQUIRE_FALSE(cell.inverted);
}

TEST_CASE("ApplyTo is ApplyTextTo plus the background", "[ChromeSurface]") {
    const ned::ui::Brush brush{.background = ned::ui::Color::RGB(0x10, 0x20, 0x30),
                               .foreground = ned::ui::Color::RGB(0xAA, 0xBB, 0xCC),
                               .italic     = true};

    ned::ui::Cell viaApplyTo;
    brush.ApplyTo(viaApplyTo);

    ned::ui::Cell viaTextTo;
    viaTextTo.background_color = brush.background;
    brush.ApplyTextTo(viaTextTo);

    // The two must not drift: ApplyTo is defined in terms of ApplyTextTo, and
    // this is what keeps that true if either is edited.
    REQUIRE(viaApplyTo.background_color == viaTextTo.background_color);
    REQUIRE(viaApplyTo.foreground_color == viaTextTo.foreground_color);
    REQUIRE(viaApplyTo.bold == viaTextTo.bold);
    REQUIRE(viaApplyTo.italic == viaTextTo.italic);
    REQUIRE(viaApplyTo.underlined == viaTextTo.underlined);
    REQUIRE(viaApplyTo.strikethrough == viaTextTo.strikethrough);
    REQUIRE(viaApplyTo.inverted == viaTextTo.inverted);
}
