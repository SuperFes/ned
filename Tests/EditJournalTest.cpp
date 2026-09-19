#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "Text/EditJournal.h"

using ned::text::AnchorPolicy;
using ned::text::EditJournal;
using ned::text::EditOp;
using ned::text::Gravity;
using ned::text::InsideDelete;
using ned::text::RelocateThrough;
using ned::text::RelocateThroughAll;

namespace {

constexpr AnchorPolicy kHint{.gravity = Gravity::Right, .insideDelete = InsideDelete::Invalidate};
constexpr AnchorPolicy kRangeEnd{.gravity = Gravity::Left, .insideDelete = InsideDelete::Clamp};

} // namespace

TEST_CASE("An offset strictly before an edit is untouched", "[EditJournal]") {
    const EditOp insert = EditOp::Inserted(1, 10, 5);
    REQUIRE(RelocateThrough(0, insert, kHint) == 0);
    REQUIRE(RelocateThrough(9, insert, kHint) == 9);

    const EditOp erase = EditOp::Deleted(1, 10, 15);
    REQUIRE(RelocateThrough(9, erase, kHint) == 9);
}

TEST_CASE("An offset after an edit shifts by its length delta", "[EditJournal]") {
    REQUIRE(RelocateThrough(20, EditOp::Inserted(1, 10, 5), kHint) == 25);
    REQUIRE(RelocateThrough(20, EditOp::Deleted(1, 10, 15), kHint) == 15);
    REQUIRE(RelocateThrough(20, EditOp::Replaced(1, 10, 5, 2), kHint) == 17);
}

// The boundary the whole Gravity distinction exists for, and the one the
// snapshot-diff path got wrong: an inlay hint anchored exactly where the user
// types has to follow the text it annotates, or it renders to the left of the
// characters just typed -- inside the identifier they extended.
TEST_CASE("Gravity decides an insertion landing exactly on the offset", "[EditJournal]") {
    const EditOp insert = EditOp::Inserted(1, 10, 2);
    REQUIRE(RelocateThrough(10, insert, kHint) == 12);
    REQUIRE(RelocateThrough(10, insert, kRangeEnd) == 10);
}

TEST_CASE("An offset inside a deletion clamps or invalidates by policy", "[EditJournal]") {
    const EditOp erase = EditOp::Deleted(1, 10, 20);
    REQUIRE(RelocateThrough(15, erase, kRangeEnd) == 10);
    REQUIRE_FALSE(RelocateThrough(15, erase, kHint).has_value());

    // The deletion's own first byte is the boundary, not the inside: an
    // anchor there names a position that still exists after the delete.
    REQUIRE(RelocateThrough(10, erase, kHint) == 10);
    // Its last byte is inside, though -- [10,20) is half-open.
    REQUIRE_FALSE(RelocateThrough(19, erase, kHint).has_value());
    REQUIRE(RelocateThrough(20, erase, kHint) == 10);
}

TEST_CASE("Nothing survives a barrier", "[EditJournal]") {
    REQUIRE_FALSE(RelocateThrough(0, EditOp::Barrier(1), kRangeEnd).has_value());
    REQUIRE_FALSE(RelocateThrough(1000, EditOp::Barrier(1), kHint).has_value());
}

// The failure the snapshot diff could not express: two edits either side of
// an offset are one contiguous changed region to a diff, so the offset looks
// like it sits inside the change when neither edit came near it.
TEST_CASE("An offset between two distant edits survives both", "[EditJournal]") {
    const std::vector<EditOp> ops{EditOp::Inserted(1, 0, 1), EditOp::Inserted(2, 24, 1)};
    REQUIRE(RelocateThroughAll(10, ops, kHint) == 11);
}

TEST_CASE("A sequence applies in order", "[EditJournal]") {
    const std::vector<EditOp> ops{
        EditOp::Inserted(1, 0, 4),  // 10 -> 14
        EditOp::Deleted(2, 0, 2),   // 14 -> 12
        EditOp::Inserted(3, 12, 3), // 12 -> 15 under Right gravity
    };
    REQUIRE(RelocateThroughAll(10, ops, kHint) == 15);
    REQUIRE(RelocateThroughAll(10, ops, kRangeEnd) == 12);
}

TEST_CASE("A sequence stops at the first op the offset does not survive", "[EditJournal]") {
    const std::vector<EditOp> ops{EditOp::Inserted(1, 0, 4), EditOp::Deleted(2, 12, 16), EditOp::Inserted(3, 0, 100)};
    REQUIRE_FALSE(RelocateThroughAll(10, ops, kHint).has_value());
}

TEST_CASE("A journal replays only what is newer than the holder's generation", "[EditJournal]") {
    EditJournal journal;
    journal.Record(EditOp::Inserted(1, 0, 5));
    journal.Record(EditOp::Inserted(2, 100, 5));

    // Stamped before either edit: both apply.
    REQUIRE(journal.Relocate(50, 0, kHint) == 55);
    // Stamped after the first: only the second applies, and it is past this
    // offset, so nothing moves.
    REQUIRE(journal.Relocate(55, 1, kHint) == 55);
    // Stamped at the newest generation: nothing to replay.
    REQUIRE(journal.Relocate(55, 2, kHint) == 55);
}

TEST_CASE("OpsSince slices strictly by generation", "[EditJournal]") {
    EditJournal journal;
    journal.Record(EditOp::Inserted(1, 0, 1));
    journal.Record(EditOp::Inserted(2, 0, 1));
    journal.Record(EditOp::Inserted(3, 0, 1));

    REQUIRE(journal.OpsSince(0)->size() == 3);
    REQUIRE(journal.OpsSince(2)->size() == 1);
    REQUIRE(journal.OpsSince(3)->empty()); // valid, and distinct from "cannot carry"
}

// The bound is what makes "cannot carry" a real answer rather than a guess --
// a holder ignored for longer than the journal remembers is told so, and
// drops what it holds.
TEST_CASE("A holder older than the journal's reach cannot be carried", "[EditJournal]") {
    EditJournal journal;
    for (std::size_t generation = 1; generation <= EditJournal::kCapacity + 10; ++generation) {
        journal.Record(EditOp::Inserted(generation, 0, 1));
    }
    REQUIRE(journal.Size() == EditJournal::kCapacity);
    REQUIRE(journal.OldestReachableGeneration() == 10);
    REQUIRE_FALSE(journal.OpsSince(9).has_value());
    REQUIRE_FALSE(journal.Relocate(0, 9, kRangeEnd).has_value());
    REQUIRE(journal.OpsSince(10).has_value());
}

TEST_CASE("A fresh journal can carry from the beginning of the document's life", "[EditJournal]") {
    const EditJournal journal;
    REQUIRE(journal.Empty());
    REQUIRE(journal.OldestReachableGeneration() == 0);
    REQUIRE(journal.Relocate(42, 0, kHint) == 42);
}
