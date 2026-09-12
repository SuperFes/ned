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
using ned::editor::CSharpMode;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::JsonMode;
using ned::editor::KotlinMode;
using ned::editor::PythonMode;
using ned::editor::TomlMode;
using ned::editor::TypeScriptMode;
using ned::editor::YamlMode;
using ned::editor::codefold::FoldableBlocks;
using ned::editor::codefold::FoldedLineRanges;
using ned::editor::codefold::FoldRegion;
using ned::editor::codefold::FoldRegionsWithDepth;
using ned::editor::codefold::ToggleFoldAtLine;
using ned::text::Buffer;

TEST_CASE("FoldableBlocks finds a C function body", "[CodeFold]") {
    const auto mode   = CMode();
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
    const auto blocks = FoldableBlocks(mode, "{\n  \"a\": [\n    1,\n    2\n  ]\n}\n");
    REQUIRE(blocks.size() == 2); // the outer object and the inner array
}

TEST_CASE("FoldableBlocks finds a Python function body", "[CodeFold]") {
    const auto mode   = PythonMode();
    const auto blocks = FoldableBlocks(mode, "def f():\n    x = 1\n    return x\n");
    REQUIRE(blocks.size() == 1);
}

TEST_CASE("FoldableBlocks finds a JavaScript function body and object literal", "[CodeFold]") {
    const auto mode   = JavaScriptMode();
    const auto blocks = FoldableBlocks(mode, "function f() {\n    return {\n        a: 1,\n    };\n}\n");
    REQUIRE(blocks.size() == 2);
}

TEST_CASE("FoldableBlocks finds a TypeScript class body", "[CodeFold]") {
    const auto mode   = TypeScriptMode();
    const auto blocks = FoldableBlocks(mode, "class C {\n    f(): void {}\n}\n");
    REQUIRE(blocks.size() >= 1);
}

TEST_CASE("FoldableBlocks finds a Go function body, a struct body, and an interface body", "[CodeFold]") {
    const auto mode   = GoMode();
    const auto blocks = FoldableBlocks(mode, "type S struct {\n    X int\n}\n\n"
                                             "type I interface {\n    M()\n}\n\n"
                                             "func f() {\n    return\n}\n");
    REQUIRE(blocks.size() == 3);
}

TEST_CASE("FoldableBlocks finds a C# class body, a method body, and an initializer expression", "[CodeFold]") {
    const auto mode   = CSharpMode();
    const auto blocks = FoldableBlocks(mode, "class Widget {\n"
                                             "    int[] Xs = new int[] {\n        1, 2, 3,\n    };\n"
                                             "    int F() {\n"
                                             "        return 1;\n"
                                             "    }\n"
                                             "}\n");
    REQUIRE(blocks.size() == 3);
}

TEST_CASE("FoldableBlocks finds a Java class body, an interface body, a method body, and an array initializer",
          "[CodeFold]") {
    const auto mode   = JavaMode();
    const auto blocks = FoldableBlocks(mode, "interface Sized {\n"
                                             "    int size();\n"
                                             "}\n"
                                             "\n"
                                             "class Widget {\n"
                                             "    int[] xs = {\n        1, 2, 3,\n    };\n"
                                             "    int size() {\n"
                                             "        return 1;\n"
                                             "    }\n"
                                             "}\n");
    REQUIRE(blocks.size() == 4);
}

TEST_CASE("FoldableBlocks finds a Kotlin class body, a braced function body, a lambda, and a when-expression",
          "[CodeFold]") {
    const auto mode   = KotlinMode();
    const auto blocks = FoldableBlocks(mode, "class Widget {\n"
                                             "    fun size(): Int {\n"
                                             "        return listOf(1, 2).map {\n"
                                             "            it + 1\n"
                                             "        }.first()\n"
                                             "    }\n"
                                             "\n"
                                             "    fun kind(n: Int): String = when (n) {\n"
                                             "        0 -> \"zero\"\n"
                                             "        else -> \"many\"\n"
                                             "    }\n"
                                             "}\n");
    REQUIRE(blocks.size() == 4);
}

