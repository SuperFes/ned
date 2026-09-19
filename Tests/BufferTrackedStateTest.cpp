#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "Text/Buffer.h"

using ned::text::AnchorId;
using ned::text::AnchorRange;
using ned::text::Buffer;

// Every tracked position in Buffer, across every entry point that can move
// one. The per-tenant tests beside this one check each field's own gravity
// rule in detail; this checks the matrix those tests leave open -- that each
// tenant is actually wired to each entry point.
//
// That is the gap this exists for, and it is not hypothetical: diagnostics
// shipped relocated at five editing sites and not at the sixth (undo/redo),
// so an undo moved the text and left the underlines behind. A per-tenant test
// passes happily while a whole column of the matrix is missing.
//
// Narrowing is deliberately not a tenant here -- it constrains SetPoint, so
// placing an edit outside the narrowed range takes a different setup than
// every other field, and its own four tests already cover each entry point.
namespace {

// Offsets below are into this content after the prior edit in Populate(),
// which is what gives UnsavedChangeRanges_ a tracked range to relocate.
//
//   line one\nline two\nline threeZ\n
//   0         9         18        29
constexpr const char* kContent = "line one\nline two\nline three\n";

struct Placement {
    static constexpr std::size_t kMark            = 20;
    static constexpr std::size_t kFold            = 10;
    static constexpr std::size_t kSecondaryCursor = 12;
    static constexpr std::size_t kSnippetStart    = 11;
    static constexpr std::size_t kSnippetEnd      = 14;
    static constexpr std::size_t kExcerptStart    = 18;
    static constexpr std::size_t kExcerptEnd      = 28;
    static constexpr std::size_t kDiagnosticStart = 19;
    static constexpr std::size_t kDiagnosticEnd   = 23;
    static constexpr std::size_t kUnsavedStart    = 28;
    static constexpr std::size_t kUnsavedEnd      = 29;
    // Every edit below lands inside this one, which is what makes it legal
    // once excerpt ranges are set at all -- see CanInsertAtExcerpt.
    static constexpr std::size_t kEditableEnd = 8;
};

void Populate(Buffer& buffer) {
    // Before anything else: a real edit far from the ones under test, so
    // UnsavedChangeRanges_ carries an entry that must itself relocate.
    buffer.InsertAt(28, "Z");
    REQUIRE(buffer.UnsavedChangeRanges() ==
            std::vector<std::pair<std::size_t, std::size_t>>{{Placement::kUnsavedStart, Placement::kUnsavedEnd}});

    buffer.SetMark(Placement::kMark);
    buffer.SetFoldMarker(Placement::kFold, Buffer::FoldMarker::Collapsed);
    buffer.AddCursorAt(Placement::kSecondaryCursor);
    buffer.SetSnippetRanges({Buffer::SnippetRange{.id           = 1,
                                                  .tabstopIndex = 1,
                                                  .start        = Placement::kSnippetStart,
                                                  .end          = Placement::kSnippetEnd,
                                                  .active       = false}});
    buffer.SetExcerptRanges({
        Buffer::ExcerptRange{.start = 0, .end = Placement::kEditableEnd, .editable = true},
        Buffer::ExcerptRange{.start = Placement::kExcerptStart, .end = Placement::kExcerptEnd, .editable = true},
    });
    buffer.SetDiagnostics({Buffer::Diagnostic{.startByte = Placement::kDiagnosticStart,
                                              .endByte   = Placement::kDiagnosticEnd,
                                              .severity  = Buffer::Diagnostic::Severity::Error,
                                              .message   = "tracked"}});
}

// `delta` is the edit's own length change. Every tenant above sits strictly
// after the edited span, so each one shifts by exactly that and by nothing
// else -- gravity at a boundary is the per-tenant tests' subject, not this
// one's.
void RequireAllMoved(const Buffer& buffer, std::ptrdiff_t delta) {
    const auto shifted = [delta](std::size_t offset) -> std::size_t {
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offset) + delta);
    };

    REQUIRE(buffer.Mark() == shifted(Placement::kMark));
    REQUIRE(buffer.FoldMarkerAt(shifted(Placement::kFold)) == Buffer::FoldMarker::Collapsed);

    REQUIRE(buffer.SecondaryCursors().size() == 1);
    REQUIRE(buffer.SecondaryCursors()[0].point == shifted(Placement::kSecondaryCursor));

    REQUIRE(buffer.SnippetRanges().size() == 1);
    REQUIRE(buffer.SnippetRanges()[0].start == shifted(Placement::kSnippetStart));
    REQUIRE(buffer.SnippetRanges()[0].end == shifted(Placement::kSnippetEnd));

    REQUIRE(buffer.ExcerptRanges().size() == 2);
    REQUIRE(buffer.ExcerptRanges()[0].start == 0); // the edited excerpt itself grows/shrinks in place
    REQUIRE(buffer.ExcerptRanges()[0].end == shifted(Placement::kEditableEnd));
    REQUIRE(buffer.ExcerptRanges()[1].start == shifted(Placement::kExcerptStart));
    REQUIRE(buffer.ExcerptRanges()[1].end == shifted(Placement::kExcerptEnd));

    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].startByte == shifted(Placement::kDiagnosticStart));
    REQUIRE(buffer.Diagnostics()[0].endByte == shifted(Placement::kDiagnosticEnd));

    const std::vector<std::pair<std::size_t, std::size_t>>& unsaved = buffer.UnsavedChangeRanges();
    const std::pair<std::size_t, std::size_t>               expected{shifted(Placement::kUnsavedStart), shifted(Placement::kUnsavedEnd)};
    REQUIRE(std::ranges::find(unsaved, expected) != unsaved.end());
}

} // namespace

