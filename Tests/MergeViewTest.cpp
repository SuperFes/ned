#include <catch2/catch_test_macros.hpp>

#include "Editor/MergeView.h"
#include "Editor/ModeOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::MergeSideBufferName;
using ned::editor::MergeViewSession;
using ned::editor::RewriteReadOnlyBuffer;
using ned::text::Buffer;
using ned::text::BufferList;
using ned::text::MergeSideKind;

namespace {

constexpr const char* kConflicted = "before\n<<<<<<< HEAD\nours 1\nours 2\n=======\ntheirs\n>>>>>>> feature\nafter\n";

Buffer& MergedBuffer(BufferList& list, const std::string& text) {
    Buffer& buffer = list.CreateBuffer("main.cpp");
    buffer.InsertAtPoint(text);
    buffer.SetPoint(0);
    return buffer;
}

} // namespace

TEST_CASE("A session creates read-only side buffers holding each side's text", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});

    const auto sides = session.Sides();
    REQUIRE(sides.size() == 2);
    CHECK(sides[0]->Name() == MergeSideBufferName(MergeSideKind::Ours, "main.cpp"));
    CHECK(sides[0]->Text() == "before\nours 1\nours 2\nafter\n");
    CHECK(sides[0]->ReadOnly());
    CHECK(sides[1]->Text() == "before\ntheirs\nafter\n");
    CHECK(session.KindOf(*sides[1]) == MergeSideKind::Theirs);
    CHECK(session.Merged() == &merged);
    CHECK(session.ConflictCount() == 1);
}

TEST_CASE("A base side is skipped when the markers carry no diff3 section", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Base, MergeSideKind::Theirs});
    CHECK(session.Sides().size() == 2);
    CHECK_FALSE(list.Find(MergeSideBufferName(MergeSideKind::Base, "main.cpp")));
}

TEST_CASE("Ending a session closes its side buffers and leaves the merged one", "[MergeView]") {
    BufferList list;
    Buffer&    merged = MergedBuffer(list, kConflicted);
    {
        MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});
        CHECK(list.Find(MergeSideBufferName(MergeSideKind::Ours, "main.cpp")));
    }
    CHECK_FALSE(list.Find(MergeSideBufferName(MergeSideKind::Ours, "main.cpp")));
    CHECK_FALSE(list.Find(MergeSideBufferName(MergeSideKind::Theirs, "main.cpp")));
    CHECK(list.Find("main.cpp") == &merged);
}

TEST_CASE("Editing the merged buffer re-derives the sides and their alignment", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});
    Buffer&          ours = *session.Sides()[0];

    merged.InsertAt(0, "top\n");
    session.Refresh();
    const auto* alignment = session.AlignmentFor(ours);
    REQUIRE(alignment);
    CHECK(ours.Text() == "top\nbefore\nours 1\nours 2\nafter\n");
    CHECK(ours.ReadOnly());
    // "after" is merged line 8 and ours line 4, and they share a row.
    CHECK(alignment->AlignedRow(4) == session.AlignmentFor(merged)->AlignedRow(8));
}

TEST_CASE("Unrelated buffers get no alignment", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    Buffer&          other  = list.CreateBuffer("other");
    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    CHECK(session.AlignmentFor(other) == nullptr);
    CHECK_FALSE(session.Contains(other));
    CHECK(session.Contains(merged));
}

TEST_CASE("A side buffer edited out from under the session is rewritten", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    Buffer&          ours = *session.Sides()[0];

    ours.SetReadOnly(false);
    ours.InsertAt(0, "junk\n");
    session.Refresh();
    CHECK(ours.Text() == "before\nours 1\nours 2\nafter\n");
    CHECK(ours.ReadOnly());
}

TEST_CASE("CorrespondingLine maps through blank rows", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});
    Buffer&          theirs = *session.Sides()[1];

    CHECK(session.CorrespondingLine(merged, 7, theirs) == 2); // after -> after
    CHECK(session.CorrespondingLine(merged, 1, theirs) == 1); // hunk start -> "theirs"
    CHECK(session.CorrespondingLine(merged, 4, theirs) == 1); // inside the hunk -> its last line
    CHECK(session.CorrespondingLine(theirs, 2, merged) == 7);
}