TEST_CASE("FoldableBlocks offers no Kotlin fold for an expression-bodied function or a braceless if-branch",
          "[CodeFold]") {
    // kotlin-folds.scm matches function_body/control_structure_body only
    // with an explicit "{" child -- this grammar's block braces inline into
    // those parents from a hidden rule, so an unguarded capture would put a
    // fold affordance on constructs that have no block at all.
    const auto mode   = KotlinMode();
    const auto blocks = FoldableBlocks(mode, "fun double(n: Int): Int = n * 2\n"
                                             "\n"
                                             "fun sign(n: Int): Int = if (n < 0) -1 else 1\n");
    REQUIRE(blocks.empty());
}

TEST_CASE("A Kotlin expression body is not foldable even when it spans lines", "[CodeFold]") {
    // The case above passes for a reason that does not generalise -- those
    // bodies are single-line, and NormalizeFoldBlocks drops single-line blocks
    // whatever the source said. This one spans four lines and still must not
    // fold: `function_body` is `{ ... }` OR `= expr`, and this instance carries
    // no brackets at all.
    //
    // The deleted kotlin-folds.scm stated it by hand (`(function_body "{")`),
    // which the union could never honour -- a fold source states only
    // positives, so the imprint's unconditional capture won regardless. It is
    // the instance check in Editor/ImprintBracket.h's DelimitersOf that
    // actually answers it.
    const auto mode   = KotlinMode();
    const auto blocks = FoldableBlocks(mode, "fun describe(n: Int): String = if (n < 0)\n"
                                             "    \"negative\"\n"
                                             "else\n"
                                             "    \"non-negative\"\n"
                                             "\n"
                                             "fun twice(n: Int): Int {\n"
                                             "    return n * 2\n"
                                             "}\n");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks.front().first == 105); // the braced body, not the `= if`
}

TEST_CASE("FoldableBlocks returns nothing for a mode with no fold query", "[CodeFold]") {
    const ned::editor::Mode mode   = ned::editor::FundamentalMode();
    const auto              blocks = FoldableBlocks(mode, "anything at all");
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

TEST_CASE("ToggleFoldAtLine picks the outermost block when two start on the same line", "[CodeFold]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    // Both the outer function body and an inner compound statement start on
    // the same line here. Deliberately wrapped across two lines: a block
    // confined to a single line is no longer foldable at all (see
    // "Folding a single-line block cannot hide the line below it"), so a
    // one-line version of this would have nothing to choose between.
    buffer.InsertAtPoint("int main(void) { if (1) {\n    return 0; } }\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() >= 2);
    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));

    // The outermost (largest) block is the one that got marked -- the same
    // block the fold gutter's own one-affordance-per-line rule shows; see
    // ToggleFoldAtLine's doc comment for why this flipped from innermost.
    const auto* outermost = &blocks[0];
    for (const auto& block : blocks) {
        if ((block.second - block.first) > (outermost->second - outermost->first)) {
            outermost = &block;
        }
    }
    REQUIRE(buffer.FoldMarkerAt(outermost->first).has_value());
}

TEST_CASE("Folding a single-line block cannot hide the line below it", "[CodeFold]") {
    // Real bug, found while measuring Tier 0 trait inference against the
    // hand-written fold corpus (Docs/ParsingEngine.md). `(compound_statement)
    // @fold` matches the one-line body of `int f(void) { return 1; }`, and
    // FoldedLineRanges' own [startLine + 1, endLine + 1] then named line 1 --
    // the *next* function's opening line, which has nothing to do with the
    // folded block.
    //
    // Nothing about this needed the new engine to surface; it was reachable
    // with the queries exactly as they stand.
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(void) { return 1; }\nint g(void) {\n    return 2;\n}\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    // Stronger than when this test was written: the single-line block is no
    // longer merely un-toggleable, it never reaches the list. FoldableBlocks
    // enforces the rule for every fold source at once, which is what lets
    // sources compose without each re-deriving it.
    REQUIRE(blocks.size() == 1); // g's multi-line body only

    CHECK_FALSE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    CHECK(FoldedLineRanges(buffer, buffer.Content(), blocks).empty());

    // Its multi-line neighbour still folds normally.
    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 1));
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    // [startLine, endLineExclusive) per FoldedLineRanges' own contract, so
    // this is g's body and closing brace -- lines 2 and 3 -- and nothing of
    // f's, which is the point.
    CHECK(ranges[0].first == 2);
    CHECK(ranges[0].second == 4);
}

