#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Editor/ChunkedHighlight.h"
#include "Editor/Mode.h"

using ned::editor::HighlightSpan;
using ned::editor::HighlightSweepChunk;
using ned::editor::HighlightWindow;
using ned::editor::MarkdownMode;
using ned::editor::SyntaxClass;

// perf/parallel-highlighting-round-1 follow-up: HighlightSweepChunk is the
// one-chunk-at-a-time step behind Minimap's background sweep. The whole
// point of chunking is that it must be a scheduling change only, never a
// rendering one -- these tests pin that equivalence directly, with no
// EventLoop/DeadlineTimer involved (HighlightSweepChunk is pure and
// synchronous, driven here by a plain loop instead of a real timer).

namespace {

bool SameSpanSet(std::vector<HighlightSpan> a, std::vector<HighlightSpan> b) {
    const auto key = [](const HighlightSpan& s) {
        return std::tuple{s.startByte, s.endByte, static_cast<int>(s.syntaxClass), s.captureId};
    };
    std::sort(a.begin(), a.end(), [&](const auto& x, const auto& y) { return key(x) < key(y); });
    std::sort(b.begin(), b.end(), [&](const auto& x, const auto& y) { return key(x) < key(y); });
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](const auto& x, const auto& y) {
               return key(x) == key(y);
           });
}

// A mode whose spans are exactly known so the dedup-by-window-start
// boundary logic can be checked precisely rather than through a real
// grammar's own output.
ned::editor::Mode FixedSpansMode(std::vector<HighlightSpan> spans) {
    ned::editor::Mode mode;
    mode.name      = "fixed-spans-mode";
    mode.highlight = [spans = std::move(spans)](std::string_view, HighlightWindow window) {
        std::vector<HighlightSpan> result;
        for (const HighlightSpan& s : spans) {
            if (s.startByte < window.endByte && s.endByte > window.startByte) { // intersects
                result.push_back(s);
            }
        }
        return result;
    };
    return mode;
}

// Runs the sweep to completion via a plain loop (a test's stand-in for the
// real timer-driven ticking) and returns the accumulated spans.
std::vector<HighlightSpan> SweepToCompletion(const ned::editor::Mode& mode, std::string_view text,
                                             std::size_t chunkBytes) {
    std::vector<HighlightSpan> out;
    std::size_t                cursor = 0;
    std::size_t                ticks  = 0;
    while (cursor < text.size()) {
        cursor = HighlightSweepChunk(mode.highlight, text, cursor, chunkBytes, out);
        REQUIRE(++ticks < 1'000'000); // runaway guard, not a real bound
    }
    return out;
}

} // namespace

TEST_CASE("A span crossing a chunk boundary is captured exactly once, by the chunk it starts in", "[ChunkedHighlight]") {
    // Three spans: one fully inside the first chunk, one straddling the
    // boundary between chunks 1 and 2 (starts in chunk 1), one fully inside
    // chunk 2 -- the shape that would duplicate or drop under an off-by-one
    // in the >= cursor filter.
    const ned::editor::Mode mode = FixedSpansMode({
        {.startByte = 2, .endByte = 5, .syntaxClass = SyntaxClass::Keyword},
        {.startByte = 8, .endByte = 14, .syntaxClass = SyntaxClass::String}, // crosses byte 10
        {.startByte = 11, .endByte = 15, .syntaxClass = SyntaxClass::Comment},
    });
    const std::string       text(20, 'x');

    std::vector<HighlightSpan> out;
    std::size_t                cursor = HighlightSweepChunk(mode.highlight, text, 0, 10, out);
    REQUIRE(cursor == 10);
    REQUIRE(out.size() == 2); // the keyword span, and the straddling string span (starts at 8, < 10)

    cursor = HighlightSweepChunk(mode.highlight, text, cursor, 10, out);
    REQUIRE(cursor == 20);
    REQUIRE(out.size() == 3); // the comment span added; the straddling one NOT duplicated

    const std::vector<HighlightSpan> whole = mode.highlight(text, HighlightWindow{});
    REQUIRE(SameSpanSet(out, whole));
}

TEST_CASE("HighlightSweepChunk on an empty document completes with no work", "[ChunkedHighlight]") {
    const ned::editor::Mode    mode = FixedSpansMode({});
    std::vector<HighlightSpan> out;
    const std::size_t          cursor = HighlightSweepChunk(mode.highlight, "", 0, 4096, out);
    REQUIRE(cursor == 0);
    REQUIRE(out.empty());
}

TEST_CASE("Sweeping a real injection-heavy markdown document matches one whole-document call", "[ChunkedHighlight]") {
    // The exact shape KEYBENCH measured the cost against: many short
    // paragraphs, each markdown_inline injection its own sub-parse -- the
    // scenario a chunk-boundary bug is most likely to show up in, since
    // injected regions are exactly the spans most likely to straddle an
    // arbitrary byte window.
    std::string text;
    while (text.size() < 40000) {
        text += "alpha *beta* [gamma](https://example.com) `delta` epsilon\n";
    }
    const ned::editor::Mode mode = MarkdownMode();
    REQUIRE(mode.highlight);

    const std::vector<HighlightSpan> whole = mode.highlight(text, HighlightWindow{});
    REQUIRE(whole.size() > 10); // sanity: the real grammar actually produced something

    // 1-byte chunks deliberately excluded: with ~700 injected regions in this
    // 40 KiB document, that's tens of thousands of ticks and was the case
    // that first surfaced CollectRawInjectionMatches's own unwindowed walk
    // (Editor/Injection.cpp) -- now fixed (MatchesInRange), but still not a
    // realistic chunk size to hold a runtime bound against here.
    for (const std::size_t chunkBytes : {37UL, 4096UL, 16384UL, 100000UL}) {
        const std::vector<HighlightSpan> swept = SweepToCompletion(mode, text, chunkBytes);
        INFO("chunkBytes = " << chunkBytes);
        REQUIRE(SameSpanSet(swept, whole));
    }
}

TEST_CASE("Sweeping a plain C++ document (no injections) matches one whole-document call", "[ChunkedHighlight]") {
    std::string text = "#include <vector>\n\nnamespace demo {\n\nclass Widget {\n  public:\n"
                       "    int Value() const { return value_; }\n\n  private:\n    int value_ = 0;\n};\n\n} // namespace demo\n";
    while (text.size() < 8000) {
        text += "int f" + std::to_string(text.size()) + "() { return 1; } // a comment\n";
    }
    const ned::editor::Mode mode = ned::editor::CppMode();
    REQUIRE(mode.highlight);

    const std::vector<HighlightSpan> whole = mode.highlight(text, HighlightWindow{});
    const std::vector<HighlightSpan> swept = SweepToCompletion(mode, text, 500);
    REQUIRE(SameSpanSet(swept, whole));
}
