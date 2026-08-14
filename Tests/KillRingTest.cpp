#include <catch2/catch_test_macros.hpp>

#include "Text/KillRing.h"

using ned::text::KillRing;

TEST_CASE("Fresh KillRing is empty", "[KillRing]") {
    KillRing ring;

    REQUIRE(ring.Empty());
    REQUIRE(ring.Current().empty());
}

TEST_CASE("Kill pushes the most recent entry as the yank target", "[KillRing]") {
    KillRing ring;

    ring.Kill("first");
    REQUIRE(ring.Current() == "first");

    ring.Kill("second");
    REQUIRE(ring.Current() == "second");
}

TEST_CASE("YankPop cycles to older entries and wraps around", "[KillRing]") {
    KillRing ring;

    ring.Kill("a");
    ring.Kill("b");
    ring.Kill("c");
    REQUIRE(ring.Current() == "c");

    REQUIRE(ring.YankPop() == "b");
    REQUIRE(ring.YankPop() == "a");
    REQUIRE(ring.YankPop() == "c"); // wraps back around
}

TEST_CASE("A fresh kill resets the yank pointer to the newest entry", "[KillRing]") {
    KillRing ring;

    ring.Kill("a");
    ring.Kill("b");
    (void)ring.YankPop(); // now pointing at "a"
    REQUIRE(ring.Current() == "a");

    ring.Kill("c");
    REQUIRE(ring.Current() == "c");
}

TEST_CASE("KillRing evicts the oldest entry once over capacity", "[KillRing]") {
    KillRing ring(3);

    ring.Kill("1");
    ring.Kill("2");
    ring.Kill("3");
    ring.Kill("4"); // evicts "1"

    REQUIRE(ring.Current() == "4");
    REQUIRE(ring.YankPop() == "3");
    REQUIRE(ring.YankPop() == "2");
    REQUIRE(ring.YankPop() == "4"); // "1" is gone; wraps after only 3 entries
}