TEST_CASE("A stale marker on a block that shrank to one line hides nothing", "[CodeFold]") {
    // The defence in depth: a marker can outlive the shape that justified it
    // if an edit pulls a multi-line block onto one line, and FoldedLineRanges
    // must not start hiding the following line when that happens.
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(void) {\n    return 1;\n}\nint g(void) { return 2; }\n");

    auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    REQUIRE_FALSE(FoldedLineRanges(buffer, buffer.Content(), blocks).empty());

    // Collapse f's body onto one line; the marker's byte offset still matches.
    Buffer shrunk("test.c");
    shrunk.InsertAtPoint("int f(void) { return 1; }\nint g(void) { return 2; }\n");
    const auto shrunkBlocks = FoldableBlocks(mode, shrunk.Text());
    shrunk.SetFoldMarker(shrunkBlocks.empty() ? 0 : shrunkBlocks[0].first,
                         Buffer::FoldMarker::Collapsed);
    CHECK(FoldedLineRanges(shrunk, shrunk.Content(), shrunkBlocks).empty());
}

TEST_CASE("FoldRegionsWithDepth gives disjoint siblings depth 0", "[CodeFold]") {
    // Two independent, non-nested function bodies -- neither contains the
    // other, so both are top-level.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks  = {{10, 20}, {30, 40}};
    const auto                                             regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 2);
    REQUIRE(regions[0].depth == 0);
    REQUIRE(regions[1].depth == 0);
}

TEST_CASE("FoldRegionsWithDepth gives increasing depth for properly nested blocks", "[CodeFold]") {
    // [0,100) contains [10,90) contains [20,80) -- three levels deep.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks  = {{0, 100}, {10, 90}, {20, 80}};
    const auto                                             regions = FoldRegionsWithDepth(blocks);
    REQUIRE(regions.size() == 3);
    REQUIRE(regions[0].depth == 0);
    REQUIRE(regions[1].depth == 1);
    REQUIRE(regions[2].depth == 2);
}

TEST_CASE("FoldRegionsWithDepth resets depth for a sibling after a nested block closes", "[CodeFold]") {
    // [0,10) contains [1,5); [11,20) is a separate top-level sibling after
    // the first one closes.
    const std::vector<std::pair<std::size_t, std::size_t>> blocks  = {{0, 10}, {1, 5}, {11, 20}};
    const auto                                             regions = FoldRegionsWithDepth(blocks);
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

// ---------------------------------------------------------------------------
// Indentation languages: where a fold starts, and what collapsing it hides.
//
// A brace language gets this for free -- its `{` sits on the row that stays
// visible. An indentation body begins one row BELOW the line that names it, so
// every one of these used to fold from its own first statement: the affordance
// sat on the wrong row and collapsing left that statement on screen.
// `imprint::FoldAnchorStart` is what moves the start onto the header row; these
// hold the end-to-end result, which is what a reader actually sees.
// ---------------------------------------------------------------------------

TEST_CASE("A Python body folds from its header line, not from its first statement", "[CodeFold]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def f():\n    x = 1\n    return x\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() == 1);
    CHECK(buffer.Content().ByteOffsetToLine(blocks[0].first) == 0); // the `def f():` row

    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 1); // the whole body, not all-but-its-first-line
    CHECK(ranges[0].second == 3);
}

