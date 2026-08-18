#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Editor/CodeFold.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::CMode;
using ned::editor::CppMode;
using ned::editor::JavaScriptMode;
using ned::editor::JsonMode;
using ned::editor::PythonMode;
using ned::editor::TypeScriptMode;
using ned::editor::codefold::FoldableBlocks;
using ned::editor::codefold::FoldRegion;
using ned::editor::codefold::FoldRegionsWithDepth;
using ned::editor::codefold::FoldedLineRanges;
using ned::editor::codefold::ToggleFoldAtLine;
using ned::text::Buffer;

TEST_CASE("FoldableBlocks finds a C function body", "[CodeFold]") {
    const auto mode = CMode();
    const auto blocks = FoldableBlocks(mode, "int main(void) {\n    return 0;\n}\n");
    REQUIRE(blocks.size() == 1);
    REQUIRE(blocks[0].first == std::string("int main(void) ").size());
}

TEST_CASE("FoldableBlocks finds a C++ struct body and a function body", "[CodeFold]") {
    const auto mode   = CppMode();
    const auto blocks = FoldableBlocks(mode, "struct S {\n    int x;\n};\nint f() {\n    return 1;\n}\n");
    REQUIRE(blocks.size() == 2);
}

TEST_CASE("FoldableBlocks finds JSON objects and arrays", "[CodeFold]") {
    const auto mode   = JsonMode();
    const auto blocks = FoldableBlocks(mode, R"({"a": [1, 2, 3]})");
    REQUIRE(blocks.size() == 2); // the outer object and the inner array
}

TEST_CASE("FoldableBlocks finds a Python function body", "[CodeFold]") {
    const auto mode   = PythonMode();
    const auto blocks = FoldableBlocks(mode, "def f():\n    return 1\n");
    REQUIRE(blocks.size() == 1);
}

TEST_CASE("FoldableBlocks finds a JavaScript function body and object literal", "[CodeFold]") {
    const auto mode   = JavaScriptMode();
    const auto blocks = FoldableBlocks(mode, "function f() {\n    return {a: 1};\n}\n");
    REQUIRE(blocks.size() == 2);
}

TEST_CASE("FoldableBlocks finds a TypeScript class body", "[CodeFold]") {
    const auto mode   = TypeScriptMode();
    const auto blocks = FoldableBlocks(mode, "class C {\n    f(): void {}\n}\n");
    REQUIRE(blocks.size() >= 1);
}

TEST_CASE("FoldableBlocks returns nothing for a mode with no fold query", "[CodeFold]") {
    const ned::editor::Mode mode = ned::editor::FundamentalMode();
    const auto               blocks = FoldableBlocks(mode, "anything at all");
    REQUIRE(blocks.empty());
}

TEST_CASE("FoldedLineRanges hides a collapsed block's body through its closing line", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() == 1);

    buffer.SetFoldMarker(blocks[0].first, Buffer::FoldMarker::Collapsed);
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges[0].first == 1);  // hides starting the line after "int main(void) {"
    REQUIRE(ranges[0].second == 3); // through and including "}" (line 2)
}

TEST_CASE("FoldedLineRanges skips a stale marker with no matching block", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");

    // A marker at a byte offset that doesn't correspond to any real
    // foldable block's own startByte -- e.g. set by hand, or left over
    // after content changed underneath it.
    buffer.SetFoldMarker(0, Buffer::FoldMarker::Collapsed);
    const auto blocks = FoldableBlocks(mode, buffer.Text());
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.empty());
}

TEST_CASE("ToggleFoldAtLine collapses then expands the innermost block starting on that line", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    REQUIRE(buffer.FoldMarkerAt(blocks[0].first).has_value());

    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    REQUIRE_FALSE(buffer.FoldMarkerAt(blocks[0].first).has_value());
}

TEST_CASE("ToggleFoldAtLine is a no-op when no block starts on that line", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE_FALSE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 1)); // "    return 0;" -- no block starts here
}

TEST_CASE("ToggleFoldAtLine picks the innermost block when two start on the same line", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    // Both the outer function body and (degenerately) an inner compound
    // statement start on the same line here.
    buffer.InsertAtPoint("int main(void) { if (1) { return 0; } }\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() >= 2);
    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));

    // The innermost (smallest) block is the one that got marked.
    const auto* innermost = &blocks[0];
    for (const auto& block : blocks) {
        if ((block.second - block.first) < (innermost->second - innermost->first)) {
            innermost = &block;
        }
    }
    REQUIRE(buffer.FoldMarkerAt(innermost->first).has_value());
}

TEST_CASE("FoldRegionsWithDepth gives disjoint siblings depth 0", "[CodeFold]") {
    // Two independent, non-nested function bodies -- neither contains the
    // other, so both are top-level.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks = {{10, 20}, {30, 40}};
    const auto                                              regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 2);
    REQUIRE(regions[0].depth == 0);
    REQUIRE(regions[1].depth == 0);
}

TEST_CASE("FoldRegionsWithDepth gives increasing depth for properly nested blocks", "[CodeFold]") {
    // [0,100) contains [10,90) contains [20,80) -- three levels deep.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks = {{0, 100}, {10, 90}, {20, 80}};
    const auto                                              regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 3);
    REQUIRE(regions[0].depth == 0);
    REQUIRE(regions[1].depth == 1);
    REQUIRE(regions[2].depth == 2);
}

TEST_CASE("FoldRegionsWithDepth resets depth for a sibling after a nested block closes", "[CodeFold]") {
    // [0,10) contains [1,5); [11,20) is a separate top-level sibling after
    // the first one closes.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks = {{0, 10}, {1, 5}, {11, 20}};
    const auto                                              regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 3);
    REQUIRE(regions[0].depth == 0);
    REQUIRE(regions[1].depth == 1);
    REQUIRE(regions[2].depth == 0);
}

TEST_CASE("FoldRegionsWithDepth from a real C++ file exceeds the 4-column display cap when deeply nested", "[CodeFold]") {
    const auto  mode = CppMode();
    std::string source;
    // Nest compound_statements 6 deep -- BufferView caps *display* at
    // min(depth, 3), but FoldRegionsWithDepth itself reports the real,
    // uncapped depth.
    source += "void f() {\n";
    for (int i = 0; i < 5; ++i) {
        source += "if (1) {\n";
    }
    source += "int x = 1;\n";
    for (int i = 0; i < 6; ++i) {
        source += "}\n";
    }

    const auto blocks  = FoldableBlocks(mode, source);
    const auto regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 6);

    int maxDepth = 0;
    for (const FoldRegion& region : regions) {
        maxDepth = std::max(maxDepth, region.depth);
    }
    REQUIRE(maxDepth == 5); // 6 nested blocks, 0-indexed depth
}
