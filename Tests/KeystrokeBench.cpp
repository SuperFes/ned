#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/RecencyGlow.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// A diagnostic, not an assertion -- hidden from the default run by Catch2's
// leading-dot convention, and run on demand with `ned_tests "[keybench]"`.
//
// It exists because every ordinary measurement lied about this. Whole-process
// CPU while typing said "fine" (a few ticks), because the expensive part is
// not ned burning CPU -- and neither is it the terminal, which was the next
// wrong guess. It is one call, once per keystroke, and nothing short of
// timing the keystroke path itself showed that.
TEST_CASE(". KEYBENCH: per-keystroke cost through the real paint path", "[.][keybench]") {
    std::ifstream      in("Source/UI/BufferView/Paint.cpp");
    std::ostringstream content;
    content << in.rdbuf();
    std::string source = content.str();
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

    const auto bench = [&](const char* label) {
        for (int i = 0; i < 5; ++i) { // warm the highlight cache
            ned::ui::Canvas c(screen, box);
            view.Paint(c);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) {
            view.OnEvent(ned::ui::test::Character('x')); // a real self-insert
            ned::ui::Canvas c(screen, box);
            view.Paint(c);
        }
        const auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        WARN(label << ": " << (us / 50) << " us per keystroke+repaint");
    };

    ned::editor::SetRecencyGlowEnabled(false);
    bench("glow off, no selection");

    ned::editor::SetRecencyGlowEnabled(true);
    bench("glow on,  no selection");

    ned::editor::SetRecencyGlowEnabled(false);
    buffer.SetMark(0); // select from start of buffer to point -- a screenful of selected cells
    bench("glow off, SELECTION  ");
    buffer.ClearMark();

    // Same buffer, same size, no tree-sitter mode at all: isolates the
    // highlight/parse cost from everything else the frame does.
    mode = ned::editor::Mode{.name = "fundamental-mode"};
    bench("no syntax mode      ");
}
