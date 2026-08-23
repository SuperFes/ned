#include <catch2/catch_test_macros.hpp>

#include "Text/ThreeWayMerge.h"

using ned::text::HasConflictMarkers;
using ned::text::MergeResult;
using ned::text::ThreeWayMerge;

TEST_CASE("Identical base/ours/theirs merges to the same content with no conflicts", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nb\nc\n", "a\nb\nc\n");
    REQUIRE(result.mergedText == "a\nb\nc\n");
    REQUIRE(result.conflictCount == 0);
    REQUIRE_FALSE(result.firstConflictOffset.has_value());
}

TEST_CASE("An ours-only change is taken automatically", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nX\nc\n", "a\nb\nc\n");
    REQUIRE(result.mergedText == "a\nX\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("A theirs-only change is taken automatically", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nb\nc\n", "a\nY\nc\n");
    REQUIRE(result.mergedText == "a\nY\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("Both sides making the same change is not a conflict", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nX\nc\n", "a\nX\nc\n");
    REQUIRE(result.mergedText == "a\nX\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("Both sides changing the same line differently is a genuine conflict", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nX\nc\n", "a\nY\nc\n");
    REQUIRE(result.conflictCount == 1);
    REQUIRE(result.firstConflictOffset.has_value());
    REQUIRE(result.mergedText == "a\n<<<<<<< buffer\nX\n=======\nY\n>>>>>>> disk\nc\n");
    REQUIRE(*result.firstConflictOffset == result.mergedText.find("<<<<<<<"));
}

TEST_CASE("Two independent non-overlapping changes merge cleanly", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\nd\ne\n", "X\nb\nc\nd\ne\n", "a\nb\nc\nd\nY\n");
    REQUIRE(result.mergedText == "X\nb\nc\nd\nY\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("Adjacent-but-not-overlapping changes merge cleanly, not a false conflict", "[ThreeWayMerge]") {
    // ours changes line 0 ("a"), theirs changes line 1 ("b") -- touching but
    // not overlapping base ranges.
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "X\nb\nc\n", "a\nY\nc\n");
    REQUIRE(result.mergedText == "X\nY\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("An insertion at the start of the file merges cleanly", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("b\nc\n", "a\nb\nc\n", "b\nc\n");
    REQUIRE(result.mergedText == "a\nb\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("An insertion at the end of the file merges cleanly", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\n", "a\nb\n", "a\nb\nc\n");
    REQUIRE(result.mergedText == "a\nb\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("A deletion on one side merges cleanly", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\n", "a\nc\n", "a\nb\nc\n");
    REQUIRE(result.mergedText == "a\nc\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("An empty base with both sides adding the same content is not a conflict", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("", "a\nb\n", "a\nb\n");
    REQUIRE(result.mergedText == "a\nb\n");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("An empty base with both sides adding different content is a conflict", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("", "a\n", "b\n");
    REQUIRE(result.conflictCount == 1);
    REQUIRE(result.mergedText == "<<<<<<< buffer\na\n=======\nb\n>>>>>>> disk\n");
}

TEST_CASE("Multiple genuine conflicts in one file are all counted", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb\nc\nd\ne\n", "X\nb\nc\nd\nY\n", "Z\nb\nc\nd\nW\n");
    REQUIRE(result.conflictCount == 2);
    REQUIRE(result.firstConflictOffset.has_value());
    REQUIRE(*result.firstConflictOffset == 0); // the very first line is the first conflict
}

TEST_CASE("A file with no trailing newline round-trips through an unrelated change", "[ThreeWayMerge]") {
    const MergeResult result = ThreeWayMerge("a\nb", "a\nX", "a\nb");
    REQUIRE(result.mergedText == "a\nX");
    REQUIRE(result.conflictCount == 0);
}

TEST_CASE("HasConflictMarkers detects a marker at the start of a line only", "[ThreeWayMerge]") {
    REQUIRE(HasConflictMarkers("foo\n<<<<<<< buffer\nbar\n"));
    REQUIRE_FALSE(HasConflictMarkers("foo\nnot at line start <<<<<<< buffer\n"));
    REQUIRE_FALSE(HasConflictMarkers("plain text, no markers at all\n"));
    REQUIRE(HasConflictMarkers("<<<<<<< buffer\n")); // marker as the very first line
}