TEST_CASE("A one-statement Python body folds", "[CodeFold]") {
    // The body is one line, but the construct is two: the "must span more than
    // one line" rule is about what a fold can hide, and here there is a line to
    // hide. Before anchoring, `return 1` was its own start and end and the
    // function was simply unfoldable.
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def f():\n    return 1\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() == 1);
    CHECK(buffer.Content().ByteOffsetToLine(blocks[0].first) == 0);
}

TEST_CASE("A Python class folds as a class and as methods", "[CodeFold]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("class C:\n"
                         "    def m(self):\n"
                         "        return 1\n"
                         "\n"
                         "    def n(self):\n"
                         "        return 2\n");

    const auto               blocks = FoldableBlocks(mode, buffer.Text());
    std::vector<std::size_t> headerLines;
    for (const auto& block : blocks)
        headerLines.push_back(buffer.Content().ByteOffsetToLine(block.first));
    CHECK(headerLines == std::vector<std::size_t>{0, 1, 4}); // class C:, def m, def n
}

TEST_CASE("A blank line between a header and its body does not move the fold", "[CodeFold]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def f():\n\n    x = 1\n    return x\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() == 1);
    CHECK(buffer.Content().ByteOffsetToLine(blocks[0].first) == 0);
}

TEST_CASE("A Python if/else folds its own branch and leaves else standing", "[CodeFold]") {
    // The if-statement and its suite both begin on the `if x:` row once the
    // suite is anchored. NormalizeFoldBlocks keeps the tighter one, so folding
    // collapses the branch rather than the whole statement.
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("if x:\n    a = 1\n    b = 2\nelse:\n    c = 3\n    d = 4\n");

    const auto               blocks = FoldableBlocks(mode, buffer.Text());
    std::vector<std::size_t> headerLines;
    for (const auto& block : blocks)
        headerLines.push_back(buffer.Content().ByteOffsetToLine(block.first));
    CHECK(headerLines == std::vector<std::size_t>{0, 3}); // `if x:` and `else:`

    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 1);
    CHECK(ranges[0].second == 3); // `else:` is still on screen
}

TEST_CASE("A YAML key folds the block it names", "[CodeFold]") {
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("root:\n  a: 1\n  b:\n    - x\n    - y\nother: 2\n");

    const auto               blocks = FoldableBlocks(mode, buffer.Text());
    std::vector<std::size_t> headerLines;
    for (const auto& block : blocks)
        headerLines.push_back(buffer.Content().ByteOffsetToLine(block.first));
    CHECK(headerLines == std::vector<std::size_t>{0, 2}); // `root:` and `  b:`

    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 1);
    CHECK(ranges[0].second == 5); // `other: 2` is not root's to hide
}

TEST_CASE("A TOML table does not hide the next table's header", "[CodeFold]") {
    // `table` runs to wherever the next one starts, so its end byte sits on a
    // line it does not own. Trimming trailing whitespace is what keeps the
    // hides-through-the-end-line convention honest.
    const auto mode = TomlMode();
    Buffer     buffer("test.toml");
    buffer.InsertAtPoint("[table]\nkey = 1\nother = 2\n\n[next]\nk = 2\n");

    const auto blocks = FoldableBlocks(mode, buffer.Text());
    REQUIRE(blocks.size() == 2);
    CHECK(buffer.Content().ByteOffsetToLine(blocks[0].first) == 0);

    REQUIRE(ToggleFoldAtLine(buffer, buffer.Content(), blocks, 0));
    const auto ranges = FoldedLineRanges(buffer, buffer.Content(), blocks);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 1);
    CHECK(ranges[0].second == 3); // through `other = 2`, not through `[next]`
}

TEST_CASE("NormalizeFoldBlocks keeps the innermost block per start byte", "[CodeFold]") {
    const std::string text = "a\nb\nc\nd\n";
    const auto        kept = ned::editor::codefold::NormalizeFoldBlocks({{0, 8}, {0, 4}, {2, 6}}, text);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0] == std::pair<std::size_t, std::size_t>{0, 3}); // trailing newline trimmed
    CHECK(kept[1] == std::pair<std::size_t, std::size_t>{2, 5});
}
