#include <catch2/catch_test_macros.hpp>

#include "Editor/LinkedEditingSession.h"
#include "Text/Buffer.h"

using ned::editor::LinkedEditingSession;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("LinkedEditingSession::Start activates whichever range contains point", "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(2); // inside the opening tag's "div"
    auto session = LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}});
    REQUIRE(session.has_value());

    const auto& ranges = buffer.SnippetRanges();
    REQUIRE(ranges.size() == 2);
    REQUIRE(ranges[0] == Buffer::SnippetRange{1, 0, 1, 4, true});
    REQUIRE(ranges[1] == Buffer::SnippetRange{2, 0, 7, 10, false});
}

TEST_CASE("LinkedEditingSession::Start declines fewer than 2 ranges or point outside every range",
          "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(2);
    REQUIRE_FALSE(LinkedEditingSession::Start(buffer, "scratch", {{1, 4}}).has_value());
    REQUIRE(buffer.SnippetRanges().empty());

    buffer.SetPoint(0); // outside both ranges
    REQUIRE_FALSE(LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}}).has_value());
}

TEST_CASE("LinkedEditingSession typing in one range mirrors into the other, one undo step at a time",
          "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(4); // end of the opening tag's "div"
    auto session = LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}});
    REQUIRE(session.has_value());

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("2");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "<div2></div2>");
    REQUIRE(buffer.Point() == 5);
    REQUIRE(session->PointStillInside(buffer));

    buffer.Undo();
    REQUIRE(buffer.Text() == "<div></div>"); // keystroke + mirror sync undo together
}

TEST_CASE("LinkedEditingSession::PointStillInside goes false once point leaves every range",
          "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(2);
    auto session = LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}});
    REQUIRE(session.has_value());
    REQUIRE(session->PointStillInside(buffer));

    buffer.SetPoint(0);
    REQUIRE_FALSE(session->PointStillInside(buffer));
}

TEST_CASE("LinkedEditingSession::RangesValid goes false once undo clears the buffer's snippet ranges",
          "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(2);
    auto session = LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}});
    REQUIRE(session.has_value());
    REQUIRE(session->RangesValid(buffer));

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("x");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    buffer.Undo();
    buffer.Undo(); // past the session's own start -- Undo() clears SnippetRanges_ outright
    REQUIRE_FALSE(session->RangesValid(buffer));
}

TEST_CASE("LinkedEditingSession::Finish clears the buffer's snippet ranges and keeps the text",
          "[LinkedEditingSession]") {
    Buffer buffer("scratch", Rope("<div></div>"));
    buffer.SetPoint(2);
    auto session = LinkedEditingSession::Start(buffer, "scratch", {{1, 4}, {7, 10}});
    REQUIRE(session.has_value());
    session->Finish(buffer);
    REQUIRE(buffer.SnippetRanges().empty());
    REQUIRE(buffer.Text() == "<div></div>");
}
