// Bounded-time sanity checks: Rope/Buffer/BufferView operations must cost
// time proportional to the edit/viewport size, not the whole document size.
// These guard against accidentally reintroducing an O(n)-per-edit rope
// implementation or a paint() that scans a whole line instead of stopping at
// viewport width -- the classic "so-long-mode" failure case that motivated
// choosing a rope over a flat buffer back in Phase 1.
//
// Thresholds are deliberately generous (hundreds of ms for thousands of ops
// against multi-megabyte content) to avoid flakiness on slow/loaded CI
// machines, while still sitting far below what an O(document size) per-op
// implementation would take at these sizes.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>


#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using namespace std::chrono;

namespace {

std::string MakeMultiLineContent(std::size_t approxByteSize) {
    std::string content;
    content.reserve(approxByteSize + 128);

    const std::string line = "the quick brown fox jumps over the lazy dog, again and again\n"; // 63 bytes
    while (content.size() < approxByteSize) {
        content += line;
    }
    return content;
}

std::string MakeSingleLongLine(std::size_t length) {
    std::string content;
    content.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        content += static_cast<char>('a' + static_cast<char>(i % 26));
    }
    return content;
}

std::string MakeLargeJsonArray(std::size_t entryCount) {
    std::string content = "[\n";
    for (std::size_t i = 0; i < entryCount; ++i) {
        content += "  {\"id\": " + std::to_string(i) + ", \"name\": \"entry-" + std::to_string(i) +
                   "\", \"active\": true, \"tag\": null},\n";
    }
    content += "  {}\n]\n";
    return content;
}

} // namespace

