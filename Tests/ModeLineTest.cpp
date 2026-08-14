#include <catch2/catch_test_macros.hpp>

#include <ox/ox.hpp>
#include <string>

#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "UI/ActiveBuffer.h"
#include "UI/ModeLine.h"
#include "UI/Theme.h"

namespace {

std::u32string RowText(ox::ScreenBuffer& screen, int row, int width) {
    std::u32string out;
    for (int col = 0; col < width; ++col) {
        out.push_back(screen[{.x = col, .y = row}].symbol);
    }
    return out;
}

} // namespace

TEST_CASE("ModeLine shows the buffer name and 1-indexed line:column", "[ModeLine]") {
    ned::text::Buffer buffer("myfile.txt", ned::text::Rope("hello\nworld"));
    buffer.SetPoint(8); // line 1 (0-indexed), col 2 (0-indexed) -> displayed L2:C3

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    modeLine.paint(canvas);

    const std::u32string row = RowText(screen, 0, 40);
    REQUIRE(row.find(U"myfile.txt") != std::u32string::npos);
    REQUIRE(row.find(U"L2:C3") != std::u32string::npos);

    // Endpoints of the gradient should match the theme's declared colors.
    REQUIRE(screen[{.x = 0, .y = 0}].brush.background == ox::Color{theme.modeLineGradientStart});
    REQUIRE(screen[{.x = 39, .y = 0}].brush.background == ox::Color{theme.modeLineGradientEnd});
    REQUIRE(screen[{.x = 0, .y = 0}].brush.foreground == ox::Color{theme.modeLineForeground});
}

TEST_CASE("ModeLine shows the active mode's name", "[ModeLine]") {
    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    modeLine.paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find(U"(c-mode)") != std::u32string::npos);
}

TEST_CASE("ModeLine recomputes its text fresh on every paint call", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    modeLine.paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find(U"L1:C1") != std::u32string::npos);

    buffer.SetPoint(3); // end of "abc"
    modeLine.paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find(U"L1:C4") != std::u32string::npos);
}

TEST_CASE("ModeLine shows a modified marker only when the buffer has unsaved changes", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    REQUIRE_FALSE(buffer.Modified());
    modeLine.paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find(U"*scratch") == std::u32string::npos);

    buffer.InsertAtPoint("!");
    REQUIRE(buffer.Modified());
    modeLine.paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find(U"*scratch") != std::u32string::npos);
}
