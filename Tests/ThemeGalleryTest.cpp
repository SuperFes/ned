#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>

#include "Editor/ThemeSetting.h"
#include "TestEvents.h"
#include "UI/Compositing.h"
#include "UI/Theme.h"
#include "UI/ThemeGallery.h"
#include "UI/ThemePaints.h"
#include "UI/ThemeResolve.h"
#include "UI/Widget.h"

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Theme;
using ned::ui::ThemeGallery;

namespace {

struct PaintStoreGuard {
    PaintStoreGuard() : assumedBackground_(ned::ui::AssumedBackground()), detectedAccent_(ned::ui::DetectedAccent()) {
        Clear();
    }
    ~PaintStoreGuard() {
        Clear();
        // Not part of the paint registries, but process-wide all the same,
        // and ResolveConfiguredTheme writes both. Leaving AssumedBackground
        // set changes OverlayBackground's answer for every theme whose
        // background is the terminal's own -- which is DarkTheme's -- so a
        // leak here silently reshades the selection bar in every other
        // widget test that runs after this file.
        ned::ui::SetAssumedBackground(assumedBackground_);
        ned::ui::SetDetectedAccent(detectedAccent_);
    }
    static void Clear() {
        ned::editor::ClearNamedPaintOverrides();
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearNamedPaints();
        ned::ui::ClearSurfaceOverrides();
    }

  private:
    std::optional<ned::ui::Color> assumedBackground_;
    std::optional<ned::ui::Color> detectedAccent_;
};

// Paints the gallery into a fresh Screen of the given size -- the headless
// Paint() exercise every widget test here does (Widget.h's Screen/Canvas/
// Cell are plain data with no live-terminal dependency).
Screen PaintGallery(ThemeGallery& gallery, int width, int height) {
    Screen    screen(width, height);
    const Box box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1};
    gallery.SetBox_(box);
    Canvas canvas(screen, box);
    gallery.Paint(canvas);
    return screen;
}

std::string RowText(Screen& screen, int row, int width) {
    std::string text;
    for (int x = 0; x < width; ++x) {
        text += screen.PixelAt(x, row).character;
    }
    return text;
}

bool ScreenContains(Screen& screen, int width, int height, std::string_view needle) {
    for (int y = 0; y < height; ++y) {
        if (RowText(screen, y, width).find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("The gallery enumerates every surface and every named paint", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();

    ned::editor::AddNamedPaint("brand", "x $accent $keyword");
    ned::ui::ApplyPaintOverrides(theme);

    ThemeGallery      gallery(theme);
    const auto        entries = gallery.Entries();
    const std::size_t surfaces =
        static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const ThemeGallery::Entry& e) {
            return e.kind == ThemeGallery::Entry::Kind::Surface;
        }));

    REQUIRE(surfaces == ned::ui::SurfaceNames().size());
    REQUIRE(std::any_of(entries.begin(), entries.end(), [](const ThemeGallery::Entry& e) {
        return e.kind == ThemeGallery::Entry::Kind::Paint && e.name == "brand";
    }));
}

TEST_CASE("A surface a theme actually set is marked as overridden", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();

    ThemeGallery gallery(theme);
    for (const auto& entry : gallery.Entries()) {
        if (entry.kind == ThemeGallery::Entry::Kind::Surface) {
            REQUIRE_FALSE(entry.overridden); // nothing set yet -- all derived
        }
    }

    ned::editor::AddSurfacePaint("popup", "fill", "$bg");
    ned::ui::ApplyPaintOverrides(theme);

    const auto entries = gallery.Entries();
    const auto popup   = std::find_if(entries.begin(), entries.end(), [](const ThemeGallery::Entry& e) {
        return e.kind == ThemeGallery::Entry::Kind::Surface && e.name == "popup";
    });
    REQUIRE(popup != entries.end());
    REQUIRE(popup->overridden);
}

