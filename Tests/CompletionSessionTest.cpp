#include <catch2/catch_test_macros.hpp>

#include "Editor/CompletionSession.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::CompletionSession;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::WorkspaceTextEdit;
using ned::text::Buffer;
using ned::text::Rope;

namespace {

    Buffer MakeBuffer(const std::string& content) { return Buffer("test", Rope(content)); }

    // Mirrors what LspContent's own parsing guarantees (sortText/filterText
    // default to the label), so a test that doesn't care about them doesn't
    // have to keep restating it.
    CompletionItem Item(std::string label, std::string insertText = {}) {
        CompletionItem item;
        item.label      = label;
        item.insertText = insertText.empty() ? label : std::move(insertText);
        item.sortText   = label;
        item.filterText = label;
        return item;
    }

} // namespace

TEST_CASE("A candidate with no textEdit falls back to the caller's word-boundary prefix start", "[CompletionSession]") {
    Buffer            buffer  = MakeBuffer("foo");
    const std::size_t point   = 3;
    CompletionSession session({Item("foobar")}, /*isIncomplete=*/false, buffer.Content(), point, /*fallbackPrefixStart=*/0);

    REQUIRE(session.Candidates().size() == 1);
    CHECK(session.Candidates()[0].replaceStart == 0);
}

TEST_CASE("A candidate's textEdit range start wins over the fallback prefix start", "[CompletionSession]") {
    // "std::vec" -- the editor's own ASCII word rule starts the prefix at
    // "vec" (offset 5), but clangd's textEdit covers "std::vec" from 0.
    Buffer            buffer = MakeBuffer("std::vec");
    const std::size_t point  = 8;

    CompletionItem item = Item("vector", "std::vector");
    item.textEdit       = WorkspaceTextEdit{.start   = LspPosition{.line = 0, .character = 0},
                                            .end     = LspPosition{.line = 0, .character = 8},
                                            .newText = "std::vector"};

    CompletionSession session({item}, false, buffer.Content(), point, /*fallbackPrefixStart=*/5);
    REQUIRE(session.Candidates().size() == 1);
    CHECK(session.Candidates()[0].replaceStart == 0);

    const auto plan = session.PlanAccept(point);
    REQUIRE(plan.has_value());
    CHECK(plan->replaceStart == 0);
    CHECK(plan->replaceEnd == 8);
    CHECK(plan->newText == "std::vector");
}

TEST_CASE("A textEdit range starting after point degrades to an insert at point", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("foo bar");
    const std::size_t point  = 3;

    CompletionItem item = Item("foo_thing");
    item.textEdit       = WorkspaceTextEdit{.start   = LspPosition{.line = 0, .character = 5}, // past point
                                            .end     = LspPosition{.line = 0, .character = 7},
                                            .newText = "foo_thing"};

    CompletionSession session({item}, false, buffer.Content(), point, 0);
    const auto        plan = session.PlanAccept(point);
    REQUIRE(plan.has_value());
    CHECK(plan->replaceStart == point); // never a reversed range that would delete backwards
    CHECK(plan->replaceEnd == point);
}

TEST_CASE("An insertText that doesn't extend the typed prefix replaces it rather than appending", "[CompletionSession]") {
    // The regression this whole step exists for: a server doing its own
    // fuzzy matching answers "sco" with "some_count", which shares no
    // prefix. The old suffix-subtraction path inserted the *whole* string
    // after the typed text, producing "scosome_count".
    Buffer            buffer = MakeBuffer("sco");
    const std::size_t point  = 3;

    CompletionSession session({Item("some_count")}, false, buffer.Content(), point, /*fallbackPrefixStart=*/0);
    const auto        plan = session.PlanAccept(point);
    REQUIRE(plan.has_value());
    CHECK(plan->replaceStart == 0);
    CHECK(plan->replaceEnd == 3);
    CHECK(plan->newText == "some_count");
}

TEST_CASE("PlanAccept reports a snippet item's raw body and flags it for expansion", "[CompletionSession]") {
    Buffer         buffer = MakeBuffer("fo");
    CompletionItem item   = Item("for", "for (${1:i}) {\n\t$0\n}");
    item.isSnippet        = true;

    CompletionSession session({item}, false, buffer.Content(), 2, 0);
    const auto        plan = session.PlanAccept(2);
    REQUIRE(plan.has_value());
    CHECK(plan->isSnippet);
    CHECK(plan->newText == "for (${1:i}) {\n\t$0\n}"); // raw, never pre-expanded
    CHECK(plan->replaceStart == 0);
}

TEST_CASE("PlanAccept is nullopt with no candidates", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("");
    CompletionSession session({}, false, buffer.Content(), 0, 0);
    CHECK(session.Empty());
    CHECK_FALSE(session.PlanAccept(0).has_value());
}

TEST_CASE("Candidates are ranked by sortText, not arrival order", "[CompletionSession]") {
    Buffer buffer = MakeBuffer("");

    CompletionItem zebra = Item("zebra");
    zebra.sortText       = "0001"; // the server wants this one first
    CompletionItem apple = Item("apple");
    apple.sortText       = "0002";

    CompletionSession session({apple, zebra}, false, buffer.Content(), 0, 0);
    REQUIRE(session.Candidates().size() == 2);
    CHECK(session.Candidates()[0].item.label == "zebra");
    CHECK(session.Candidates()[1].item.label == "apple");
}

