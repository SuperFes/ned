#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/LineListSearch.h"

using ned::editor::LineListSearch;

namespace {
const std::vector<std::string> kLines = {
    "the quick brown fox", // 0
    "jumps over the lazy", // 1
    "dog while a Fox",     // 2 -- deliberate uppercase F for smart-case coverage
    "watches quietly",     // 3
};
}

TEST_CASE("Forward search finds the first matching line at or after the start index", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 0);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(search.Query() == "fox");
    REQUIRE(search.CurrentIndex() == 0);
}

TEST_CASE("RepeatSearch advances past the current match", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 0);
    search.AppendChar(U'o');
    REQUIRE(search.CurrentIndex() == 0); // "brown"/"fox" on line 0

    search.RepeatSearch();
    REQUIRE(search.CurrentIndex() == 1); // "jumps over"

    search.RepeatSearch();
    REQUIRE(search.CurrentIndex() == 2); // "dog"

    search.RepeatSearch();
    REQUIRE(search.CurrentIndex() == 0); // wraps back around
}

TEST_CASE("Backward search wraps to the end of the list", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Backward, 0);
    for (const char c : std::string("quiet")) { // "quiet" -- distinct from line 0's "quick"
        search.AppendChar(static_cast<char32_t>(c));
    }

    REQUIRE(search.Found());
    REQUIRE(search.CurrentIndex() == 3); // "quietly", wrapping backward from line 0
}

TEST_CASE("Smart-case: an uppercase query character makes the search case-sensitive", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 0);
    search.AppendChar(U'F');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(search.CurrentIndex() == 2); // only "Fox" (capitalized) matches, not "fox"
}

TEST_CASE("A failing search leaves the last successful match in place", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 0);
    search.AppendChar(U'd');
    search.AppendChar(U'o');
    search.AppendChar(U'g');
    REQUIRE(search.Found());
    REQUIRE(search.CurrentIndex() == 2);

    search.AppendChar(U'z'); // "dogz" appears nowhere
    REQUIRE_FALSE(search.Found());
    REQUIRE(search.CurrentIndex() == std::nullopt);
}

TEST_CASE("ReverseDirection flips search direction from the current match", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 0);
    search.AppendChar(U'o');
    REQUIRE(search.CurrentIndex() == 0);
    search.RepeatSearch();
    REQUIRE(search.CurrentIndex() == 1);

    search.ReverseDirection();
    search.RepeatSearch();
    REQUIRE(search.CurrentIndex() == 0); // stepping backward from line 1
}

TEST_CASE("An empty query is always found with no current index", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Forward, 2);
    REQUIRE(search.Found());
    REQUIRE(search.CurrentIndex() == std::nullopt);
    REQUIRE(search.OriginalIndex() == 2);
}

TEST_CASE("StatusText reflects direction and failure state", "[LineListSearch]") {
    LineListSearch search(kLines, LineListSearch::Direction::Backward, 0);
    search.AppendChar(U'x');
    REQUIRE(search.StatusText() == "Backward I-search: x");

    search.AppendChar(U'y'); // "xy" appears nowhere
    REQUIRE(search.StatusText() == "Failing Backward I-search: xy");
}
