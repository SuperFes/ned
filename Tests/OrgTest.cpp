#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Org.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::org::Checkbox;
using ned::editor::org::CyclePriorityAtPoint;
using ned::editor::org::CycleTodoKeywordAtPoint;
using ned::editor::org::DefaultTodoKeywords;
using ned::editor::org::Headline;
using ned::editor::org::HeadlineAtPoint;
using ned::editor::org::NextPriority;
using ned::editor::org::NextTodoKeyword;
using ned::editor::org::ParseCheckboxes;
using ned::editor::org::ParseOutline;
using ned::editor::org::ReflectParentCheckboxStates;
using ned::editor::org::SetHeadlinePriority;
using ned::editor::org::SetHeadlineTodoKeyword;
using ned::editor::org::ToggleCheckboxAtPoint;
using ned::editor::org::ToggleCheckboxState;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("ParseOutline finds no headlines in plain text", "[Org]") {
    const auto headlines = ParseOutline("just some text\nno stars here\n");
    REQUIRE(headlines.empty());
}

TEST_CASE("ParseOutline finds a plain headline with no keyword/priority/tags", "[Org]") {
    const auto headlines = ParseOutline("* Just a title\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].level == 1);
    REQUIRE(headlines[0].todoKeyword.empty());
    REQUIRE_FALSE(headlines[0].priority.has_value());
    REQUIRE(headlines[0].title == "Just a title");
    REQUIRE(headlines[0].tags.empty());
    REQUIRE(headlines[0].lineNumber == 0);
}

TEST_CASE("ParseOutline reads depth from star count", "[Org]") {
    const auto headlines = ParseOutline("* One\n** Two\n*** Three\n");
    REQUIRE(headlines.size() == 3);
    REQUIRE(headlines[0].level == 1);
    REQUIRE(headlines[1].level == 2);
    REQUIRE(headlines[2].level == 3);
}

TEST_CASE("ParseOutline requires a space after the stars", "[Org]") {
    const auto headlines = ParseOutline("*no-space-here\n* real headline\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].title == "real headline");
}

TEST_CASE("ParseOutline requires stars at column 0, not an indented list item", "[Org]") {
    const auto headlines = ParseOutline("  * indented, not a headline\n* real headline\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].title == "real headline");
}

TEST_CASE("ParseOutline recognizes a TODO keyword", "[Org]") {
    const auto headlines = ParseOutline("* TODO Buy milk\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].todoKeyword == "TODO");
    REQUIRE(headlines[0].title == "Buy milk");
}

TEST_CASE("ParseOutline doesn't misread a word merely starting with a keyword as that keyword", "[Org]") {
    const auto headlines = ParseOutline("* TODOING something\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].todoKeyword.empty());
    REQUIRE(headlines[0].title == "TODOING something");
}

TEST_CASE("ParseOutline recognizes a priority cookie", "[Org]") {
    const auto headlines = ParseOutline("* [#A] Important\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].priority.has_value());
    REQUIRE(*headlines[0].priority == 'A');
    REQUIRE(headlines[0].title == "Important");
}

TEST_CASE("ParseOutline recognizes a TODO keyword and priority cookie together", "[Org]") {
    const auto headlines = ParseOutline("* TODO [#B] Fix the thing\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].todoKeyword == "TODO");
    REQUIRE(*headlines[0].priority == 'B');
    REQUIRE(headlines[0].title == "Fix the thing");
}

TEST_CASE("ParseOutline recognizes trailing tags", "[Org]") {
    const auto headlines = ParseOutline("* Buy milk  :errand:home:\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].title == "Buy milk");
    REQUIRE(headlines[0].tags == std::vector<std::string>{"errand", "home"});
}

TEST_CASE("ParseOutline recognizes keyword, priority, and tags all together", "[Org]") {
    const auto headlines = ParseOutline("*** TODO [#A] Ship it :work:urgent:\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].level == 3);
    REQUIRE(headlines[0].todoKeyword == "TODO");
    REQUIRE(*headlines[0].priority == 'A');
    REQUIRE(headlines[0].title == "Ship it");
    REQUIRE(headlines[0].tags == std::vector<std::string>{"work", "urgent"});
}

TEST_CASE("ParseOutline doesn't mistake a stray colon inside the title for a tag block", "[Org]") {
    const auto headlines = ParseOutline("* Note: this has a colon but no tags\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].title == "Note: this has a colon but no tags");
    REQUIRE(headlines[0].tags.empty());
}

TEST_CASE("ParseOutline handles a title-less headline with only tags", "[Org]") {
    const auto headlines = ParseOutline("* :onlytag:\n");
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].title.empty());
    REQUIRE(headlines[0].tags == std::vector<std::string>{"onlytag"});
}

TEST_CASE("ParseOutline records byte offsets and honors a missing trailing newline", "[Org]") {
    const auto headlines = ParseOutline("* First\nnot a headline\n** Second");
    REQUIRE(headlines.size() == 2);
    REQUIRE(headlines[0].lineNumber == 0);
    REQUIRE(headlines[0].lineStartByte == 0);
    REQUIRE(headlines[0].lineEndByte == 7); // "* First"
    REQUIRE(headlines[1].lineNumber == 2);
    REQUIRE(headlines[1].title == "Second");
}

TEST_CASE("ParseOutline honors a custom keyword set", "[Org]") {
    const std::vector<std::string> keywords{"TODO", "IN-PROGRESS", "DONE"};
    const auto                     headlines = ParseOutline("* IN-PROGRESS Ship it\n", keywords);
    REQUIRE(headlines.size() == 1);
    REQUIRE(headlines[0].todoKeyword == "IN-PROGRESS");
    REQUIRE(headlines[0].title == "Ship it");
}

TEST_CASE("NextTodoKeyword cycles through the default keyword set and back to none", "[Org]") {
    const auto keywords = DefaultTodoKeywords();
    REQUIRE(NextTodoKeyword("", keywords) == "TODO");
    REQUIRE(NextTodoKeyword("TODO", keywords) == "DONE");
    REQUIRE(NextTodoKeyword("DONE", keywords) == "");
}

TEST_CASE("NextTodoKeyword treats an unrecognized keyword the same as none", "[Org]") {
    const auto keywords = DefaultTodoKeywords();
    REQUIRE(NextTodoKeyword("STALE-KEYWORD", keywords) == "TODO");
}

TEST_CASE("NextTodoKeyword returns empty for an empty keyword set", "[Org]") { REQUIRE(NextTodoKeyword("TODO", {}) == ""); }

TEST_CASE("NextPriority cycles A -> B -> C -> none -> A", "[Org]") {
    REQUIRE(*NextPriority(std::nullopt) == 'A');
    REQUIRE(*NextPriority('A') == 'B');
    REQUIRE(*NextPriority('B') == 'C');
    REQUIRE_FALSE(NextPriority('C').has_value());
    REQUIRE(*NextPriority(NextPriority('C')) == 'A');
}

TEST_CASE("ParseCheckboxes finds unchecked and checked items", "[Org]") {
    const auto boxes = ParseCheckboxes("- [ ] Buy milk\n- [X] Walk the dog\n");
    REQUIRE(boxes.size() == 2);
    REQUIRE(boxes[0].state == ' ');
    REQUIRE(boxes[0].text == "Buy milk");
    REQUIRE(boxes[0].indent == 0);
    REQUIRE(boxes[1].state == 'X');
    REQUIRE(boxes[1].text == "Walk the dog");
}

TEST_CASE("ParseCheckboxes ignores non-checkbox lines", "[Org]") {
    const auto boxes = ParseCheckboxes("plain text\n- not a checkbox\n* headline\n");
    REQUIRE(boxes.empty());
}

TEST_CASE("ParseCheckboxes records indent for nesting", "[Org]") {
    const auto boxes = ParseCheckboxes("- [ ] Parent\n  - [ ] Child\n");
    REQUIRE(boxes.size() == 2);
    REQUIRE(boxes[0].indent == 0);
    REQUIRE(boxes[1].indent == 2);
}

TEST_CASE("ParseCheckboxes records the byte offset of the state character", "[Org]") {
    const auto boxes = ParseCheckboxes("- [ ] Buy milk\n");
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0].stateByte == 3); // "- [" is 3 bytes
}

TEST_CASE("ToggleCheckboxState toggles unchecked and checked", "[Org]") {
    REQUIRE(ToggleCheckboxState(' ') == 'X');
    REQUIRE(ToggleCheckboxState('X') == ' ');
    REQUIRE(ToggleCheckboxState('x') == ' ');
}

TEST_CASE("ToggleCheckboxState treats partial the same as unchecked", "[Org]") { REQUIRE(ToggleCheckboxState('-') == 'X'); }

TEST_CASE("ReflectParentCheckboxStates leaves a childless item alone", "[Org]") {
    auto boxes = ParseCheckboxes("- [ ] Solo\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[0].state == ' ');
}

TEST_CASE("ReflectParentCheckboxStates marks a parent checked when all children are checked", "[Org]") {
    auto boxes = ParseCheckboxes("- [ ] Parent\n  - [X] A\n  - [X] B\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[0].state == 'X');
}

TEST_CASE("ReflectParentCheckboxStates marks a parent unchecked when all children are unchecked", "[Org]") {
    auto boxes = ParseCheckboxes("- [X] Parent\n  - [ ] A\n  - [ ] B\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[0].state == ' ');
}

TEST_CASE("ReflectParentCheckboxStates marks a parent partial when children are mixed", "[Org]") {
    auto boxes = ParseCheckboxes("- [ ] Parent\n  - [X] A\n  - [ ] B\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[0].state == '-');
}

TEST_CASE("ReflectParentCheckboxStates propagates through a grandparent", "[Org]") {
    auto boxes = ParseCheckboxes("- [ ] Grandparent\n"
                                  "  - [ ] Parent\n"
                                  "    - [X] Child A\n"
                                  "    - [X] Child B\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[1].state == 'X'); // Parent, from its two checked children
    REQUIRE(boxes[0].state == 'X'); // Grandparent, from Parent's own now-checked state
}

TEST_CASE("ReflectParentCheckboxStates propagates through a single-child chain", "[Org]") {
    // Middle has exactly one (checked) direct child, so Middle itself
    // becomes checked -- and Parent's own one direct child (Middle) is then
    // checked too, so Parent becomes checked as well. Confirms aggregation
    // reads each level's own already-recomputed state, not a raw leaf scan.
    auto boxes = ParseCheckboxes("- [ ] Parent\n"
                                  "  - [ ] Middle\n"
                                  "    - [X] Grandchild\n");
    boxes      = ReflectParentCheckboxStates(std::move(boxes));
    REQUIRE(boxes[1].state == 'X'); // Middle, from its one checked child
    REQUIRE(boxes[0].state == 'X'); // Parent, from Middle's own now-checked state
}

TEST_CASE("HeadlineAtPoint finds the headline point sits on", "[Org]") {
    Buffer buffer("test", Rope("* TODO Buy milk\nsome body text\n"));
    buffer.SetPoint(5); // inside "* TODO Buy milk"

    const auto headline = HeadlineAtPoint(buffer, DefaultTodoKeywords());
    REQUIRE(headline.has_value());
    REQUIRE(headline->title == "Buy milk");
}

TEST_CASE("HeadlineAtPoint returns nullopt off the headline's own line", "[Org]") {
    Buffer buffer("test", Rope("* TODO Buy milk\nsome body text\n"));
    buffer.SetPoint(20); // inside "some body text"

    REQUIRE_FALSE(HeadlineAtPoint(buffer, DefaultTodoKeywords()).has_value());
}

TEST_CASE("SetHeadlineTodoKeyword adds a keyword to a bare headline", "[Org]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlineTodoKeyword(buffer, headlines[0], "TODO");
    REQUIRE(buffer.Text() == "* TODO Buy milk\n");
}

TEST_CASE("SetHeadlineTodoKeyword changes an existing keyword", "[Org]") {
    Buffer buffer("test", Rope("* TODO Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlineTodoKeyword(buffer, headlines[0], "DONE");
    REQUIRE(buffer.Text() == "* DONE Buy milk\n");
}

TEST_CASE("SetHeadlineTodoKeyword removes a keyword, including its own separating space", "[Org]") {
    Buffer buffer("test", Rope("* DONE Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlineTodoKeyword(buffer, headlines[0], "");
    REQUIRE(buffer.Text() == "* Buy milk\n");
}

TEST_CASE("SetHeadlineTodoKeyword handles a keyword-only headline with nothing after it", "[Org]") {
    Buffer buffer("test", Rope("* TODO"));
    const auto headlines = ParseOutline(buffer.Text());
    REQUIRE(headlines[0].todoKeyword == "TODO");
    SetHeadlineTodoKeyword(buffer, headlines[0], "");
    // "* " (stars + the mandatory space after them), not "*" -- the space
    // right after the stars is headline "furniture" ParseHeadlineLine
    // requires to recognize a line as a headline at all; only the
    // keyword's OWN separating space (nonexistent here, since "TODO" ran
    // to the line's own end) would additionally be consumed.
    REQUIRE(buffer.Text() == "* ");
    REQUIRE(ParseOutline(buffer.Text()).size() == 1); // still a valid (title-less) headline
}

TEST_CASE("SetHeadlinePriority adds a priority cookie after an existing keyword", "[Org]") {
    Buffer buffer("test", Rope("* TODO Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlinePriority(buffer, headlines[0], 'A');
    REQUIRE(buffer.Text() == "* TODO [#A] Buy milk\n");
}

TEST_CASE("SetHeadlinePriority changes an existing priority cookie", "[Org]") {
    Buffer buffer("test", Rope("* [#A] Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlinePriority(buffer, headlines[0], 'B');
    REQUIRE(buffer.Text() == "* [#B] Buy milk\n");
}

TEST_CASE("SetHeadlinePriority removes a priority cookie", "[Org]") {
    Buffer buffer("test", Rope("* [#A] Buy milk\n"));
    const auto headlines = ParseOutline(buffer.Text());
    SetHeadlinePriority(buffer, headlines[0], std::nullopt);
    REQUIRE(buffer.Text() == "* Buy milk\n");
}

TEST_CASE("CycleTodoKeywordAtPoint cycles the headline at point and reports success", "[Org]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    buffer.SetPoint(2);

    REQUIRE(CycleTodoKeywordAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "* TODO Buy milk\n");

    buffer.SetPoint(2);
    REQUIRE(CycleTodoKeywordAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "* DONE Buy milk\n");

    buffer.SetPoint(2);
    REQUIRE(CycleTodoKeywordAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "* Buy milk\n");
}

TEST_CASE("CycleTodoKeywordAtPoint reports failure off a headline", "[Org]") {
    Buffer buffer("test", Rope("just text\n"));
    buffer.SetPoint(2);
    REQUIRE_FALSE(CycleTodoKeywordAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "just text\n");
}

TEST_CASE("CyclePriorityAtPoint cycles the headline at point through A/B/C/none", "[Org]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    buffer.SetPoint(2);

    REQUIRE(CyclePriorityAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "* [#A] Buy milk\n");

    buffer.SetPoint(2);
    REQUIRE(CyclePriorityAtPoint(buffer, DefaultTodoKeywords()));
    REQUIRE(buffer.Text() == "* [#B] Buy milk\n");
}

TEST_CASE("ToggleCheckboxAtPoint toggles the checkbox at point", "[Org]") {
    Buffer buffer("test", Rope("- [ ] Buy milk\n"));
    buffer.SetPoint(0);

    REQUIRE(ToggleCheckboxAtPoint(buffer));
    REQUIRE(buffer.Text() == "- [X] Buy milk\n");

    buffer.SetPoint(0);
    REQUIRE(ToggleCheckboxAtPoint(buffer));
    REQUIRE(buffer.Text() == "- [ ] Buy milk\n");
}

TEST_CASE("ToggleCheckboxAtPoint reports failure off a checkbox line", "[Org]") {
    Buffer buffer("test", Rope("just text\n"));
    buffer.SetPoint(0);
    REQUIRE_FALSE(ToggleCheckboxAtPoint(buffer));
}

TEST_CASE("ToggleCheckboxAtPoint reflects a checked child up into its parent", "[Org]") {
    Buffer buffer("test", Rope("- [ ] Parent\n  - [ ] Child A\n  - [X] Child B\n"));
    const std::size_t childAStateByte = buffer.Text().find("[ ] Child A") + 1;
    buffer.SetPoint(childAStateByte);

    REQUIRE(ToggleCheckboxAtPoint(buffer));
    REQUIRE(buffer.Text() == "- [X] Parent\n  - [X] Child A\n  - [X] Child B\n");
}

TEST_CASE("ToggleCheckboxAtPoint reflects a partial state up into the parent", "[Org]") {
    Buffer buffer("test", Rope("- [X] Parent\n  - [X] Child A\n  - [X] Child B\n"));
    const std::size_t childAStateByte = buffer.Text().find("[X] Child A") + 1;
    buffer.SetPoint(childAStateByte);

    REQUIRE(ToggleCheckboxAtPoint(buffer));
    REQUIRE(buffer.Text() == "- [-] Parent\n  - [ ] Child A\n  - [X] Child B\n");
}
