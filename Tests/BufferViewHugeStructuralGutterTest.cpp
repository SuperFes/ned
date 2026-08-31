#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/HugeStructuralWindow.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// huge-file-structural-gutters follow-up: EnsureFoldableBlocksCache/
// EnsureSymbolGutterCache feed mode_.fold/mode_.symbolKind a bounded window
// (± editor::HugeStructuralWindowBytes() around the visible viewport)
// instead of the whole document for a huge (ITextStorage::IsHuge()) buffer --
// see BufferView::HugeStructuralWindow's own doc comment. These tests build a
// real huge (piece-table-backed) buffer via Buffer::FromHugeFile (small
// content is fine -- FromHugeFile doesn't itself check size, only
// BufferList::OpenFile's threshold gate does) and exercise both halves of
// that behavior: a construct outside the window isn't found (and widening
// the window finds it), and a construct reached only via a large,
// nonzero windowStart (scrolled deep into the buffer) is remapped back to
// its correct absolute line -- the "easy to get wrong" offset bookkeeping
// every prior huge-file windowing entry (HugeRegexScan, QueryReplace) flagged.

using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}

// Mirrors BufferViewSymbolGutterTest.cpp's GutterWidthWithSymbol / the fold
// column reservation the rest of this file's own fold tests already use --
// [status][diagnostic][gap][digits][gap][symbol][fold].
int GutterWidth(std::size_t totalLines, int foldColumn, int symbolColumn) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + static_cast<int>(std::to_string(totalLines).size()) +
           kLineNumberGap + symbolColumn + foldColumn;
}

// Process-wide state -- restored via RAII, HighlightSettingsTest.cpp's own
// MaxHighlightBytesGuard precedent.
struct HugeStructuralWindowBytesGuard {
    ~HugeStructuralWindowBytesGuard() {
        ned::editor::SetHugeStructuralWindowBytes(4 * 1024 * 1024);
    }
};

struct Fixture {
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode       mode = ned::editor::CMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();

    Buffer buffer;

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    explicit Fixture(std::string_view content) : buffer(Buffer::FromHugeFile(WriteTempFile("ned_huge_structural_gutter.c", content))) {
    }

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode,
                          theme);
    }
};

} // namespace

TEST_CASE("Fold gutter finds no block on a huge buffer whose closing brace lies outside a small window, "
          "and finds it once the window is widened",
          "[BufferView][HugeFile]") {
    const HugeStructuralWindowBytesGuard guard;

    // A single foldable block whose body is ~8 KB -- far bigger than the
    // small window this test configures below, so the window's own end
    // lands somewhere in the middle of the padding, before the closing "}".
    std::string content = "int main(void) {\n";
    for (int i = 0; i < 200; ++i) {
        content += "    // pad pad pad pad pad pad pad pad\n"; // 40 bytes/line
    }
    content += "    return 0;\n}\n";

    Fixture fixture{content};
    REQUIRE(fixture.buffer.Content().IsHuge());

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2}); // 3 rows visible
    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    const int         foldCol    = GutterWidth(totalLines, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;

    // Window margin (200 bytes) doesn't reach anywhere near the closing
    // brace ~8 KB into the block -- the truncated window's own parse can't
    // see the block close, so no "@fold" capture fires for this header line.
    ned::editor::SetHugeStructuralWindowBytes(200);
    view.Paint(canvas);
    REQUIRE(screen.PixelAt(foldCol, 0).character == " ");

    // Widening the window past the block's full size lets the same header
    // line's block be found -- proves this is genuinely windowing behavior,
    // not a permanent inability to fold huge buffers at all.
    ned::editor::SetHugeStructuralWindowBytes(20000);
    view.Paint(canvas);
    REQUIRE(screen.PixelAt(foldCol, 0).character == "⊟");
}

TEST_CASE("Fold gutter remaps a huge buffer's window-relative offsets back to the correct absolute line "
          "when the window starts deep in the file",
          "[BufferView][HugeFile]") {
    const HugeStructuralWindowBytesGuard guard;

    // ~20 KB of leading filler, well past a modest window margin, so
    // scrolling to the foldable block below gives EnsureFoldableBlocksCache
    // a large, nonzero windowStart -- exactly the offset-remap arithmetic
    // ("+= windowStart") every prior huge-file windowing entry flagged as
    // the easy-to-get-wrong step.
    std::string content;
    for (int i = 0; i < 500; ++i) {
        content += "// filler filler filler filler\n"; // ~32 bytes/line
    }
    const std::size_t targetLine = 500;
    content += "int add(int a, int b) {\n    return a + b;\n}\n";
    for (int i = 0; i < 10; ++i) {
        content += "// trailing filler so SetTopLine has room to scroll this far\n";
    }

    Fixture fixture{content};
    REQUIRE(fixture.buffer.Content().IsHuge());

    ned::editor::SetHugeStructuralWindowBytes(4096);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2}); // 3 rows visible
    view.SetTopLine(targetLine); // scroll so the function's own header line is row 0
    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    const int         foldCol    = GutterWidth(totalLines, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    // A wrong (missing or double-applied) remap would report this block at
    // some other, unrelated line -- one nowhere near the current viewport --
    // so no glyph would render on any of the 3 visible rows at all. Finding
    // it at exactly row 0 (the header's real line) proves the remap is
    // correct, not just that folding "works somewhere."
    REQUIRE(screen.PixelAt(foldCol, 0).character == "⊟");
}

TEST_CASE("Symbol gutter remaps a huge buffer's window-relative offsets back to the correct absolute line "
          "when the window starts deep in the file",
          "[BufferView][HugeFile]") {
    const HugeStructuralWindowBytesGuard guard;

    std::string content;
    for (int i = 0; i < 500; ++i) {
        content += "// filler filler filler filler\n";
    }
    const std::size_t targetLine = 500;
    content += "int add(int a, int b) { return a + b; }\n";
    for (int i = 0; i < 10; ++i) {
        content += "// trailing filler so SetTopLine has room to scroll this far\n";
    }

    Fixture fixture{content};
    REQUIRE(fixture.buffer.Content().IsHuge());

    ned::editor::SetHugeStructuralWindowBytes(4096);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1}); // 2 rows visible
    view.SetTopLine(targetLine);
    ned::ui::Screen screen = ned::ui::Screen(40, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    // No fold column here -- the definition is written on one line (no
    // multi-line block), so mode_.fold never reserves one (matches
    // BufferViewTest.cpp's own "A block written entirely on one line gets no
    // fold icon" precedent).
    const int symbolCol = GutterWidth(totalLines, /*foldColumn=*/0, /*symbolColumn=*/1) - 1;
    REQUIRE(screen.PixelAt(symbolCol, 0).character == "ƒ");
}