TEST_CASE("ChangedLines names the hunk's lines in every pane", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});
    using Ranges = std::vector<std::pair<std::size_t, std::size_t>>;
    CHECK(session.ChangedLines(merged) == Ranges{{1, 7}});
    CHECK(session.ChangedLines(*session.Sides()[0]) == Ranges{{1, 3}});
    CHECK(session.ChangedLines(*session.Sides()[1]) == Ranges{{1, 2}});
}

TEST_CASE("TakeSide replaces the merged hunk with that side's lines as one undo step", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});
    Buffer&          ours = *session.Sides()[0];

    CHECK_FALSE(session.TakeSide(ours, 0)); // an unchanged line
    REQUIRE(session.TakeSide(ours, 2));
    CHECK(merged.Text() == "before\nours 1\nours 2\nafter\n");
    CHECK(merged.Point() == std::string("before\n").size());
    CHECK(session.ConflictCount() == 0);
    CHECK(session.ChangedLines(merged).empty());
    CHECK(session.AlignmentFor(merged)->RowsAfter(0) == 0);

    merged.Undo();
    CHECK(merged.Text() == kConflicted);
}

TEST_CASE("TakeSide reaches an empty side through the line above its gap", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, "a\n<<<<<<< HEAD\n=======\ngone\n>>>>>>> b\nz\n");
    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    Buffer&          ours = *session.Sides()[0];

    REQUIRE(session.TakeSide(ours, 0));
    CHECK(merged.Text() == "a\nz\n");
}

TEST_CASE("TakeSide refuses a read-only merged buffer", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    merged.SetReadOnly(true);
    CHECK_FALSE(session.TakeSide(*session.Sides()[0], 1));
    CHECK(merged.Text() == kConflicted);
}

TEST_CASE("Changed-chunk navigation wraps in both directions", "[MergeView]") {
    BufferList        list;
    const std::string two    = std::string(kConflicted) + "<<<<<<< HEAD\nx\n=======\ny\n>>>>>>> b\n";
    Buffer&           merged = MergedBuffer(list, two);
    MergeViewSession  session(list, merged, {MergeSideKind::Ours, MergeSideKind::Theirs});

    CHECK(session.NextChangedLine(0) == 1);
    CHECK(session.NextChangedLine(1) == 8);
    CHECK(session.NextChangedLine(8) == 1);
    CHECK(session.PreviousChangedLine(8) == 1);
    CHECK(session.PreviousChangedLine(1) == 8);
}

TEST_CASE("A closed merged buffer leaves the session inert", "[MergeView]") {
    BufferList       list;
    Buffer&          merged = MergedBuffer(list, kConflicted);
    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    Buffer&          ours = *session.Sides()[0];
    list.Close("main.cpp");
    CHECK(session.Merged() == nullptr);
    CHECK_FALSE(session.TakeSide(ours, 1));
    session.Refresh();
}

TEST_CASE("RewriteReadOnlyBuffer touches only the lines that differ", "[MergeView]") {
    Buffer buffer("side");
    buffer.InsertAtPoint("one\ntwo\nthree\n");
    buffer.SetReadOnly(true);
    buffer.SetPoint(std::string("one\ntwo\nth").size());

    RewriteReadOnlyBuffer(buffer, "one\nTWO\nthree\n");
    CHECK(buffer.Text() == "one\nTWO\nthree\n");
    CHECK(buffer.ReadOnly());
    // Point sat in an untouched line after the edit and kept its place.
    CHECK(buffer.Point() == std::string("one\nTWO\nth").size());

    RewriteReadOnlyBuffer(buffer, "één\nTWO\nthree\n");
    CHECK(buffer.Text() == "één\nTWO\nthree\n");
    RewriteReadOnlyBuffer(buffer, "");
    CHECK(buffer.Text().empty());
    RewriteReadOnlyBuffer(buffer, "x");
    CHECK(buffer.Text() == "x");
}

TEST_CASE("Side buffers highlight in the merged buffer's mode", "[MergeView]") {
    BufferList list;
    Buffer&    merged = MergedBuffer(list, kConflicted);
    merged.SetPath("/nonexistent/main.cpp");
    ned::editor::ClearModeCacheFor(merged);
    const std::string mergedMode = ned::editor::CachedModeForBuffer(merged).name;
    REQUIRE(mergedMode != ned::editor::FundamentalMode().name);

    MergeViewSession session(list, merged, {MergeSideKind::Ours});
    CHECK(ned::editor::CachedModeForBuffer(*session.Sides()[0]).name == mergedMode);
    ned::editor::ClearModeCacheFor(merged);
}
