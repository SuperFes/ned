#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "Editor/ConflictResolution.h"
#include "Text/Buffer.h"

using ned::editor::ConflictHunkAtPoint;
using ned::editor::ConflictResolution;
using ned::editor::NextConflictHunkStart;
using ned::editor::PreviousConflictHunkStart;
using ned::editor::ResolveConflictHunk;
using ned::text::Buffer;
using ned::text::Rope;

namespace {
    Buffer MakeBuffer(std::string_view text) { return Buffer("test", Rope(text)); }
} // namespace

TEST_CASE("ConflictHunkAtPoint finds the hunk containing point, else nullopt", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    REQUIRE_FALSE(ConflictHunkAtPoint(buffer, 0).has_value()); // in "before"
    REQUIRE(ConflictHunkAtPoint(buffer, buffer.Text().find("ours")).has_value());
    REQUIRE_FALSE(ConflictHunkAtPoint(buffer, buffer.Text().find("after")).has_value());
}

TEST_CASE("ResolveConflictHunk TakeOurs replaces the whole marked block with the ours side", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    const auto hunk = ConflictHunkAtPoint(buffer, buffer.Text().find("ours"));
    REQUIRE(hunk.has_value());
    const std::size_t generationBefore = buffer.ContentGeneration();
    REQUIRE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::TakeOurs));
    REQUIRE(buffer.Text() == "before\nours\nafter\n");
    REQUIRE(buffer.ContentGeneration() != generationBefore);
    buffer.Undo();
    REQUIRE(buffer.Text() == "before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
}

TEST_CASE("ResolveConflictHunk TakeTheirs replaces the whole marked block with the theirs side", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    const auto hunk = ConflictHunkAtPoint(buffer, 0);
    REQUIRE(hunk.has_value());
    REQUIRE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::TakeTheirs));
    REQUIRE(buffer.Text() == "theirs\n");
}

TEST_CASE("ResolveConflictHunk TakeBoth concatenates ours then theirs", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    const auto hunk = ConflictHunkAtPoint(buffer, 0);
    REQUIRE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::TakeBoth));
    REQUIRE(buffer.Text() == "ours\ntheirs\n");
}

TEST_CASE("ResolveConflictHunk TakeNeither deletes the whole marked block", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    const auto hunk = ConflictHunkAtPoint(buffer, buffer.Text().find("ours"));
    REQUIRE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::TakeNeither));
    REQUIRE(buffer.Text() == "before\nafter\n");
}

TEST_CASE("ResolveConflictHunk KeepBase uses the diff3 base section", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("<<<<<<< a\nours\n||||||| base\nbase\n=======\ntheirs\n>>>>>>> b\n");
    const auto hunk = ConflictHunkAtPoint(buffer, 0);
    REQUIRE(hunk->baseRange.has_value());
    REQUIRE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::KeepBase));
    REQUIRE(buffer.Text() == "base\n");
}

TEST_CASE("ResolveConflictHunk KeepBase on a hunk with no base section is a no-op", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    const auto hunk = ConflictHunkAtPoint(buffer, 0);
    REQUIRE_FALSE(hunk->baseRange.has_value());
    const std::string before = buffer.Text();
    REQUIRE_FALSE(ResolveConflictHunk(buffer, *hunk, ConflictResolution::KeepBase));
    REQUIRE(buffer.Text() == before);
}

TEST_CASE("Next/PreviousConflictHunkStart walk hunks in order and wrap", "[ConflictResolution]") {
    const std::string text = "<<<<<<< a\nx\n=======\ny\n>>>>>>> b\n"
                              "middle\n"
                              "<<<<<<< a\np\n=======\nq\n>>>>>>> b\n";
    Buffer            buffer   = MakeBuffer(text);
    const std::size_t firstHunk  = text.find("<<<<<<<");
    const std::size_t secondHunk = text.rfind("<<<<<<<");

    REQUIRE(NextConflictHunkStart(buffer, 0) == secondHunk);
    REQUIRE(NextConflictHunkStart(buffer, secondHunk) == firstHunk); // wraps
    REQUIRE(PreviousConflictHunkStart(buffer, text.size()) == secondHunk);
    REQUIRE(PreviousConflictHunkStart(buffer, firstHunk) == secondHunk); // wraps
}

TEST_CASE("Next/PreviousConflictHunkStart return nullopt with no hunks", "[ConflictResolution]") {
    Buffer buffer = MakeBuffer("plain text, no markers\n");
    REQUIRE_FALSE(NextConflictHunkStart(buffer, 0).has_value());
    REQUIRE_FALSE(PreviousConflictHunkStart(buffer, 0).has_value());
}
