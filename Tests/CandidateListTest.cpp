#include <catch2/catch_test_macros.hpp>

#include "UI/BufferView/CandidateList.h"

using ned::ui::bufferview::CandidateList;

namespace {
std::vector<std::string> Fruit() {
    return {"apple", "apricot", "banana", "cherry"};
}
} // namespace

TEST_CASE("Reset ranks the pool and selects the top", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());

    REQUIRE(list.Size() == 4);
    REQUIRE(list.Selection() == 0);
    REQUIRE(list.Selected() == "apple");
}

TEST_CASE("Filtering narrows to subsequence matches", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit(), "ap");

    REQUIRE_FALSE(list.Empty());
    for (const std::string& entry : list.Ranked()) {
        REQUIRE(entry.find('a') != std::string::npos);
    }
    REQUIRE(list.Ranked().size() == 2);
}

TEST_CASE("Arrow movement wraps at both ends", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());

    list.SelectPrevious();
    REQUIRE(list.Selection() == 3); // wrapped backwards off the top

    list.SelectNext();
    REQUIRE(list.Selection() == 0); // and forwards off the bottom
}

TEST_CASE("A narrowing refilter clamps the selection instead of leaving it stale", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());
    list.SelectPrevious(); // last entry, index 3

    list.Refilter("ap"); // only two survive

    // The trap: index 3 would read past the end of the narrowed list.
    REQUIRE(list.Size() == 2);
    REQUIRE(list.Selection() < list.Size());
    REQUIRE(list.Selected() == list.Ranked()[list.Selection()]);
}

TEST_CASE("Refilter keeps the selection when the list still holds it", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit(), "a");
    list.SelectNext();
    const std::string chosen = list.Selected();

    list.Refilter("a");

    REQUIRE(list.Selected() == chosen);
}

TEST_CASE("Reset returns to the top even when the previous index would fit", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());
    list.SelectNext();
    REQUIRE(list.Selection() == 1);

    // A different pool entirely -- index 1 means something unrelated now.
    list.Reset({"one", "two", "three"});

    REQUIRE(list.Selection() == 0);
    REQUIRE(list.Selected() == "one");
}

TEST_CASE("An empty result is safe to read from", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit(), "zzzz");

    REQUIRE(list.Empty());
    REQUIRE(list.Selection() == 0);
    REQUIRE(list.Selected().empty());
    list.SelectNext();     // must not divide by zero
    list.SelectPrevious(); // nor this
    REQUIRE(list.Selection() == 0);
}

TEST_CASE("SelectIndex ignores a row that is not there", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());
    list.SelectIndex(2);
    REQUIRE(list.Selection() == 2);

    // A click landing past the end should do nothing, not clamp to the last row.
    list.SelectIndex(99);
    REQUIRE(list.Selection() == 2);
}

TEST_CASE("Refilter with a fresh pool keeps the selection, unlike Reset", "[CandidateList]") {
    CandidateList list;
    list.Reset(Fruit());
    list.SelectNext();

    // Recomputed-per-keystroke pools (the command registry, the buffer list) are
    // nominally the same list, so the selection should survive.
    list.Refilter(Fruit(), "");

    REQUIRE(list.Selection() == 1);
}

TEST_CASE("Pinned entries stay at the top instead of being ranked among the rest", "[CandidateList]") {
    CandidateList list;
    // The theme picker's shape: two action rows, then the real names. With an
    // empty query every candidate scores the same, so FuzzyFilterAndRank's
    // alphabetical tie-break would otherwise sort "Current theme" between
    // "Catppuccin Mocha" and "Dark" -- which is exactly what proper-casing the
    // theme names caused before pinning existed.
    list.Reset({"Current theme", "None (detect)", "Catppuccin Mocha", "Dark", "Apple"}, "", 2);

    REQUIRE(list.Ranked().size() == 5);
    REQUIRE(list.Ranked()[0] == "Current theme");
    REQUIRE(list.Ranked()[1] == "None (detect)");
    // Everything below is ranked normally -- alphabetically, for an empty query.
    REQUIRE(list.Ranked()[2] == "Apple");
    REQUIRE(list.Selected() == "Current theme");
}

TEST_CASE("A pinned entry is still filtered out when it does not match", "[CandidateList]") {
    CandidateList list;
    list.Reset({"Current theme", "None (detect)", "Gruvbox Dark", "Gruvbox Light"}, "", 2);

    // Pinned means "never competes for position", not "always shown": typing a
    // real theme's name should leave the action rows behind.
    list.Refilter("Gruvbox");
    REQUIRE(list.Ranked().size() == 2);
    REQUIRE(list.Ranked()[0] == "Gruvbox Dark");

    // ...and a query that does match one brings it back to the top.
    list.Refilter("Cur");
    REQUIRE(list.Ranked().front() == "Current theme");
}

TEST_CASE("Pinning survives a recomputed pool and cannot point past its end", "[CandidateList]") {
    CandidateList list;
    list.Reset({"Current theme", "None (detect)", "Dark"}, "", 2);

    list.Refilter(std::vector<std::string>{"Only one"}, "");
    REQUIRE(list.Ranked() == std::vector<std::string>{"Only one"});
}
