#include <catch2/catch_test_macros.hpp>

#include "Editor/FuzzyMatch.h"

using ned::editor::FuzzyFilterAndRank;
using ned::editor::FuzzyScore;

TEST_CASE("An exact match scores at least as high as any other match", "[FuzzyMatch]") {
    const auto exact   = FuzzyScore("find-file", "find-file");
    const auto partial = FuzzyScore("find-file", "ff");
    REQUIRE(exact.has_value());
    REQUIRE(partial.has_value());
    REQUIRE(*exact > *partial);
}

TEST_CASE("A non-subsequence query returns nullopt", "[FuzzyMatch]") {
    REQUIRE_FALSE(FuzzyScore("find-file", "xyz").has_value());
    REQUIRE_FALSE(FuzzyScore("find-file", "ffz").has_value());
}

TEST_CASE("A word-boundary-start match outranks an incidental interior match", "[FuzzyMatch]") {
    // "stb" matches switch-to-buffer at each word start (s-witch, t-o, b-uffer);
    // against a candidate where the same letters only occur as an incidental
    // interior subsequence, the word-boundary candidate should score higher.
    const auto wordStart = FuzzyScore("switch-to-buffer", "stb");
    const auto interior  = FuzzyScore("substitute-blob", "stb");
    REQUIRE(wordStart.has_value());
    REQUIRE(interior.has_value());
    REQUIRE(*wordStart > *interior);
}

TEST_CASE("A consecutive run of matched characters scores higher than a scattered match", "[FuzzyMatch]") {
    const auto consecutive = FuzzyScore("find-file", "fin");       // "fin" is a consecutive prefix run
    const auto scattered   = FuzzyScore("forward-in-line", "fin"); // same 3 letters, spread apart
    REQUIRE(consecutive.has_value());
    REQUIRE(scattered.has_value());
    REQUIRE(*consecutive > *scattered);
}

TEST_CASE("Matching is case-insensitive", "[FuzzyMatch]") {
    REQUIRE(FuzzyScore("Find-File", "ff") == FuzzyScore("find-file", "FF"));
}

TEST_CASE("An empty query matches every candidate with score 0", "[FuzzyMatch]") {
    REQUIRE(FuzzyScore("find-file", "") == 0);
    REQUIRE(FuzzyScore("", "") == 0);
}

TEST_CASE("FuzzyFilterAndRank with an empty query returns every candidate, sorted alphabetically", "[FuzzyMatch]") {
    const std::vector<std::string> candidates = {"switch-to-buffer", "find-file", "delete-window"};
    const std::vector<std::string> ranked     = FuzzyFilterAndRank(candidates, "");
    REQUIRE(ranked == std::vector<std::string>{"delete-window", "find-file", "switch-to-buffer"});
}

TEST_CASE("FuzzyFilterAndRank excludes non-matching candidates entirely", "[FuzzyMatch]") {
    const std::vector<std::string> candidates = {"find-file", "quit", "save-buffer"};
    const std::vector<std::string> ranked     = FuzzyFilterAndRank(candidates, "fi");
    REQUIRE(ranked == std::vector<std::string>{"find-file"});
}

TEST_CASE("FuzzyFilterAndRank breaks equal scores alphabetically", "[FuzzyMatch]") {
    // "abc" and "abd" both match query "ab" identically (same word-start +
    // consecutive-run shape), so they tie on score and must fall back to
    // alphabetical order.
    const std::vector<std::string> candidates = {"abd", "abc"};
    const std::vector<std::string> ranked     = FuzzyFilterAndRank(candidates, "ab");
    REQUIRE(ranked == std::vector<std::string>{"abc", "abd"});
}

TEST_CASE("FuzzyFilterAndRank ranks a tight word-boundary match above a loose scattered one", "[FuzzyMatch]") {
    // Both candidates match query "ab" as a subsequence, but "a-b-c" matches
    // with both letters at word-boundary starts and a short gap, while
    // "xaxbxc" matches with neither at a boundary and a longer gap -- the
    // former should rank first.
    const std::vector<std::string> candidates = {"xaxbxc", "a-b-c"};
    const std::vector<std::string> ranked     = FuzzyFilterAndRank(candidates, "ab");
    REQUIRE(ranked == std::vector<std::string>{"a-b-c", "xaxbxc"});
}
