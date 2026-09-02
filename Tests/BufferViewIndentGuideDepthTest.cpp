#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/WhitespaceSettings.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture shape.
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

// WhitespaceSettings is process-wide state -- mirrors WhitespaceSettingsTest.cpp's own guard.
struct IndentGuideSettingsGuard {
    ~IndentGuideSettingsGuard() {
        ned::editor::SetIndentGuidesEnabled(false);
        ned::editor::SetIndentGuideDepthColorsEnabled(true);
    }
};

} // namespace

TEST_CASE("Indent guides at different nesting levels get different colors when depth-colorized",
          "[BufferView][WhitespaceSettings]") {
    const IndentGuideSettingsGuard guard;
    ned::editor::SetIndentGuidesEnabled(true);
    ned::editor::SetIndentGuideDepthColorsEnabled(true);

    Fixture fixture;
    // 9 leading spaces then 'y' -- default tab width 4, so guide columns
    // land at display column 4 (1st level) and 8 (2nd level), both still
    // inside the leading-whitespace run (indentEnd == 9).
    fixture.buffer.InsertAtPoint("         y\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x; // point is at offset 0

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(fixture.theme.indentGuideDepthPalette.size() >= 2);
    REQUIRE(screen.PixelAt(gutterWidth + 4, 0).foreground_color == fixture.theme.indentGuideDepthPalette[0]);
    REQUIRE(screen.PixelAt(gutterWidth + 8, 0).foreground_color == fixture.theme.indentGuideDepthPalette[1]);
}

TEST_CASE("Indent guides use one flat color when depth colors are disabled", "[BufferView][WhitespaceSettings]") {
    const IndentGuideSettingsGuard guard;
    ned::editor::SetIndentGuidesEnabled(true);
    ned::editor::SetIndentGuideDepthColorsEnabled(false);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("         y\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x;

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(gutterWidth + 4, 0).foreground_color == fixture.theme.indentGuideForeground);
    REQUIRE(screen.PixelAt(gutterWidth + 8, 0).foreground_color == fixture.theme.indentGuideForeground);
}
