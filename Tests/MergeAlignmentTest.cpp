#include <catch2/catch_test_macros.hpp>

#include "Text/ConflictHunk.h"
#include "Text/MergeAlignment.h"

using ned::text::AlignedChunk;
using ned::text::AlignPanes;
using ned::text::MergeSide;
using ned::text::MergeSideFromMarkers;
using ned::text::MergeSideKind;
using ned::text::PaneAlignment;
using ned::text::ParseConflictHunks;

namespace {

MergeSide Side(const std::string& merged, MergeSideKind kind) {
    const auto hunks = ParseConflictHunks(merged);
    auto       side  = MergeSideFromMarkers(merged, hunks, kind);
    REQUIRE(side.has_value());
    return *side;
}

std::vector<PaneAlignment> Align(std::size_t mergedLines, const std::vector<const MergeSide*>& sides) {
    return AlignPanes(mergedLines, sides);
}

} // namespace

TEST_CASE("A text with no markers derives an identical side with one unchanged chunk", "[MergeAlignment]") {
    const std::string merged = "a\nb\nc\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    REQUIRE(ours.text == merged);
    REQUIRE(ours.lineCount == 3);
    REQUIRE(ours.chunks == std::vector<AlignedChunk>{{0, 3, 0, 3, false}});
}

TEST_CASE("Each side replaces the marker block with its own section as one changed chunk", "[MergeAlignment]") {
    const std::string merged = "before\n<<<<<<< HEAD\nours 1\nours 2\n=======\ntheirs\n>>>>>>> feature\nafter\n";

    const MergeSide ours = Side(merged, MergeSideKind::Ours);
    REQUIRE(ours.text == "before\nours 1\nours 2\nafter\n");
    REQUIRE(ours.lineCount == 4);
    REQUIRE(ours.chunks == std::vector<AlignedChunk>{{0, 1, 0, 1, false}, {1, 6, 1, 2, true}, {7, 1, 3, 1, false}});

    const MergeSide theirs = Side(merged, MergeSideKind::Theirs);
    REQUIRE(theirs.text == "before\ntheirs\nafter\n");
    REQUIRE(theirs.chunks == std::vector<AlignedChunk>{{0, 1, 0, 1, false}, {1, 6, 1, 1, true}, {7, 1, 2, 1, false}});
}

TEST_CASE("Base is derived only when every hunk has a diff3 section", "[MergeAlignment]") {
    const std::string diff3 = "<<<<<<< HEAD\nx\n||||||| base\no\n=======\ny\n>>>>>>> b\n";
    const MergeSide   base  = Side(diff3, MergeSideKind::Base);
    REQUIRE(base.text == "o\n");
    REQUIRE(base.chunks == std::vector<AlignedChunk>{{0, 7, 0, 1, true}});

    const std::string mixed = diff3 + "mid\n<<<<<<< HEAD\np\n=======\nq\n>>>>>>> b\n";
    const auto        hunks = ParseConflictHunks(mixed);
    REQUIRE(hunks.size() == 2);
    REQUIRE_FALSE(MergeSideFromMarkers(mixed, hunks, MergeSideKind::Base).has_value());
    REQUIRE(MergeSideFromMarkers(mixed, hunks, MergeSideKind::Ours).has_value());
}

TEST_CASE("An empty section becomes a changed chunk with no side lines", "[MergeAlignment]") {
    const std::string merged = "a\n<<<<<<< HEAD\n=======\ngone\n>>>>>>> b\nz\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    REQUIRE(ours.text == "a\nz\n");
    REQUIRE(ours.chunks == std::vector<AlignedChunk>{{0, 1, 0, 1, false}, {1, 4, 1, 0, true}, {5, 1, 1, 1, false}});
}

