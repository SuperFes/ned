#include <catch2/catch_test_macros.hpp>

#include "Editor/DabbrevComplete.h"

using ned::editor::CollectDabbrevCandidates;

TEST_CASE("Finds a longer word sharing the prefix", "[DabbrevComplete]") {
    const std::string content = "let counter = 0;\ncounter += 1;";
    const auto         result  = CollectDabbrevCandidates(content, /*point=*/4, "coun");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "counter");
}

TEST_CASE("An empty prefix yields no candidates", "[DabbrevComplete]") {
    const std::string content = "counter counter counter";
    CHECK(CollectDabbrevCandidates(content, 0, "").empty());
}

TEST_CASE("Matches occurrences both before and after point, nearest first on each side", "[DabbrevComplete]") {
    // "far_before" ... "far_close_before" <point> "far_close_after" ... "far_after"
    const std::string content = "far_before x far_close_before far_close_after y far_after";
    const std::size_t point   = content.find(" far_close_after"); // just before the "after" word starts

    const auto result = CollectDabbrevCandidates(content, point, "far_");
    REQUIRE(result.size() == 4);
    CHECK(result[0] == "far_close_before"); // nearest before point
    CHECK(result[1] == "far_before");       // farther before point
    CHECK(result[2] == "far_close_after");  // nearest after point
    CHECK(result[3] == "far_after");        // farther after point
}

TEST_CASE("Exact-length matches (nothing left to suggest) are excluded", "[DabbrevComplete]") {
    const std::string content = "foo foobar";
    // "foo" itself appears again verbatim -- shouldn't be offered as its own completion.
    const auto result = CollectDabbrevCandidates(content, content.size(), "foo");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "foobar");
}

TEST_CASE("Matching is case-sensitive and ASCII-word-boundary aware", "[DabbrevComplete]") {
    const std::string content = "Counter counter_value CounterExtra";
    const auto         result  = CollectDabbrevCandidates(content, content.size(), "counter");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "counter_value"); // "Counter"/"CounterExtra" don't match the lowercase prefix
}

TEST_CASE("Duplicate matches are deduplicated", "[DabbrevComplete]") {
    const std::string content = "counter1 counter1 counter1";
    const auto         result  = CollectDabbrevCandidates(content, content.size(), "counter");
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "counter1");
}

TEST_CASE("Result count is capped at maxCandidates", "[DabbrevComplete]") {
    const std::string content = "counter1 counter2 counter3 counter4";
    const auto         result  = CollectDabbrevCandidates(content, content.size(), "counter", /*maxCandidates=*/2);
    CHECK(result.size() == 2);
}

TEST_CASE("No matches yields an empty result", "[DabbrevComplete]") {
    CHECK(CollectDabbrevCandidates("nothing relevant here", 5, "xyz").empty());
}
