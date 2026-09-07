#include <catch2/catch_test_macros.hpp>

#include "Text/LineDiff.h"

using ned::text::DiffLine;
using ned::text::DiffLineKind;
using ned::text::DiffLines;
using ned::text::LineDiffHunk;
using ned::text::SplitLines;
using ned::text::UnifiedDiff;

TEST_CASE("SplitLines keeps each line's own trailing newline", "[LineDiff]") {
    const auto lines = SplitLines("a\nb\nc\n");
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == "a\n");
    REQUIRE(lines[1] == "b\n");
    REQUIRE(lines[2] == "c\n");
}

TEST_CASE("SplitLines' last piece has no trailing newline when the text doesn't end with one", "[LineDiff]") {
    const auto lines = SplitLines("a\nb");
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "a\n");
    REQUIRE(lines[1] == "b");
}

TEST_CASE("SplitLines on empty text returns no lines", "[LineDiff]") {
    REQUIRE(SplitLines("").empty());
}

TEST_CASE("DiffLines reports no hunks for identical input", "[LineDiff]") {
    const auto a = SplitLines("a\nb\nc\n");
    REQUIRE(DiffLines(a, a).empty());
}

TEST_CASE("DiffLines finds a single-line replacement hunk between matching prefix/suffix", "[LineDiff]") {
    const auto a     = SplitLines("a\nb\nc\n");
    const auto b     = SplitLines("a\nX\nc\n");
    const auto hunks = DiffLines(a, b);
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0] == LineDiffHunk{1, 1, 1, 1});
}

TEST_CASE("DiffLines reports a pure insertion as a zero-width hunk on the a side", "[LineDiff]") {
    const auto a     = SplitLines("a\nc\n");
    const auto b     = SplitLines("a\nb\nc\n");
    const auto hunks = DiffLines(a, b);
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0] == LineDiffHunk{1, 0, 1, 1});
}

TEST_CASE("DiffLines reports a pure deletion as a zero-width hunk on the b side", "[LineDiff]") {
    const auto a     = SplitLines("a\nb\nc\n");
    const auto b     = SplitLines("a\nc\n");
    const auto hunks = DiffLines(a, b);
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0] == LineDiffHunk{1, 1, 1, 0});
}

TEST_CASE("DiffLines reports two independent hunks for two separated changes", "[LineDiff]") {
    const auto a     = SplitLines("a\nb\nc\nd\ne\n");
    const auto b     = SplitLines("A\nb\nc\nD\ne\n");
    const auto hunks = DiffLines(a, b);
    REQUIRE(hunks.size() == 2);
    REQUIRE(hunks[0] == LineDiffHunk{0, 1, 0, 1});
    REQUIRE(hunks[1] == LineDiffHunk{3, 1, 3, 1});
}

TEST_CASE("UnifiedDiff returns nothing for line-for-line identical text", "[LineDiff]") {
    REQUIRE(UnifiedDiff("a\nb\nc\n", "a\nb\nc\n").empty());
}

TEST_CASE("UnifiedDiff surrounds a single changed line with context on both sides", "[LineDiff]") {
    const auto lines = UnifiedDiff("a\nb\nc\nd\ne\n", "a\nb\nX\nd\ne\n", /*contextLines=*/2);
    REQUIRE(lines.size() == 6);
    REQUIRE(lines[0] == DiffLine{DiffLineKind::Context, "a"});
    REQUIRE(lines[1] == DiffLine{DiffLineKind::Context, "b"});
    REQUIRE(lines[2] == DiffLine{DiffLineKind::Removed, "c"});
    REQUIRE(lines[3] == DiffLine{DiffLineKind::Added, "X"});
    REQUIRE(lines[4] == DiffLine{DiffLineKind::Context, "d"});
    REQUIRE(lines[5] == DiffLine{DiffLineKind::Context, "e"});
}

TEST_CASE("UnifiedDiff collapses a long unchanged run into a single Omitted marker", "[LineDiff]") {
    std::string oldText = "change-a\n";
    for (int i = 0; i < 20; ++i) {
        oldText += "same" + std::to_string(i) + "\n";
    }
    oldText += "change-b\n";
    std::string newText = "changed-a\n" + oldText.substr(oldText.find('\n') + 1);
    newText             = "changed-a\n";
    for (int i = 0; i < 20; ++i) {
        newText += "same" + std::to_string(i) + "\n";
    }
    newText += "changed-b\n";

    const auto lines = UnifiedDiff(oldText, newText, /*contextLines=*/2);
    // change-a/changed-a, 2 lines of context, an Omitted marker, 2 more lines
    // of context, then change-b/changed-b.
    bool sawOmitted = false;
    for (const DiffLine& line : lines) {
        if (line.kind == DiffLineKind::Omitted) {
            sawOmitted = true;
            REQUIRE(line.text.find("16") != std::string::npos); // 20 identical lines minus 2+2 shown as context
        }
    }
    REQUIRE(sawOmitted);
    REQUIRE(lines.front() == DiffLine{DiffLineKind::Removed, "change-a"});
    REQUIRE(lines[1] == DiffLine{DiffLineKind::Added, "changed-a"});
}

TEST_CASE("UnifiedDiff merges two hunks whose context windows overlap into one continuous run", "[LineDiff]") {
    // Two single-line changes only one line apart -- with contextLines=2 the
    // windows overlap, so the connecting line between them is shown as plain
    // context rather than triggering two separate Omitted-bounded groups.
    const auto lines = UnifiedDiff("a\nb\nc\nd\ne\n", "X\nb\nc\nY\ne\n", /*contextLines=*/2);
    bool sawOmitted = false;
    for (const DiffLine& line : lines) {
        if (line.kind == DiffLineKind::Omitted) {
            sawOmitted = true;
        }
    }
    REQUIRE_FALSE(sawOmitted);
    REQUIRE(lines[0] == DiffLine{DiffLineKind::Removed, "a"});
    REQUIRE(lines[1] == DiffLine{DiffLineKind::Added, "X"});
    REQUIRE(lines[2] == DiffLine{DiffLineKind::Context, "b"});
    REQUIRE(lines[3] == DiffLine{DiffLineKind::Context, "c"});
    REQUIRE(lines[4] == DiffLine{DiffLineKind::Removed, "d"});
    REQUIRE(lines[5] == DiffLine{DiffLineKind::Added, "Y"});
    REQUIRE(lines[6] == DiffLine{DiffLineKind::Context, "e"});
}

TEST_CASE("UnifiedDiff strips the trailing newline from each rendered line", "[LineDiff]") {
    const auto lines = UnifiedDiff("a\n", "b\n");
    REQUIRE(lines[0].text.find('\n') == std::string::npos);
    REQUIRE(lines[1].text.find('\n') == std::string::npos);
}

TEST_CASE("UnifiedDiff handles a changed final line with no trailing newline", "[LineDiff]") {
    const auto lines = UnifiedDiff("a\nb", "a\nc");
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == DiffLine{DiffLineKind::Context, "a"});
    REQUIRE(lines[1] == DiffLine{DiffLineKind::Removed, "b"});
    REQUIRE(lines[2] == DiffLine{DiffLineKind::Added, "c"});
}