TEST_CASE("Ranking matches against filterText, not the label", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("foo");
    CompletionItem    item   = Item("foo (from <bar>)");
    item.filterText          = "foo";

    CompletionSession session({item, Item("unrelated")}, false, buffer.Content(), 3, 0);
    REQUIRE(session.Candidates().size() == 1);
    CHECK(session.Candidates()[0].item.label == "foo (from <bar>)");
}

TEST_CASE("Selection is bounded: an out-of-range Select is ignored, Cycle wraps", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("");
    CompletionSession session({Item("a"), Item("b")}, false, buffer.Content(), 0, 0);

    CHECK(session.SelectedIndex() == 0);
    session.Select(99); // a click racing a just-narrowed list must not select a neighbor
    CHECK(session.SelectedIndex() == 0);
    session.Select(1);
    CHECK(session.SelectedIndex() == 1);
    session.Cycle(1);
    CHECK(session.SelectedIndex() == 0); // wrapped
    session.Cycle(-1);
    CHECK(session.SelectedIndex() == 1); // wrapped the other way
}

TEST_CASE("Refilter narrows locally as the prefix grows, without a re-request", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("f");
    CompletionSession session({Item("foobar"), Item("fizz"), Item("food")}, /*isIncomplete=*/false, buffer.Content(), 1, 0);
    REQUIRE(session.Candidates().size() == 3);

    // The caller has already applied the edit; "foo" is now typed.
    Buffer typed = MakeBuffer("foo");
    CHECK(session.Refilter(typed.Content(), /*point=*/3, /*currentPrefixStart=*/0) == CompletionSession::Outcome::Keep);
    REQUIRE(session.Candidates().size() == 2);
    // "foobar" and "food" score identically (both match "foo" as a
    // consecutive run at offset 0 -- FuzzyScore doesn't consider the
    // unmatched tail), so the tiebreak falls to sortText, which
    // LspContent defaulted to the label. Server intent deliberately wins
    // over any shorter-is-better heuristic of ours.
    CHECK(session.Candidates()[0].item.label == "foobar");
    CHECK(session.Candidates()[1].item.label == "food");
    CHECK(session.SelectedIndex() == 0); // reset to the best match, not left pointing at a dropped item
}

TEST_CASE("Refilter widens again on backspace, recovering previously dropped candidates", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("fo");
    CompletionSession session({Item("foobar"), Item("fizz")}, false, buffer.Content(), 2, 0);
    REQUIRE(session.Candidates().size() == 1); // "fizz" doesn't contain "fo" in order

    Buffer backspaced = MakeBuffer("f");
    CHECK(session.Refilter(backspaced.Content(), 1, 0) == CompletionSession::Outcome::Keep);
    CHECK(session.Candidates().size() == 2); // recovered with no round trip
}

TEST_CASE("Refilter dismisses once a complete list no longer matches", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("f");
    CompletionSession session({Item("foobar")}, /*isIncomplete=*/false, buffer.Content(), 1, 0);

    Buffer typed = MakeBuffer("fzz");
    CHECK(session.Refilter(typed.Content(), 3, 0) == CompletionSession::Outcome::Dismiss);
}

TEST_CASE("Refilter re-requests instead of dismissing when the list was incomplete", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("f");
    CompletionSession session({Item("foobar")}, /*isIncomplete=*/true, buffer.Content(), 1, 0);

    // Still matching: the popup stays usable *and* the server gets re-asked.
    Buffer narrowed = MakeBuffer("foo");
    CHECK(session.Refilter(narrowed.Content(), 3, 0) == CompletionSession::Outcome::Rerequest);
    CHECK(session.Candidates().size() == 1);

    // No longer matching: an incomplete list may simply not have been sent
    // the match, so ask rather than give up.
    Buffer diverged = MakeBuffer("fzz");
    CHECK(session.Refilter(diverged.Content(), 3, 0) == CompletionSession::Outcome::Rerequest);
}

TEST_CASE("Refilter dismisses when the word boundary moves under it", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("foo");
    CompletionSession session({Item("foobar")}, false, buffer.Content(), 3, /*fallbackPrefixStart=*/0);

    // Typing "." ends the identifier: the caller's word rule now starts the
    // prefix after the dot. The session doesn't need to know "." is a
    // trigger character -- it just reports that its own word is over, and
    // the caller's auto-trigger gate issues the genuinely new request.
    Buffer dotted = MakeBuffer("foo.");
    CHECK(session.Refilter(dotted.Content(), /*point=*/4, /*currentPrefixStart=*/4) == CompletionSession::Outcome::Dismiss);
}

TEST_CASE("Refilter dismisses when point moves before the session's prefix start", "[CompletionSession]") {
    Buffer            buffer = MakeBuffer("let foo");
    CompletionSession session({Item("foobar")}, false, buffer.Content(), 7, /*fallbackPrefixStart=*/4);

    CHECK(session.Refilter(buffer.Content(), /*point=*/2, /*currentPrefixStart=*/0) == CompletionSession::Outcome::Dismiss);
}