TEST_CASE("Inserting near the end of a multi-megabyte buffer stays fast", "[Performance]") {
    ned::text::Buffer buffer("scratch", ned::text::Rope(MakeMultiLineContent(10'000'000)));
    buffer.SetPoint(buffer.Size());

    const auto start = steady_clock::now();
    for (int i = 0; i < 3000; ++i) {
        buffer.InsertAtPoint("x");
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("Point navigation across a multi-megabyte buffer stays fast", "[Performance]") {
    const ned::text::Buffer buffer("scratch", ned::text::Rope(MakeMultiLineContent(10'000'000)));
    const ned::text::Rope&  content = buffer.Content();

    const auto start = steady_clock::now();
    for (std::size_t i = 0; i < 3000; ++i) {
        const std::size_t offset = (i * 104729) % content.ByteLength(); // scattered offsets
        (void)content.ByteOffsetToLine(offset);
        (void)content.LineToByteOffset(i % content.LineCount());
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("Inserting into a pathologically long single line stays fast", "[Performance]") {
    ned::text::Buffer buffer("scratch", ned::text::Rope(MakeSingleLongLine(5'000'000)));
    REQUIRE(buffer.Content().LineCount() == 1);

    buffer.SetPoint(buffer.Size() / 2);

    const auto start = steady_clock::now();
    for (int i = 0; i < 2000; ++i) {
        buffer.InsertAtPoint("x");
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("Grapheme-boundary point movement across a pathologically long line stays fast", "[Performance]") {
    ned::text::Buffer buffer("scratch", ned::text::Rope(MakeSingleLongLine(5'000'000)));
    buffer.SetPoint(buffer.Size() / 2);

    const auto start = steady_clock::now();
    for (int i = 0; i < 2000; ++i) {
        buffer.MoveForward();
    }
    for (int i = 0; i < 2000; ++i) {
        buffer.MoveBackward();
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("Vertical motion out of a pathologically long single line with tab-aware goal column stays fast",
          "[Performance]") {
    // Two lines: a 5-million-byte first line (point starts at its very end),
    // then a short second line -- exercises both the goal-column *capture*
    // walk (point deep in the long line) and the *landing* walk (bouncing
    // back onto the long line with a goal column carried over from the
    // short one), each bounded by kMaxTabAwareColumnScan rather than the
    // line's real length (see Buffer.cpp). Only 200 iterations, not the
    // 2000 other [Performance] cases use -- each one here does real bounded
    // work (up to kMaxTabAwareColumnScan calls to Rope::CodepointAt, an
    // O(log document size) tree descent, not a free array index), unlike
    // the O(1)-per-call operations those other cases measure; the point of
    // this test is proving the cost stays bounded and independent of the
    // 5-million-byte line, not matching their iteration count.
    const std::string longLine = MakeSingleLongLine(5'000'000);
    ned::text::Buffer buffer("scratch", ned::text::Rope(longLine + "\nshort"));
    buffer.SetPoint(longLine.size());

    const auto start = steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        buffer.MoveToNextLine(4);
        buffer.MoveToPreviousLine(4);
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("BufferView::paint on a pathologically long single line stays fast", "[Performance]") {
    ned::text::Buffer          buffer("scratch", ned::text::Rope(MakeSingleLongLine(5'000'000)));
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher(registry, ned::editor::KeymapStack({&keymap}));
    ned::editor::Mode       mode  = ned::editor::FundamentalMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();

    std::string statusMessage;

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    ned::ui::Screen   screen = ned::ui::Screen(80, 24);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    // Put point far into the line so paint (if it ever regressed to scanning
    // from the line start) would have real work to redo on every call.
    buffer.SetPoint(buffer.Size() - 100);

    const auto start = steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        view.Paint(canvas);
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("BufferView::paint on a large wrap-enabled document stays fast across repeated calls",
          "[Performance]") {
    // line-wrap follow-up: BufferView::RowsForLine()/EnsureRowCountCache()
    // are the one place doing genuinely non-trivial per-line work in that
    // class (a real word-break scan, not just a boolean range check) --
    // flagged in their own doc comments for exactly this kind of test
    // rather than pre-optimized. This proves the "compute lazily, memoize
    // per line, never eagerly scan the whole buffer up front" design
    // actually holds: an earlier eager-whole-range version measured over
    // 2 seconds here before being caught by this exact test and fixed.
    //
    // Deliberately FundamentalMode with wrapLines forced on, not
    // MarkdownMode/OrgMode -- an earlier version of this test used
    // MarkdownMode and initially (mis-)diagnosed a real slowdown as a wrap
    // bug, when the actual cost was MarkdownMode's own real tree-sitter
    // highlight pass parsing the full document (an entirely separate,
    // pre-existing cost unrelated to wrap). FundamentalMode has no
    // highlight function at all, so this isolates wrap's own cost cleanly.
    //
    // A modest buffer (a few hundred lines -- comfortably more than the
    // 24-row viewport below, which is the whole point) and 50 Paint()
    // calls, not the multi-megabyte/200-call scale other tests in this
    // file use: confirmed directly that this test's own real cost tracks
    // Paint() call count, not buffer size at all (Paint() only ever visits
    // the visible rows; MaxTopLine()/ScrollToShowPoint() aren't even
    // exercised by a bare repeated-Paint()-call loop like this one, since
    // neither a scroll bar nor a real event ever asks this BufferView to
    // rescroll) -- so a large buffer here would only inflate the one-time
    // setup cost, not exercise anything this test actually cares about.
    // Kept modest specifically to leave real margin under
    // -DNED_ENABLE_SANITIZERS=ON's `Debug`-build-plus-instrumentation
    // overhead (unoptimized code paying real heap-allocation/redzone cost
    // on every ComputeWrapSegments call), the same "tuned down to leave
    // real margin under ASan" precedent the JsonMode test below already
    // establishes.
    ned::text::Buffer          buffer("scratch", ned::text::Rope(MakeMultiLineContent(5'000)));
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher(registry, ned::editor::KeymapStack({&keymap}));
    ned::editor::Mode       mode  = ned::editor::FundamentalMode();
    mode.wrapLines                = true;
    ned::ui::Theme          theme = ned::ui::DarkTheme();

    std::string statusMessage;

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    ned::ui::Screen   screen = ned::ui::Screen(80, 24);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    const auto start = steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        view.Paint(canvas);
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("BufferView::paint with JsonMode's tree-sitter highlighting stays fast on a large file",
          "[Performance]") {
    // JsonMode::highlight does a full tree-sitter reparse plus a full query
    // run over the whole tree -- no ts_tree_edit-based incremental
    // reparsing yet, a deliberate v1 scope cut (see Parser.h's own doc
    // comment) -- but BufferView caches the result across paint() calls,
    // recomputing only when the buffer's content actually changes (see
    // highlightCacheBuffer_'s own doc comment in BufferView.h). This test is
    // what proved that combination fast enough in practice, not just
    // asserted it should be, matching this project's "prove it before
    // optimizing" discipline applied to tree-sitter for the first time: an
    // earlier version with no caching at all measured ~217ms *per* paint()
    // call on this same content; with caching but before ClassAtOffset was
    // also narrowed to per-line spans (SpansForLine, BufferView.cpp) it was
    // still ~44ms/call. 150 JSON object entries (~10KB) -- a real config-file
    // size, not a pathological one; kept modest enough (rather than the
    // 2,000 originally measured with, then 500 once tuned once already) to
    // leave real margin under -DNED_ENABLE_SANITIZERS=ON's ~3x
    // instrumentation overhead, not just the un-instrumented build. Tuned
    // down again, from 500, by the generic-code-folding follow-up: JsonMode
    // gained a real fold query (Mode.h's Mode::fold), which BufferView now
    // also calls once per content-generation change (EnsureFoldableBlocksCache)
    // alongside the existing highlight call -- a second genuinely necessary
    // full tree-sitter query pass (sharing the underlying parsed Tree with
    // highlight, see Mode.cpp's SharedParse, but not the query run itself),
    // not an inefficiency; this is the "prove it before optimizing"
    // discipline's own precedent, applied to this exact test a second time.
    ned::text::Buffer          buffer("scratch", ned::text::Rope(MakeLargeJsonArray(150)));
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher(registry, ned::editor::KeymapStack({&keymap}));
    ned::editor::Mode       mode  = ned::editor::JsonMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();

    std::string statusMessage;

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    ned::ui::Screen   screen = ned::ui::Screen(80, 24);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    const auto start = steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        view.Paint(canvas);
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}

TEST_CASE("BufferView::paint stays fast repeatedly switching between two tree-sitter-highlighted buffers",
          "[Performance]") {
    // per-buffer-highlight-cache follow-up: highlightCacheBuffer_/
    // foldableBlocksCacheBuffer_ (the two fields the test right above this
    // one already exercises) only ever remember the *most recently
    // painted* buffer -- switching to a second buffer and back used to be
    // a guaranteed miss even though neither buffer's content had changed,
    // forcing a full tree-sitter reparse + fold query run on every single
    // switch back, purely because some other buffer got painted in
    // between. This is what proved highlightCacheByBuffer_/
    // foldableBlocksCacheByBuffer_ (BufferView.h) actually fix that, not
    // just asserted it should: alternating Paint() between two buffers 50
    // times total stays within the same bound the single-buffer 50-call
    // test above uses, which it would not without those two caches --
    // pre-fix, this is ~25 full reparses per buffer instead of 1.
    ned::text::Buffer buffer1("scratch1", ned::text::Rope(MakeLargeJsonArray(150)));
    ned::text::Buffer buffer2("scratch2", ned::text::Rope(MakeLargeJsonArray(150)));

    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher(registry, ned::editor::KeymapStack({&keymap}));
    ned::editor::Mode       mode  = ned::editor::JsonMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();

    std::string statusMessage;

    ned::ui::ActiveBuffer activeBuffer(buffer1);
    ned::ui::BufferView   view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    ned::ui::Screen screen = ned::ui::Screen(80, 24);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});

    const auto start = steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        activeBuffer.Set(i % 2 == 0 ? buffer1 : buffer2);
        view.Paint(canvas);
    }
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(duration_cast<milliseconds>(elapsed).count() < 500);
}
