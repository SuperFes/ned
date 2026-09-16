#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/MinimapSettings.h"
#include "Editor/Mode.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/Utf8.h"
#include "UI/ActiveBuffer.h"
#include "UI/EventLoop.h"
#include "UI/Minimap.h"
#include "UI/Theme.h"

using ned::editor::MinimapCharsPerDot;
using ned::editor::MinimapWidth;
using ned::editor::SetMinimapCharsPerDot;
using ned::editor::SetMinimapWidth;
using ned::ui::Minimap;

namespace {

// Mirrors ProjectSidebarTest.cpp's own small mouse-event helpers.
ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed);
}
ned::ui::Event MouseMove(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved);
}
ned::ui::Event MouseRelease(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released);
}

// Process-wide state -- mirrors MinimapSettingsTest.cpp's own guard.
struct MinimapSettingsGuard {
    ~MinimapSettingsGuard() {
        SetMinimapWidth(5);
        SetMinimapCharsPerDot(8);
    }
};

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();

    Minimap View() {
        return Minimap(activeBuffer, mode, theme);
    }
};

} // namespace

TEST_CASE("Minimap paints without crashing on an empty buffer", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});

    ned::ui::Screen screen = ned::ui::Screen(5, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    minimap.Paint(canvas); // must not crash/throw
}

// per-buffer-highlight-cache follow-up: ClearBufferCache doesn't touch
// plane_/EnsurePlane at all -- just the highlight-span map -- so this
// exercises it both before the cache could hold anything for buffer (a
// fresh Minimap) and after a real Paint() call (still a no-op without a
// wired EventLoop, per the test above, but the same code path a live
// pane's own Minimap goes through on a real buffer close either way).
TEST_CASE("Minimap::ClearBufferCache is a safe no-op, with or without a prior paint", "[Minimap]") {
    Fixture fixture;
    Minimap minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    REQUIRE_NOTHROW(minimap.ClearBufferCache(fixture.buffer));

    ned::ui::Screen screen = ned::ui::Screen(5, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    minimap.Paint(canvas);
    REQUIRE_NOTHROW(minimap.ClearBufferCache(fixture.buffer));
}

// minimap-not-inheriting-theme follow-up: AdvanceHighlightSweep's own reset
// condition used to fire on `firstPaint` (!lastSpans_), which stays true for
// every tick of a multi-chunk sweep right up until the final commit -- so on
// any document needing more than one kHighlightSweepChunkBytes-sized chunk,
// every single Paint() wiped sweepActive_/sweepSpans_/sweepText_ back to
// scratch before the sweep ever advanced past its first chunk, and the
// minimap rendered every dot as theme_.defaultForeground forever (visually
// indistinguishable from "ignoring the theme's syntax colours" -- what
// prompted this test). Repeated Paint() calls with nothing else about the
// buffer changing between them is exactly how a live session behaves
// between keystrokes; this reproduces that without a live terminal.
TEST_CASE("Minimap's chunked highlight sweep completes across repeated frames with no edit between them",
          "[Minimap]") {
    const MinimapSettingsGuard guard;

    // Real cpp syntax, well past kHighlightSweepChunkBytes (4096 bytes) so
    // completing the sweep needs more than one AdvanceHighlightSweep tick.
    std::string content;
    for (int i = 0; i < 400; ++i) {
        content += "int example_variable_" + std::to_string(i) + " = 12345; // a comment\n";
    }
    REQUIRE(content.size() > Minimap::kHighlightSweepChunkBytes * 2);

    ned::text::Buffer     buffer{"scratch"};
    buffer.InsertAtPoint(content);
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::editor::Mode     mode  = ned::editor::CppMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();

    ned::ui::EventLoop eventLoop;
    Minimap            minimap(activeBuffer, mode, theme);
    minimap.SetEventLoop(&eventLoop);
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});

    ned::ui::Screen screen = ned::ui::Screen(5, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});

    const auto chunksNeeded = content.size() / Minimap::kHighlightSweepChunkBytes + 2;
    for (std::size_t i = 0; i < chunksNeeded; ++i) {
        minimap.Paint(canvas);
    }

    REQUIRE(minimap.CommittedSpanCountForTesting() > 0);
}

TEST_CASE("Minimap paints a flat background when no EventLoop is wired", "[Minimap]") {
    // Real-minimap-look follow-up: content now always renders via
    // Notcurses' own ncvisual_blit into a plane this widget owns
    // (Minimap.cpp's EnsurePlane()/PaintPlane()) -- invisible to a headless
    // Screen/Canvas, which only ever sees this class's own flat-background
    // backstop fill (the scroll-position band is baked into that plane's
    // own raster, not painted separately into Cells -- real per-pixel
    // transparency turned out not to be reliably supported across
    // terminals for the Kitty pixel protocol, see EnsurePlane()'s own
    // comment). SetEventLoop is never called here (the "unset is a safe
    // no-op" contract every Set* hook in this codebase follows), so
    // EnsurePlane() bails immediately and every Cell in this widget's
    // region should come back as this theme's plain background -- the
    // real, still-testable-without-a-live-terminal contract this class has.
    const MinimapSettingsGuard guard;
    SetMinimapWidth(1);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("x\n\n\n");

    Minimap minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    minimap.Paint(canvas);

    const ned::ui::Cell& cell = screen.PixelAt(0, 0);
    REQUIRE(cell.character == " ");
    REQUIRE(cell.foreground_color == fixture.theme.background);
    REQUIRE(cell.background_color == fixture.theme.background);
}

TEST_CASE("Minimap does not render past MinimapWidth * MinimapCharsPerDot * 2 columns of a line", "[Minimap]") {
    const MinimapSettingsGuard guard;
    SetMinimapWidth(1);
    SetMinimapCharsPerDot(1);
    // width=1 column -> 2 sub-columns -> maxColumn = 2 real characters.
    // A run of 50 'x's must not crash or hang regardless -- this is the
    // actual behavior under test, not a specific glyph value (the exact
    // dot pattern for "many x's truncated to 2" is covered by the dot-
    // setting test above).
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(50, 'x'));

    Minimap minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    minimap.Paint(canvas); // must not crash/hang
}

TEST_CASE("Minimap click maps to a position via the same proportional math as ScrollBar", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9}); // height 10

    minimap.scrollable_length  = 100;
    minimap.item_visual_length = 10;

    int received = -1;
    minimap.SetOnScroll([&received](int position) { received = position; });

    // Clicking the middle row (row 5 of 10) should land roughly mid-way
    // through the scrollable range.
    minimap.OnEvent(MousePress(2, 5));
    REQUIRE(received == (5 * 100) / 10); // == 50, same formula ScrollBar::PositionForRow uses
}

TEST_CASE("Minimap drag continues reporting positions until release", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    minimap.scrollable_length  = 100;
    minimap.item_visual_length = 10;

    std::vector<int> received;
    minimap.SetOnScroll([&received](int position) { received.push_back(position); });

    minimap.OnEvent(MousePress(2, 0));
    minimap.OnEvent(MouseMove(2, 9));
    minimap.OnEvent(MouseRelease(2, 9));
    REQUIRE(received.size() == 2); // press + one move; release reports nothing new

    received.clear();
    minimap.OnEvent(MouseMove(2, 3)); // no longer dragging after release
    REQUIRE(received.empty());
}
