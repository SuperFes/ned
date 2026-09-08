#include <catch2/catch_test_macros.hpp>

#include "Text/Buffer.h"
#include "UI/BufferView/CacheStamp.h"

using ned::text::Buffer;
using ned::ui::bufferview::CacheStamp;

TEST_CASE("A default-constructed stamp matches nothing, itself included", "[CacheStamp]") {
    Buffer buffer("scratch");

    const CacheStamp never;
    const CacheStamp current = CacheStamp::For(&buffer, {0});

    REQUIRE_FALSE(never.Matches(current));
    REQUIRE_FALSE(current.Matches(never));
    // Two never-computed caches are not interchangeable -- each still has to be
    // built once, so an empty stamp must not match another empty one either.
    REQUIRE_FALSE(never.Matches(CacheStamp{}));
}

TEST_CASE("A stamp matches one built from the same buffer and values", "[CacheStamp]") {
    Buffer buffer("scratch");

    const CacheStamp stored = CacheStamp::For(&buffer, {7, 9});

    REQUIRE(stored.Matches(CacheStamp::For(&buffer, {7, 9})));
}

TEST_CASE("Any differing value invalidates the match", "[CacheStamp]") {
    Buffer buffer("scratch");

    const CacheStamp stored = CacheStamp::For(&buffer, {7, 9});

    REQUIRE_FALSE(stored.Matches(CacheStamp::For(&buffer, {8, 9})));
    REQUIRE_FALSE(stored.Matches(CacheStamp::For(&buffer, {7, 10})));
}

TEST_CASE("A different buffer never matches, even at identical generations", "[CacheStamp]") {
    Buffer first("first");
    Buffer second("second");

    // The trap this guards: two buffers freshly opened both sit at generation 0,
    // so comparing generations alone would hand one buffer's derived lines to
    // the other.
    const CacheStamp stored = CacheStamp::For(&first, {0});

    REQUIRE_FALSE(stored.Matches(CacheStamp::For(&second, {0})));
}

TEST_CASE("Value count is part of the key", "[CacheStamp]") {
    Buffer buffer("scratch");

    // A windowed cache that dropped its window from the key would otherwise
    // still look current after the window moved.
    const CacheStamp windowed = CacheStamp::For(&buffer, {3, 0, 4096});

    REQUIRE_FALSE(windowed.Matches(CacheStamp::For(&buffer, {3, 0})));
    REQUIRE(windowed.Matches(CacheStamp::For(&buffer, {3, 0, 4096})));
}

TEST_CASE("Invalidate forces the next comparison to miss", "[CacheStamp]") {
    Buffer buffer("scratch");

    CacheStamp       stored  = CacheStamp::For(&buffer, {2});
    const CacheStamp current = CacheStamp::For(&buffer, {2});
    REQUIRE(stored.Matches(current));

    stored.Invalidate();

    REQUIRE_FALSE(stored.Matches(current));
}

TEST_CASE("IsFor reports the buffer a stamp was built from", "[CacheStamp]") {
    Buffer first("first");
    Buffer second("second");

    const CacheStamp stored = CacheStamp::For(&first, {1});

    REQUIRE(stored.IsFor(&first));
    REQUIRE_FALSE(stored.IsFor(&second));
    REQUIRE_FALSE(stored.IsFor(nullptr));
    REQUIRE_FALSE(CacheStamp{}.IsFor(&first));
}

TEST_CASE("A stamp survives being copied, which is how caches store theirs", "[CacheStamp]") {
    Buffer buffer("scratch");

    const CacheStamp built  = CacheStamp::For(&buffer, {5, 6, 7, 8});
    CacheStamp       stored = built;

    REQUIRE(stored.Matches(built));
}
