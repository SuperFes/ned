#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>

#include "Text/AnchorSet.h"

using ned::text::AnchorId;
using ned::text::AnchorPolicy;
using ned::text::AnchorRange;
using ned::text::AnchorSet;
using ned::text::Gravity;
using ned::text::InsideDelete;

TEST_CASE("A default-constructed AnchorId is never live", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId none;

    REQUIRE_FALSE(none.Valid());
    REQUIRE_FALSE(set.Offset(none).has_value());
    set.Destroy(none); // a no-op, not a crash: teardown needs no liveness check
}

TEST_CASE("An anchor shifts with content inserted before it", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId anchor = set.Create(10);

    set.ApplyEdit(/*offset=*/4, /*oldLength=*/0, /*newLength=*/3);
    REQUIRE(set.Offset(anchor) == 13);

    set.ApplyEdit(/*offset=*/0, /*oldLength=*/2, /*newLength=*/0);
    REQUIRE(set.Offset(anchor) == 11);
}

TEST_CASE("An anchor ignores an edit entirely after it", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId anchor = set.Create(5);

    set.ApplyEdit(/*offset=*/20, /*oldLength=*/0, /*newLength=*/100);
    set.ApplyEdit(/*offset=*/20, /*oldLength=*/50, /*newLength=*/0);
    REQUIRE(set.Offset(anchor) == 5);
}

TEST_CASE("Gravity decides an insert landing exactly on an anchor", "[AnchorSet]") {
    AnchorSet set;
    // Right keeps naming the same byte of content, so it moves past text
    // typed at its position; Left keeps naming the same position.
    const AnchorId right = set.Create(8, AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Clamp});
    const AnchorId left  = set.Create(8, AnchorPolicy{.gravity = Gravity::Left, .insideDelete = InsideDelete::Clamp});

    set.ApplyEdit(/*offset=*/8, /*oldLength=*/0, /*newLength=*/4);
    REQUIRE(set.Offset(right) == 12);
    REQUIRE(set.Offset(left) == 8);
}

TEST_CASE("InsideDelete decides an anchor the deletion swallowed", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId clamped =
        set.Create(12, AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Clamp});
    const AnchorId dropped =
        set.Create(12, AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Invalidate});

    set.ApplyEdit(/*offset=*/10, /*oldLength=*/6, /*newLength=*/0);
    REQUIRE(set.Offset(clamped) == 10);
    REQUIRE_FALSE(set.Offset(dropped).has_value());
    // The invalidated one still holds its slot -- the holder learns on read
    // and is the one that releases it.
    REQUIRE(set.LiveCount() == 1);
}

TEST_CASE("A range grows around text typed at either of its own edges", "[AnchorSet]") {
    AnchorSet         set;
    const AnchorRange range = set.CreateRange(4, 9);

    set.ApplyEdit(/*offset=*/4, /*oldLength=*/0, /*newLength=*/2); // at the start edge
    REQUIRE(set.Range(range) == std::pair<std::size_t, std::size_t>{4, 11});

    set.ApplyEdit(/*offset=*/11, /*oldLength=*/0, /*newLength=*/3); // at the end edge
    REQUIRE(set.Range(range) == std::pair<std::size_t, std::size_t>{4, 14});
}

TEST_CASE("A range with the opposite gravities excludes a boundary insert", "[AnchorSet]") {
    AnchorSet set;
    // The inactive-snippet-field rule: an insert at the seam between two
    // adjacent fields must not be claimed by this one.
    const AnchorRange range =
        set.CreateRange(4, 9, AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Clamp},
                        AnchorPolicy{.gravity = Gravity::Left, .insideDelete = InsideDelete::Clamp});

    set.ApplyEdit(/*offset=*/4, /*oldLength=*/0, /*newLength=*/2);
    REQUIRE(set.Range(range) == std::pair<std::size_t, std::size_t>{6, 11});

    set.ApplyEdit(/*offset=*/11, /*oldLength=*/0, /*newLength=*/3);
    REQUIRE(set.Range(range) == std::pair<std::size_t, std::size_t>{6, 11});
}

TEST_CASE("Half a surviving range is not a range", "[AnchorSet]") {
    AnchorSet         set;
    const AnchorRange range =
        set.CreateRange(4, 9, AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Invalidate},
                        AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Invalidate});

    set.ApplyEdit(/*offset=*/3, /*oldLength=*/3, /*newLength=*/0); // spans the start, not the end
    REQUIRE_FALSE(set.Offset(range.start).has_value());
    REQUIRE(set.Offset(range.end).has_value());
    REQUIRE_FALSE(set.Range(range).has_value());
}

TEST_CASE("A replacement is applied as one op, not a delete plus an insert", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId inside = set.Create(6);

    // [4, 10) became 3 bytes. As one replacement the anchor collapses to the
    // replacement point; as a delete half followed by an insert half it would
    // land at 4 + 3 = 7, past text it never named. The single op is the
    // honest answer, and is what EditJournal replay gives a carried-forward
    // result for the same edit.
    set.ApplyEdit(/*offset=*/4, /*oldLength=*/6, /*newLength=*/3);
    REQUIRE(set.Offset(inside) == 4);
}

TEST_CASE("A barrier drops every anchor", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId first  = set.Create(3);
    const AnchorId second = set.Create(30);
    REQUIRE(set.LiveCount() == 2);

    set.ApplyBarrier();
    REQUIRE_FALSE(set.Offset(first).has_value());
    REQUIRE_FALSE(set.Offset(second).has_value());
    REQUIRE(set.LiveCount() == 0);
}

TEST_CASE("A destroyed anchor's slot is reused without its handle following", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId first = set.Create(3);
    set.Destroy(first);
    REQUIRE(set.LiveCount() == 0);

    const AnchorId second = set.Create(90);
    REQUIRE(second.index == first.index); // the slot really was reused
    REQUIRE(second.version != first.version);

    // The stale handle must not name the new occupant -- that is the whole
    // reason a handle carries a version.
    REQUIRE_FALSE(set.Offset(first).has_value());
    REQUIRE(set.Offset(second) == 90);

    // And destroying through the stale handle must not evict the new one.
    set.Destroy(first);
    REQUIRE(set.Offset(second) == 90);
}

TEST_CASE("An identity edit moves nothing", "[AnchorSet]") {
    AnchorSet      set;
    const AnchorId anchor = set.Create(7);

    set.ApplyEdit(/*offset=*/0, /*oldLength=*/0, /*newLength=*/0);
    REQUIRE(set.Offset(anchor) == 7);
}
