#include <catch2/catch_test_macros.hpp>

#include "Text/ConflictHunk.h"
#include "Text/ThreeWayMerge.h"

using ned::text::ConflictHunk;
using ned::text::ParseConflictHunks;

namespace {
std::string Slice(const std::string& text, ConflictHunk::Range range) {
    return text.substr(range.start, range.end - range.start);
}
} // namespace

TEST_CASE("No markers at all parses to an empty hunk list", "[ConflictHunk]") {
    REQUIRE(ParseConflictHunks("a\nb\nc\n").empty());
    REQUIRE(ParseConflictHunks("").empty());
}

TEST_CASE("A single ours/theirs hunk parses with no base range", "[ConflictHunk]") {
    const std::string text  = "before\n<<<<<<< buffer\nours line\n=======\ntheirs line\n>>>>>>> disk\nafter\n";
    const auto        hunks = ParseConflictHunks(text);
    REQUIRE(hunks.size() == 1);
    const ConflictHunk& hunk = hunks[0];
    REQUIRE(hunk.startByte == text.find("<<<<<<<"));
    REQUIRE(hunk.endByte == text.find("after"));
    REQUIRE_FALSE(hunk.baseRange.has_value());
    REQUIRE(Slice(text, hunk.oursRange) == "ours line\n");
    REQUIRE(Slice(text, hunk.theirsRange) == "theirs line\n");
}

TEST_CASE("A diff3 hunk with a base section parses all three ranges", "[ConflictHunk]") {
    const std::string text =
        "<<<<<<< HEAD\nours line\n||||||| base commit\nbase line\n=======\ntheirs line\n>>>>>>> feature\n";
    const auto hunks = ParseConflictHunks(text);
    REQUIRE(hunks.size() == 1);
    const ConflictHunk& hunk = hunks[0];
    REQUIRE(hunk.baseRange.has_value());
    REQUIRE(Slice(text, hunk.oursRange) == "ours line\n");
    REQUIRE(Slice(text, *hunk.baseRange) == "base line\n");
    REQUIRE(Slice(text, hunk.theirsRange) == "theirs line\n");
}

TEST_CASE("Multiple hunks in one buffer parse independently, in document order", "[ConflictHunk]") {
    const std::string text  = "<<<<<<< a\nx\n=======\ny\n>>>>>>> b\n"
                              "middle\n"
                              "<<<<<<< a\np\n=======\nq\n>>>>>>> b\n";
    const auto        hunks = ParseConflictHunks(text);
    REQUIRE(hunks.size() == 2);
    REQUIRE(Slice(text, hunks[0].oursRange) == "x\n");
    REQUIRE(Slice(text, hunks[1].oursRange) == "p\n");
    REQUIRE(hunks[0].endByte <= hunks[1].startByte);
}

TEST_CASE("An unterminated start marker is dropped, not thrown", "[ConflictHunk]") {
    const std::string text = "<<<<<<< dangling\nno separator or end here\n";
    REQUIRE(ParseConflictHunks(text).empty());
}

TEST_CASE("A nested start marker abandons the outer one and resumes at the fresh marker", "[ConflictHunk]") {
    const std::string text  = "<<<<<<< outer\nstuff\n<<<<<<< inner\nx\n=======\ny\n>>>>>>> z\n";
    const auto        hunks = ParseConflictHunks(text);
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0].startByte == text.find("<<<<<<< inner"));
}

TEST_CASE("An empty ours or theirs side parses as a zero-length range", "[ConflictHunk]") {
    const std::string text  = "<<<<<<< a\n=======\nonly theirs\n>>>>>>> b\n";
    const auto        hunks = ParseConflictHunks(text);
    REQUIRE(hunks.size() == 1);
    REQUIRE(Slice(text, hunks[0].oursRange).empty());
    REQUIRE(Slice(text, hunks[0].theirsRange) == "only theirs\n");
}

TEST_CASE("ThreeWayMerge's own conflict output round-trips through ParseConflictHunks", "[ConflictHunk]") {
    const ned::text::MergeResult result = ned::text::ThreeWayMerge("a\nb\nc\n", "a\nX\nc\n", "a\nY\nc\n");
    REQUIRE(result.conflictCount == 1);
    const auto hunks = ParseConflictHunks(result.mergedText);
    REQUIRE(hunks.size() == 1);
    REQUIRE_FALSE(hunks[0].baseRange.has_value());
    REQUIRE(Slice(result.mergedText, hunks[0].oursRange) == "X\n");
    REQUIRE(Slice(result.mergedText, hunks[0].theirsRange) == "Y\n");
}
