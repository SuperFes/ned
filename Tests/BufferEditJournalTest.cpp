#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Text/Buffer.h"
#include "Text/EditJournal.h"

using ned::text::AnchorPolicy;
using ned::text::Buffer;
using ned::text::Gravity;
using ned::text::InsideDelete;
using ned::text::Rope;

namespace {

// The two policies the LSP result kinds actually use -- an inline annotation
// that must follow its own byte, and a range endpoint that must not swallow
// text typed past it.
constexpr AnchorPolicy kHint{.gravity = Gravity::Right, .insideDelete = InsideDelete::Invalidate};
constexpr AnchorPolicy kRangeEnd{.gravity = Gravity::Left, .insideDelete = InsideDelete::Clamp};

} // namespace

// The invariant the Commit* helpers exist to hold: a caller that has seen a
// generation can always use it as a starting point, because every bump has an
// op behind it.
TEST_CASE("Every content generation has exactly one journal op", "[Buffer][EditJournal]") {
    Buffer            buffer("scratch", Rope("hello world"));
    const std::size_t startGeneration = buffer.ContentGeneration();
    const std::size_t startOps        = buffer.Edits().Size();

    buffer.InsertAtPoint("a");
    buffer.SetPoint(3);
    buffer.DeleteForwardAtPoint();
    buffer.InsertAt(0, "xy");
    buffer.DeleteRange(0, 1);

    REQUIRE(buffer.ContentGeneration() - startGeneration == 4);
    REQUIRE(buffer.Edits().Size() - startOps == 4);
}

TEST_CASE("An insert records its own offset and length", "[Buffer][EditJournal]") {
    Buffer            buffer("scratch", Rope("hello"));
    const std::size_t before = buffer.ContentGeneration();
    buffer.InsertAt(2, "XY");

    REQUIRE(buffer.Edits().Relocate(1, before, kHint) == 1); // before the insert
    REQUIRE(buffer.Edits().Relocate(2, before, kHint) == 4); // on it, right gravity
    REQUIRE(buffer.Edits().Relocate(2, before, kRangeEnd) == 2);
    REQUIRE(buffer.Edits().Relocate(3, before, kHint) == 5); // after it
}

TEST_CASE("A delete records its own range", "[Buffer][EditJournal]") {
    Buffer            buffer("scratch", Rope("hello world"));
    const std::size_t before = buffer.ContentGeneration();
    buffer.DeleteRange(2, 3); // removes bytes [2,5)

    REQUIRE(buffer.Edits().Relocate(1, before, kHint) == 1);
    REQUIRE_FALSE(buffer.Edits().Relocate(3, before, kHint).has_value());
    REQUIRE(buffer.Edits().Relocate(3, before, kRangeEnd) == 2);
    REQUIRE(buffer.Edits().Relocate(7, before, kHint) == 4);
}

// The failure the snapshot diff could not express, at the Buffer level: two
// edits either side of an offset, with nothing reading in between.
TEST_CASE("An offset between two distant edits survives both", "[Buffer][EditJournal]") {
    Buffer            buffer("scratch", Rope("aaaa\nbbbb\ncccc\n"));
    const std::size_t before = buffer.ContentGeneration();
    buffer.InsertAt(0, "x");
    buffer.InsertAt(buffer.Size(), "y");

    REQUIRE(buffer.Edits().Relocate(7, before, kHint) == 8);
}

TEST_CASE("Backward and forward delete-at-point record their own ranges", "[Buffer][EditJournal]") {
    Buffer buffer("scratch", Rope("hello"));
    buffer.SetPoint(3);
    const std::size_t before = buffer.ContentGeneration();
    buffer.DeleteBackwardAtPoint(); // removes byte [2,3)
    REQUIRE(buffer.Edits().Relocate(4, before, kHint) == 3);

    const std::size_t afterBackward = buffer.ContentGeneration();
    buffer.SetPoint(0);
    buffer.DeleteForwardAtPoint(); // removes byte [0,1)
    REQUIRE(buffer.Edits().Relocate(3, afterBackward, kHint) == 2);
}

// A whole-content swap has no edit to describe, so it says so rather than
// letting a holder relocate through a document that no longer exists.
TEST_CASE("A wholesale content replacement is a barrier nothing survives", "[Buffer][EditJournal]") {
    Buffer            buffer("scratch", Rope("hello world"));
    const std::size_t before = buffer.ContentGeneration();
    buffer.RestoreContent("something else entirely");

    REQUIRE_FALSE(buffer.Edits().Relocate(0, before, kRangeEnd).has_value());
    REQUIRE_FALSE(buffer.Edits().Relocate(3, before, kHint).has_value());
}

// Undo has no offset/length of its own, so its op is recovered from the two
// versions' diff -- exact here because a restore is one hop, not a run.
TEST_CASE("Undo records a diff-recovered op", "[Buffer][EditJournal]") {
    Buffer buffer("scratch", Rope("hello world"));
    buffer.InsertAt(5, "XYZ"); // "helloXYZ world"
    const std::size_t afterInsert = buffer.ContentGeneration();

    buffer.Undo();
    REQUIRE(buffer.Text() == "hello world");
    // An offset past the restored region comes back by the same three bytes.
    REQUIRE(buffer.Edits().Relocate(10, afterInsert, kHint) == 7);
    // One inside it does not come back at all.
    REQUIRE_FALSE(buffer.Edits().Relocate(6, afterInsert, kHint).has_value());
}

TEST_CASE("An undo that changes no content relocates nothing", "[Buffer][EditJournal]") {
    Buffer buffer("scratch", Rope("hello"));
    buffer.InsertAtPoint("a");
    const std::size_t before = buffer.ContentGeneration();
    buffer.Undo();
    buffer.Redo();
    REQUIRE(buffer.Text() == "ahello");
    REQUIRE(buffer.Edits().Relocate(3, before, kHint) == 3);
}