TEST_CASE("Painting the gallery labels entries and paints a swatch beside each", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();
    ned::ui::ApplyPaintOverrides(theme);

    ThemeGallery gallery(theme);
    const int    width = 90;
    // Tall enough for "modeline" to be on screen unscrolled -- each entry is
    // three rows, so adding a surface ahead of it in SurfaceNames pushes it
    // down one entry. Sized from the list rather than pinned to a number, so
    // the next addition does not fail here for the wrong reason.
    const auto modelineIndex = [&] {
        const std::vector<std::string> names = ned::ui::SurfaceNames();
        return std::distance(names.begin(), std::find(names.begin(), names.end(), "modeline"));
    }();
    const int height = static_cast<int>(modelineIndex + 1) * 3 + 3;
    Screen    screen = PaintGallery(gallery, width, height);

    REQUIRE(ScreenContains(screen, width, height, "modeline"));
    REQUIRE(ScreenContains(screen, width, height, "Theme gallery"));
    // The footer's own counter, which is also the "there is more below the
    // fold" affordance.
    REQUIRE(ScreenContains(screen, width, height, " of "));

    // The "modeline" surface's swatch: its derived fill is the mode-line
    // gradient, not the buffer background, so those cells must differ from
    // the panel's own interior. Located by row rather than assumed -- the
    // first entry is "buffer", whose fill legitimately *is* the background.
    const auto entries = gallery.Entries();
    const auto modeline =
        std::find_if(entries.begin(), entries.end(), [](const ThemeGallery::Entry& e) { return e.name == "modeline"; });
    REQUIRE(modeline != entries.end());
    const int row = static_cast<int>(std::distance(entries.begin(), modeline));
    REQUIRE(row * 3 + 1 < height - 2); // on screen unscrolled

    const auto& swatchCell = screen.PixelAt(1 + 30, 1 + row * 3);
    REQUIRE_FALSE(swatchCell.background_color == theme.background);
}

TEST_CASE("The gallery reports contrast for the glyphs it actually painted", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    Theme                 theme = DarkTheme();

    // A fill deliberately painted in the same colour as the text over it:
    // the readout has to notice, since measuring theme fields rather than
    // composited cells is exactly what would miss this.
    ned::editor::AddSurfacePaint("popup", "fill", "$fg");
    ned::ui::ApplyPaintOverrides(theme);

    ThemeGallery gallery(theme);
    const int    width  = 90;
    const int    height = 60; // tall enough that "popup" is on screen unscrolled
    Screen       screen = PaintGallery(gallery, width, height);

    REQUIRE(ScreenContains(screen, width, height, "low contrast"));
}

TEST_CASE("Scrolling moves through the entry list and clamps at both ends", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();
    ned::ui::ApplyPaintOverrides(theme);

    ThemeGallery gallery(theme);
    const int    width  = 90;
    const int    height = 14; // deliberately short: fewer entries than fit
    gallery.TakeFocus();

    Screen top = PaintGallery(gallery, width, height);
    REQUIRE(ScreenContains(top, width, height, "buffer"));
    REQUIRE(ScreenContains(top, width, height, "1-"));

    gallery.OnEvent(ned::ui::test::ArrowDown());
    Screen scrolled = PaintGallery(gallery, width, height);
    REQUIRE(ScreenContains(scrolled, width, height, "2-"));

    // Up from the top clamps rather than underflowing the unsigned index.
    gallery.OnEvent(ned::ui::test::ArrowUp());
    gallery.OnEvent(ned::ui::test::ArrowUp());
    Screen back = PaintGallery(gallery, width, height);
    REQUIRE(ScreenContains(back, width, height, "1-"));

    // End clamps to the last screenful rather than scrolling past it, and
    // the count in the footer proves the clamp ran.
    gallery.OnEvent(ned::ui::test::End());
    Screen     bottom  = PaintGallery(gallery, width, height);
    const auto entries = gallery.Entries();
    REQUIRE(ScreenContains(bottom, width, height, " of " + std::to_string(entries.size())));
    REQUIRE(ScreenContains(bottom, width, height, "-" + std::to_string(entries.size())));
}

TEST_CASE("Escape asks the caller to close the gallery", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();

    ThemeGallery gallery(theme);
    bool         cancelled = false;
    gallery.SetOnCancel([&cancelled] { cancelled = true; });
    gallery.TakeFocus();

    REQUIRE(gallery.OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
}

TEST_CASE("The gallery degrades sanely for a canvas with no room", "[ThemeGallery]") {
    const PaintStoreGuard guard;
    const Theme           theme = DarkTheme();

    ThemeGallery gallery(theme);
    PaintGallery(gallery, 2, 2); // must not crash
    PaintGallery(gallery, 40, 3);
}
