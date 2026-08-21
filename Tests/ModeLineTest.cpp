#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "UI/ActiveBuffer.h"
#include "UI/ModeLine.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

ned::ui::Screen MakeScreen(int width, int height) {
    return ned::ui::Screen(width, height);
}

} // namespace

TEST_CASE("ModeLine shows a live load percentage when the loader published progress", "[ModeLine]") {
    ned::text::Buffer buffer("huge.txt");
    buffer.MarkLoading();

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    // No progress published (or an unknown total): the plain indicator.
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading...") != std::string::npos);
    REQUIRE(RowText(screen, 0, 60).find('%') == std::string::npos);

    auto progress        = std::make_shared<ned::text::LoadProgress>();
    progress->totalBytes = 200;
    progress->bytesRead.store(50);
    buffer.SetLoadProgress(progress);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading... 25%") != std::string::npos);

    // bytesRead past totalBytes (the file grew mid-load) clamps to 100.
    progress->bytesRead.store(999);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading... 100%") != std::string::npos);

    // FinishLoad clears both the loading state and the progress pointer.
    buffer.FinishLoad(ned::text::Rope("done"));
    REQUIRE(buffer.CurrentLoadProgress() == nullptr);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading") == std::string::npos);
}

TEST_CASE("ModeLine shows the buffer name and 1-indexed line:column", "[ModeLine]") {
    ned::text::Buffer buffer("myfile.txt", ned::text::Rope("hello\nworld"));
    buffer.SetPoint(8); // line 1 (0-indexed), col 2 (0-indexed) -> displayed L2:C3

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);

    const std::string row = RowText(screen, 0, 40);
    REQUIRE(row.find("myfile.txt") != std::string::npos);
    REQUIRE(row.find("L2:C3") != std::string::npos);

    // Endpoints of the gradient should match what ned::ui::Color::Interpolate
    // itself produces at t=0/t=1 -- NOT necessarily the theme's raw declared
    // colors bit-for-bit: Interpolate gamma-corrects (pow(x, 2.2) then
    // pow(_, 1/2.2), truncated back to uint8_t), which doesn't always
    // round-trip losslessly at the endpoints for an arbitrary starting RGB
    // value (confirmed against FTXUI's own real color.cpp, not assumed).
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
    REQUIRE(screen.PixelAt(39, 0).background_color ==
            ned::ui::Color::Interpolate(1.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);
}

TEST_CASE("ModeLine shows the active mode's name", "[ModeLine]") {
    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("(c-mode)") != std::string::npos);
}

TEST_CASE("ModeLine recomputes its text fresh on every paint call", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("L1:C1") != std::string::npos);

    buffer.SetPoint(3); // end of "abc"
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("L1:C4") != std::string::npos);
}

TEST_CASE("ModeLine uses the accent-tinted gradient only while its focus provider reports focused", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    // No provider set (every pre-existing construction site): plain gradient.
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));

    bool focused = true;
    modeLine.SetFocusProvider([&focused] { return focused; });
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineFocusedGradientStart, theme.modeLineFocusedGradientEnd));

    focused = false;
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
}

TEST_CASE("ModeLine shows a modified marker only when the buffer has unsaved changes", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    REQUIRE_FALSE(buffer.Modified());
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("*scratch") == std::string::npos);

    buffer.InsertAtPoint("!");
    REQUIRE(buffer.Modified());
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("*scratch") != std::string::npos);
}
