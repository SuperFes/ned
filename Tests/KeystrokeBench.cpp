#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/HighlightCache.h"
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
#include "UI/Minimap.h"
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

    // The file that was actually reported slow, with the Mode ned really
    // resolves for it -- rather than a same-sized C++ file, which is what the
    // first version of this benchmark assumed and is not the same workload.
    {
        std::ifstream      roadmapIn("ROADMAP.md");
        std::ostringstream roadmapContent;
        roadmapContent << roadmapIn.rdbuf();
        const std::string roadmap = roadmapContent.str();
        REQUIRE(roadmap.size() > 10000);

        ned::text::Buffer md{"ROADMAP.md"};
        md.InsertAtPoint(roadmap);
        ned::editor::Mode     mdMode = ned::editor::MarkdownMode();
        ned::ui::ActiveBuffer mdActive{md};
        ned::ui::BufferView   mdView(mdActive, killRing, registers, promptHistory, bufferList, dispatcher, status,
                                     mdMode, theme);
        mdView.SetBox_(box);
        md.SetPoint(roadmap.size() / 2);
        for (int i = 0; i < 5; ++i) {
            ned::ui::Canvas c(screen, box);
            mdView.Paint(c);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            mdView.OnEvent(ned::ui::test::Character('x'));
            ned::ui::Canvas c(screen, box);
            mdView.Paint(c);
        }
        const auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        WARN("  ROADMAP.md (" << (roadmap.size() / 1024) << " KiB, markdown-mode): " << (us / 20)
                              << " us per keystroke");

        // Attribution. BufferView asks the Mode for several different things
        // per frame, each its own whole-file tree-sitter query, and the
        // caches for all of them are keyed on ContentGeneration -- so an edit
        // invalidates every one at once. Time them individually.
        const auto timeIt = [&](const char* label, auto&& fn) {
            fn(); // warm
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 10; ++i) {
                md.InsertAtPoint("y"); // force a real generation change each time
                fn();
            }
            const auto each =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count() / 10;
            WARN("    " << label << ": " << each << " us");
        };

        // Controlled: same byte count, different numbers of `inline` nodes.
        // markdown's injections.scm injects markdown_inline into *every*
        // inline node, and each injection is its own parse -- so if that is
        // the cost, many short lines must be far worse than one long
        // paragraph of identical size.
        {
            const std::string word = "alpha beta gamma delta epsilon ";
            std::string       manyLines;
            while (manyLines.size() < 120000) {
                manyLines += word + "\n"; // ~4000 short paragraphs
            }
            std::string onePara;
            while (onePara.size() < 120000) {
                onePara += word; // one enormous paragraph, no newlines
            }

            const auto timeHighlight = [&](const char* label, const std::string& text) {
                ned::editor::Mode m = ned::editor::MarkdownMode();
                m.highlight(text, ned::editor::HighlightWindow{}); // warm
                const auto begin = std::chrono::steady_clock::now();
                for (int i = 0; i < 5; ++i) {
                    std::string edited = text;
                    edited += static_cast<char>('a' + i); // force a fresh parse each time
                    m.highlight(edited, ned::editor::HighlightWindow{});
                }
                const auto each =
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count() / 5;
                WARN("    " << label << ": " << each << " us");
            };
            timeHighlight("~4000 short lines (120 KiB)", manyLines);
            timeHighlight("1 huge paragraph  (120 KiB)", onePara);
        }

        // The minimap keeps its *own* whole-file highlight cache, also keyed
        // on ContentGeneration -- so with it enabled a keystroke pays the
        // whole-document highlight twice. This benchmark paints no minimap,
        // so every other number here is the optimistic case.
        // Two consumers asking for the same buffer's spans in one frame --
        // BufferView and Minimap. Through the shared cache the second is a
        // hit; before it, this was two whole-document highlights.
        // The real editor: a BufferView *and* a Minimap painting the same
        // buffer every frame, which is the default configuration.
        {
            ned::ui::Minimap minimap(mdActive, mdMode, theme);
            const ned::ui::Box mmBox{.x_min = 150, .x_max = 159, .y_min = 0, .y_max = 44};
            minimap.SetBox_(mmBox);
            mdView.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 149, .y_min = 0, .y_max = 44});
            for (int i = 0; i < 3; ++i) {
                ned::ui::Canvas c(screen, ned::ui::Box{.x_min = 0, .x_max = 149, .y_min = 0, .y_max = 44});
                mdView.Paint(c);
                ned::ui::Canvas mc(screen, mmBox);
                minimap.Paint(mc);
            }
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 10; ++i) {
                mdView.OnEvent(ned::ui::test::Character('z'));
                ned::ui::Canvas c(screen, ned::ui::Box{.x_min = 0, .x_max = 149, .y_min = 0, .y_max = 44});
                mdView.Paint(c);
                ned::ui::Canvas mc(screen, mmBox);
                minimap.Paint(mc);
            }
            const auto each =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count() / 10;
            WARN("    buffer + minimap, per keystroke: " << each << " us");
        }

        timeIt("buffer.Text() copy alone ", [&] { return md.Text().size(); });
        timeIt("mode.highlight(text, ned::editor::HighlightWindow{})     ", [&] { return mdMode.highlight ? mdMode.highlight(md.Text(), ned::editor::HighlightWindow{}).size() : 0U; });
        timeIt("mode.fold(text)          ", [&] { return mdMode.fold ? mdMode.fold(md.Text()).size() : 0U; });
        timeIt("mode.symbolKind(text)    ", [&] { return mdMode.symbolKind ? mdMode.symbolKind(md.Text()).size() : 0U; });
        timeIt("mode.testDiscovery(text) ", [&] { return mdMode.testDiscovery ? mdMode.testDiscovery(md.Text()).size() : 0U; });
    }

    // How the cost scales with document size -- the practical question is
    // "how big a file before typing stops feeling instant".
    for (const std::size_t lines : {200U, 500U, 1000U, 2000U}) {
        std::string trimmed;
        std::size_t count = 0;
        for (std::size_t i = 0; i < source.size(); ++i) {
            trimmed += source[i];
            if (source[i] == '\n' && ++count >= lines) {
                break;
            }
        }
        ned::text::Buffer sized{"sized.cpp"};
        sized.InsertAtPoint(trimmed);
        ned::ui::ActiveBuffer sizedActive{sized};
        ned::ui::BufferView   sizedView(sizedActive, killRing, registers, promptHistory, bufferList, dispatcher,
                                        status, mode, theme);
        sizedView.SetBox_(box);
        sized.SetPoint(trimmed.size() / 2);
        for (int i = 0; i < 5; ++i) {
            ned::ui::Canvas c(screen, box);
            sizedView.Paint(c);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) {
            sizedView.OnEvent(ned::ui::test::Character('x'));
            ned::ui::Canvas c(screen, box);
            sizedView.Paint(c);
        }
        const auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        WARN("  " << lines << " lines (" << (trimmed.size() / 1024) << " KiB): " << (us / 20) << " us per keystroke");
    }

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