TEST_CASE("A closing marker at EOF without a newline still ends its chunk on that line", "[MergeAlignment]") {
    const std::string merged = "a\n<<<<<<< HEAD\nx\n=======\ny\n>>>>>>> b";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    REQUIRE(ours.text == "a\nx\n");
    REQUIRE(ours.chunks == std::vector<AlignedChunk>{{0, 1, 0, 1, false}, {1, 5, 1, 1, true}});
}

TEST_CASE("Unchanged texts align with no blank rows", "[MergeAlignment]") {
    const std::string merged = "a\nb\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    const auto        panes  = Align(2, {&ours});
    REQUIRE(panes.size() == 2);
    for (const PaneAlignment& pane : panes) {
        REQUIRE(pane.LeadingRows() == 0);
        REQUIRE(pane.RowsAfter(0) == 0);
        REQUIRE(pane.AlignedRow(1) == 1);
        REQUIRE(pane.TotalRows() == 2);
    }
}

TEST_CASE("Sides pad after their hunk so the lines after it share a row", "[MergeAlignment]") {
    // Merged lines: before, <<<, ours 1, ours 2, ===, theirs, >>>, after.
    const std::string merged = "before\n<<<<<<< HEAD\nours 1\nours 2\n=======\ntheirs\n>>>>>>> feature\nafter\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    const MergeSide   theirs = Side(merged, MergeSideKind::Theirs);
    const auto        panes  = Align(8, {&ours, &theirs});

    REQUIRE(panes[0].RowsAfter(6) == 0);
    REQUIRE(panes[1].RowsAfter(2) == 4); // "ours 2"
    REQUIRE(panes[2].RowsAfter(1) == 5); // "theirs"

    // "after" is merged line 7, ours line 3, theirs line 2.
    REQUIRE(panes[0].AlignedRow(7) == 7);
    REQUIRE(panes[1].AlignedRow(3) == 7);
    REQUIRE(panes[2].AlignedRow(2) == 7);
    // Every hunk starts on the same row too.
    REQUIRE(panes[1].AlignedRow(1) == panes[0].AlignedRow(1));
    REQUIRE(panes[2].AlignedRow(1) == panes[0].AlignedRow(1));
}

TEST_CASE("A side with nothing in the hunk pads after the line before it", "[MergeAlignment]") {
    const std::string merged = "a\n<<<<<<< HEAD\n=======\ngone\n>>>>>>> b\nz\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    const auto        panes  = Align(6, {&ours});
    REQUIRE(panes[1].RowsAfter(0) == 4);
    REQUIRE(panes[1].AlignedRow(1) == panes[0].AlignedRow(5));
}

TEST_CASE("A hunk at the top of the file with an empty side pads before line 0", "[MergeAlignment]") {
    const std::string merged = "<<<<<<< HEAD\n=======\ngone\n>>>>>>> b\nz\n";
    const MergeSide   ours   = Side(merged, MergeSideKind::Ours);
    const auto        panes  = Align(5, {&ours});
    REQUIRE(panes[1].LeadingRows() == 4);
    REQUIRE(panes[1].AlignedRow(0) == 4);
    REQUIRE(panes[0].AlignedRow(4) == 4);
}

TEST_CASE("A side taller than the merged block pads the merged pane", "[MergeAlignment]") {
    // Hand-built chunks, the shape a full original file diffed against the
    // merged text produces: merged line 1 was replaced by three side lines.
    const MergeSide side{"a\nx\ny\nw\nz\n", 5, {{0, 1, 0, 1, false}, {1, 1, 1, 3, true}, {2, 1, 4, 1, false}}};
    const auto      panes = Align(3, {&side});
    REQUIRE(panes[0].RowsAfter(1) == 2);
    REQUIRE(panes[1].RowsAfter(3) == 0);
    REQUIRE(panes[0].AlignedRow(2) == panes[1].AlignedRow(4));
}

