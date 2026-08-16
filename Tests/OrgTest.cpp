#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Org.h"

using ned::editor::org::DefaultTodoKeywords;
using ned::editor::org::Headline;
using ned::editor::org::NextPriority;
using ned::editor::org::NextTodoKeyword;
using ned::editor::org::ParseOutline;

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
