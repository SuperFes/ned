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

// Mirrors BufferViewIndentGuideDepthTest.cpp's own Fixture shape.
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
struct TabGlyphSettingsGuard {
    ~TabGlyphSettingsGuard() {
        ned::editor::SetTabGlyphsEnabled(false);
        ned::editor::SetIndentGuidesEnabled(false);
    }
};

} // namespace

TEST_CASE("A real tab renders as plain space cells when tab glyphs are disabled", "[BufferView][WhitespaceSettings]") {
    const TabGlyphSettingsGuard guard;
    ned::editor::SetTabGlyphsEnabled(false);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("\ty\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x;

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(gutterWidth, 0).character == " ");
}

TEST_CASE("A real tab's first cell renders as the tab glyph when tab glyphs are enabled",
          "[BufferView][WhitespaceSettings]") {
    const TabGlyphSettingsGuard guard;
    ned::editor::SetTabGlyphsEnabled(true);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("\ty\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x;

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const ned::ui::Cell& tabCell = screen.PixelAt(gutterWidth, 0);
    REQUIRE(tabCell.character != " ");
    REQUIRE(tabCell.foreground_color == fixture.theme.tabGlyphForeground);
    // A default tab width of 4 still only ever occupies one glyph cell --
    // the rest of the expansion (columns 1-3 here) stays blank space, same
    // as the disabled case.
    REQUIRE(screen.PixelAt(gutterWidth + 1, 0).character == " ");
}

TEST_CASE("Tab glyphs take priority over an indent guide landing on the same cell",
          "[BufferView][WhitespaceSettings]") {
    const TabGlyphSettingsGuard guard;
    ned::editor::SetTabGlyphsEnabled(true);
    ned::editor::SetIndentGuidesEnabled(true);

    Fixture fixture;
    // Two leading tabs (default tab width 4): the second tab's first cell
    // lands exactly on display column 4, an indent-guide boundary, but the
    // tab glyph should still win that cell.
    fixture.buffer.InsertAtPoint("\t\ty\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    REQUIRE(view.CursorPosition().has_value());
    const int gutterWidth = view.CursorPosition()->x;

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(gutterWidth + 4, 0).foreground_color == fixture.theme.tabGlyphForeground);
}