TEST_CASE("Sides whose chunk boundaries disagree only sync where all of them agree", "[MergeAlignment]") {
    // Merged "a b c d e". Side one replaces b..c with one line; side two
    // inserts two lines before c and changes d.
    const MergeSide one{"a\nB\nd\ne\n", 4, {{0, 1, 0, 1, false}, {1, 2, 1, 1, true}, {3, 2, 2, 2, false}}};
    const MergeSide two{"a\nb\nn1\nn2\nc\nD\ne\n", 7, {{0, 2, 0, 2, false}, {2, 0, 2, 2, true}, {2, 1, 4, 1, false}, {3, 1, 5, 1, true}, {4, 1, 6, 1, false}}};
    const auto      panes = Align(5, {&one, &two});

    // "e" is merged 4, one 3, two 6.
    const std::size_t eRow = panes[0].AlignedRow(4);
    REQUIRE(panes[1].AlignedRow(3) == eRow);
    REQUIRE(panes[2].AlignedRow(6) == eRow);
    // "a" syncs at the top, and nothing needs leading rows.
    for (const PaneAlignment& pane : panes) {
        REQUIRE(pane.LeadingRows() == 0);
        REQUIRE(pane.TotalRows() == eRow + 1);
    }
}

TEST_CASE("LineAtAlignedRow inverts AlignedRow and maps blank rows to the line above", "[MergeAlignment]") {
    const PaneAlignment pane({1, 2, 5, 6}); // one leading row, line 1 has two rows after it
    REQUIRE(pane.LineAtAlignedRow(0) == 0);
    REQUIRE(pane.LineAtAlignedRow(1) == 0);
    REQUIRE(pane.LineAtAlignedRow(2) == 1);
    REQUIRE(pane.LineAtAlignedRow(3) == 1);
    REQUIRE(pane.LineAtAlignedRow(4) == 1);
    REQUIRE(pane.LineAtAlignedRow(5) == 2);
    REQUIRE(pane.LineAtAlignedRow(99) == 2);
    REQUIRE(pane.RowsAfter(1) == 2);
    REQUIRE(pane.RowsAfter(2) == 0);
    REQUIRE(pane.RowsAfter(3) == 0);
    REQUIRE(pane.AlignedRow(99) == 6);
}

TEST_CASE("Empty panes align without rows", "[MergeAlignment]") {
    const MergeSide empty{"", 0, {}};
    const auto      panes = Align(0, {&empty});
    REQUIRE(panes[0].TotalRows() == 0);
    REQUIRE(panes[1].TotalRows() == 0);
    REQUIRE(panes[1].LineAtAlignedRow(3) == 0);
}

TEST_CASE("TopForRow starts partway through the rows after a line", "[MergeAlignment]") {
    // Line 0 at row 0, line 1 at row 1 with three rows after it, line 2 at row 5.
    const PaneAlignment pane({0, 1, 5, 6});
    using Top = PaneAlignment::Top;
    CHECK(pane.TopForRow(0) == Top{.line = 0, .padding = 0});
    CHECK(pane.TopForRow(1) == Top{.line = 1, .padding = 0});
    CHECK(pane.TopForRow(2) == Top{.line = 2, .padding = 3});
    CHECK(pane.TopForRow(4) == Top{.line = 2, .padding = 1});
    CHECK(pane.TopForRow(5) == Top{.line = 2, .padding = 0});
    CHECK(pane.TopForRow(99) == Top{.line = 2, .padding = 0});
    for (std::size_t row = 0; row <= 5; ++row) {
        CHECK(pane.RowAtTop(pane.TopForRow(row)) == row);
    }
}

TEST_CASE("TopForRow inside the leading rows settles on line 0", "[MergeAlignment]") {
    const PaneAlignment pane({3, 4});
    using Top = PaneAlignment::Top;
    CHECK(pane.TopForRow(0) == Top{.line = 0, .padding = 0});
    CHECK(pane.TopForRow(2) == Top{.line = 0, .padding = 0});
    CHECK(pane.RowAtTop(Top{.line = 0, .padding = 0}) == 0);
    CHECK(pane.LineCount() == 1);
    CHECK(PaneAlignment().TopForRow(5) == Top{});
}
