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

// multi-cursor-kill-ring follow-up.

TEST_CASE("Kill is equivalent to KillPieces with a single piece", "[KillRing]") {
    KillRing ring;

    ring.Kill("solo");
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"solo"});
    REQUIRE(ring.Current() == "solo");
}

TEST_CASE("KillPieces stores multiple pieces as one entry, joined by newlines", "[KillRing]") {
    KillRing ring;

    ring.KillPieces({"foo", "bar", "baz"});
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"foo", "bar", "baz"});
    REQUIRE(ring.Current() == "foo\nbar\nbaz");
}

TEST_CASE("YankPop cycling preserves each entry's own piece shape", "[KillRing]") {
    KillRing ring;

    ring.Kill("single");
    ring.KillPieces({"a", "b"});
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"a", "b"});

    (void)ring.YankPop();
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"single"});
    REQUIRE(ring.Current() == "single");

    (void)ring.YankPop(); // wraps back around
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"a", "b"});
}

// Emacs-keymap-round-2 follow-up (kill-append).

TEST_CASE("AppendToCurrent grows the most recent single-piece entry", "[KillRing]") {
    KillRing ring;

    ring.Kill("foo");
    ring.AppendToCurrent("bar", /*prepend=*/false);
    REQUIRE(ring.Current() == "foobar");
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"foobar"});
}

TEST_CASE("AppendToCurrent with prepend inserts before the existing entry", "[KillRing]") {
    KillRing ring;

    ring.Kill("bar");
    ring.AppendToCurrent("foo", /*prepend=*/true);
    REQUIRE(ring.Current() == "foobar");
}

TEST_CASE("AppendToCurrent on an empty ring behaves like Kill", "[KillRing]") {
    KillRing ring;

    ring.AppendToCurrent("first", /*prepend=*/false);
    REQUIRE(ring.Current() == "first");
    REQUIRE(!ring.Empty());
}

TEST_CASE("AppendToCurrent against a multi-piece entry pushes a fresh entry instead", "[KillRing]") {
    KillRing ring;

    ring.KillPieces({"a", "b"});
    ring.AppendToCurrent("solo", /*prepend=*/false);
    REQUIRE(ring.CurrentPieces() == std::vector<std::string>{"solo"});
    REQUIRE(ring.YankPop() == "a\nb"); // the multi-piece entry is still there, just no longer newest
}

TEST_CASE("AppendToCurrent resets the yank pointer to the extended entry", "[KillRing]") {
    KillRing ring;

    ring.Kill("a");
    ring.Kill("b");
    (void)ring.YankPop(); // now pointing at "a"

    ring.AppendToCurrent("c", /*prepend=*/false);
    REQUIRE(ring.Current() == "bc"); // append targets the ring's front ("b"), not the yank-cycled entry
}