TEST_CASE("Every tracked position relocates across every content-mutation entry point", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope(kContent));
    Populate(buffer);

    SECTION("InsertAtPoint") {
        buffer.SetPoint(2);
        buffer.InsertAtPoint("XY");
        REQUIRE(buffer.Point() == 4);
        RequireAllMoved(buffer, 2);
    }

    SECTION("InsertAt") {
        buffer.SetPoint(6);
        buffer.InsertAt(2, "XY");
        REQUIRE(buffer.Point() == 8);
        RequireAllMoved(buffer, 2);
    }

    SECTION("DeleteRange") {
        buffer.SetPoint(6);
        REQUIRE(buffer.DeleteRange(2, 2) == "ne");
        REQUIRE(buffer.Point() == 4);
        RequireAllMoved(buffer, -2);
    }

    SECTION("DeleteBackwardAtPoint") {
        buffer.SetPoint(4);
        buffer.DeleteBackwardAtPoint();
        REQUIRE(buffer.Point() == 3);
        RequireAllMoved(buffer, -1);
    }

    SECTION("DeleteForwardAtPoint") {
        buffer.SetPoint(2);
        buffer.DeleteForwardAtPoint();
        REQUIRE(buffer.Point() == 2);
        RequireAllMoved(buffer, -1);
    }
}

// The anchor seam, held to the same matrix as the fields above. The point of
// anchors is that a new one costs no wiring, so what is actually under test
// is the feed itself: an anchor created by anyone, anywhere, moves across
// every path that changes content -- including the undo/redo path, which is
// exactly where the hand-wired fields kept being forgotten.
TEST_CASE("An anchor relocates across every content-mutation entry point", "[Buffer][AnchorSet]") {
    Buffer         buffer("scratch", ned::text::Rope(kContent));
    const AnchorId anchor = buffer.CreateAnchor(20);
    REQUIRE(buffer.LiveAnchorCount() == 1);

    SECTION("InsertAtPoint") {
        buffer.SetPoint(2);
        buffer.InsertAtPoint("XY");
        REQUIRE(buffer.AnchorOffset(anchor) == 22);
    }

    SECTION("InsertAt") {
        buffer.InsertAt(2, "XY");
        REQUIRE(buffer.AnchorOffset(anchor) == 22);
    }

    SECTION("DeleteRange") {
        buffer.DeleteRange(2, 2);
        REQUIRE(buffer.AnchorOffset(anchor) == 18);
    }

    SECTION("DeleteBackwardAtPoint") {
        buffer.SetPoint(4);
        buffer.DeleteBackwardAtPoint();
        REQUIRE(buffer.AnchorOffset(anchor) == 19);
    }

    SECTION("DeleteForwardAtPoint") {
        buffer.SetPoint(2);
        buffer.DeleteForwardAtPoint();
        REQUIRE(buffer.AnchorOffset(anchor) == 19);
    }

    SECTION("Undo and redo") {
        buffer.InsertAt(2, "XY");
        REQUIRE(buffer.AnchorOffset(anchor) == 22);
        buffer.Undo();
        REQUIRE(buffer.AnchorOffset(anchor) == 20);
        buffer.Redo();
        REQUIRE(buffer.AnchorOffset(anchor) == 22);
    }

    SECTION("A wholesale content swap drops it") {
        buffer.RestoreContent("something else entirely");
        REQUIRE_FALSE(buffer.AnchorOffset(anchor).has_value());
        REQUIRE(buffer.LiveAnchorCount() == 0);
    }
}

TEST_CASE("An anchor range tracks a span across edits at both of its edges", "[Buffer][AnchorSet]") {
    Buffer            buffer("scratch", ned::text::Rope(kContent));
    const AnchorRange word = buffer.CreateAnchorRange(5, 8); // "one"

    buffer.InsertAt(0, "// ");
    REQUIRE(buffer.AnchorRangeOffsets(word) == std::pair<std::size_t, std::size_t>{8, 11});

    // Typing at either edge extends the span -- the default range policy.
    buffer.InsertAt(11, "s");
    REQUIRE(buffer.AnchorRangeOffsets(word) == std::pair<std::size_t, std::size_t>{8, 12});
    REQUIRE(buffer.Text().substr(8, 4) == "ones");

    buffer.DestroyAnchor(word);
    REQUIRE(buffer.LiveAnchorCount() == 0);
    REQUIRE_FALSE(buffer.AnchorRangeOffsets(word).has_value());
}
