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
#include "Text/MergeAlignment.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// Blank alignment rows: the padding a side-by-side view asks a pane for so
// corresponding lines share a screen row. Painted-screen tests, since the row
// a line lands on is the thing under test.

using ned::text::PaneAlignment;
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

constexpr int kWidth  = 40;
constexpr int kHeight = 10;

void PaintInto(BufferView& view, ned::ui::Screen& screen) {
    const ned::ui::Box box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1};
    view.SetBox_(box);
    ned::ui::Canvas canvas(screen, box);
    view.Paint(canvas);
}

std::string PaintedRow(ned::ui::Screen& screen, int row) {
    std::string painted;
    for (int x = 0; x < kWidth; ++x) {
        painted += screen.PixelAt(x, row).character;
    }
    return painted;
}

bool RowShows(ned::ui::Screen& screen, int row, const std::string& text) {
    return PaintedRow(screen, row).find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Alignment rows push lines down, before line 0 and after a line", "[BufferView][MergeAlignment]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("alpha\nbeta\ngamma\n");
    fixture.buffer.SetPoint(0);
    // Two rows before alpha, two after beta.
    const PaneAlignment alignment({2, 3, 6, 7});
    BufferView          view = fixture.View();
    view.SetAlignmentQuery([&](const ned::text::Buffer& buffer) { return &buffer == &fixture.buffer ? &alignment : nullptr; });

    ned::ui::Screen screen(kWidth, kHeight);
    PaintInto(view, screen);

    CHECK_FALSE(RowShows(screen, 0, "alpha"));
    CHECK(RowShows(screen, 2, "alpha"));
    CHECK(RowShows(screen, 3, "beta"));
    CHECK_FALSE(RowShows(screen, 4, "gamma"));
    CHECK_FALSE(RowShows(screen, 5, "gamma"));
    CHECK(RowShows(screen, 6, "gamma"));
}

TEST_CASE("The terminal cursor lands on the aligned row of point's line", "[BufferView][MergeAlignment]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("alpha\nbeta\ngamma\n");
    const PaneAlignment alignment({2, 3, 6, 7});
    BufferView          view = fixture.View();
    view.SetAlignmentQuery([&](const ned::text::Buffer&) { return &alignment; });

    ned::ui::Screen screen(kWidth, kHeight);
    fixture.buffer.SetPoint(std::string("alpha\nbeta\n").size());
    PaintInto(view, screen);
    const auto cursor = view.CursorPosition();
    REQUIRE(cursor.has_value());
    CHECK(cursor->y == 6);
}

TEST_CASE("No alignment for the active buffer means no rows", "[BufferView][MergeAlignment]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("alpha\nbeta\n");
    fixture.buffer.SetPoint(0);
    const PaneAlignment alignment({2, 3, 4});
    BufferView          view = fixture.View();
    view.SetAlignmentQuery([&](const ned::text::Buffer&) -> const PaneAlignment* { return nullptr; });

    ned::ui::Screen screen(kWidth, kHeight);
    PaintInto(view, screen);
    CHECK(RowShows(screen, 0, "alpha"));
    CHECK(RowShows(screen, 1, "beta"));
}

TEST_CASE("An aligned pane never soft-wraps", "[BufferView][MergeAlignment]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint(std::string(3 * kWidth, 'x') + "\nnext\n");
    fixture.buffer.SetPoint(0);

    SECTION("unaligned, the long line wraps") {
        BufferView      view = fixture.View();
        ned::ui::Screen screen(kWidth, kHeight);
        PaintInto(view, screen);
        CHECK_FALSE(RowShows(screen, 1, "next"));
    }
    SECTION("aligned, it stays one row") {
        const PaneAlignment alignment({0, 1, 2});
        BufferView          view = fixture.View();
        view.SetAlignmentQuery([&](const ned::text::Buffer&) { return &alignment; });
        ned::ui::Screen screen(kWidth, kHeight);
        PaintInto(view, screen);
        CHECK(RowShows(screen, 1, "next"));
    }
}
