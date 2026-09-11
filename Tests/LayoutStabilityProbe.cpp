#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// A diagnostic, not an assertion -- hidden by Catch2's leading-dot convention,
// run with `ned_tests "[layoutprobe]"`.
//
// "Lines move around in a wonky way while typing" was reported twice against a
// real editing session and is a row-count question, so answer it by counting
// rows rather than by watching a terminal. Each frame records where every
// buffer line landed on screen and how wide the gutter was; anything that
// moves without the text above it changing length is the thing to explain.
TEST_CASE(". LAYOUTPROBE: what moves on screen while typing", "[.][layoutprobe]") {
    std::ifstream      in("Source/UI/BufferView/Paint.cpp");
    std::ostringstream content;
    content << in.rdbuf();
    const std::string source = content.str();
    REQUIRE(source.size() > 10000);

    ned::text::Buffer buffer{"Paint.cpp"};
    buffer.InsertAtPoint(source);

    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode       mode  = ned::editor::CppMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             status;
    ned::ui::ActiveBuffer   active{buffer};

    ned::ui::BufferView view(active, killRing, registers, promptHistory, bufferList, dispatcher, status, mode, theme);
    const ned::ui::Box  box{.x_min = 0, .x_max = 159, .y_min = 0, .y_max = 44};
    view.SetBox_(box);
    ned::ui::Screen screen(160, 45);
    buffer.SetPoint(source.size() / 2);

    // GutterWidth() is private, so measure it the way the eye does: typing
    // one character on one line must advance the cursor exactly one column.
    // Any other delta means the gutter changed width underneath, which shifts
    // every line on screen horizontally.
    std::vector<int> cursorColumns;

    // Where the row carrying the cursor's own line lands, per frame. If this
    // wanders while the lines above it are untouched, something above is
    // adding or removing rows.
    std::vector<int> cursorRows;

    for (int i = 0; i < 40; ++i) {
        view.OnEvent(ned::ui::test::Character('x'));
        ned::ui::Canvas c(screen, box);
        view.Paint(c);
        const std::optional<ned::ui::Point> cursor = view.CursorPosition();
        cursorRows.push_back(cursor ? cursor->y : -1);
        cursorColumns.push_back(cursor ? cursor->x : -1);
    }

    int horizontalJumps = 0;
    for (std::size_t i = 1; i < cursorColumns.size(); ++i) {
        if (cursorColumns[i] - cursorColumns[i - 1] != 1) {
            ++horizontalJumps;
        }
    }
    WARN("  horizontal jumps (cursor advanced by other than 1 column): " << horizontalJumps);

    int moves = 0;
    for (std::size_t i = 1; i < cursorRows.size(); ++i) {
        if (cursorRows[i] != cursorRows[i - 1]) {
            ++moves;
        }
    }
    std::ostringstream rows;
    for (const int r : cursorRows) {
        rows << r << ' ';
    }
    WARN("  cursor row moved " << moves << " times while typing on one line");
    WARN("  rows: " << rows.str());
}
