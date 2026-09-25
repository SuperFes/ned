// Enter and Backspace on Markdown's structured lines (Mode::continueLine):
// lists and quotes carry on, an empty item ends its list, and one Backspace
// takes the carried marker back.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Command.h"
#include "Editor/Commands.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::editor::CommandContext;
using ned::editor::CommandRegistry;

namespace {

// `text` with point at its `|`, after the given commands run in order.
std::string Run(std::string text, std::initializer_list<const char*> commands) {
    CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    const ned::editor::Mode mode = ned::editor::MarkdownMode();
    ned::text::Buffer       buffer("test.md");
    ned::text::KillRing     killRing;
    ned::text::BufferList   bufferList;
    const std::size_t       point = text.find('|');
    text.erase(point, 1);
    buffer.InsertAtPoint(text);
    buffer.SetPoint(point);
    std::string last;
    for (const char* command : commands) {
        CommandContext context{buffer, killRing, bufferList};
        context.mode        = &mode;
        context.lastCommand = last;
        registry.Invoke(command, context);
        last = command;
    }
    std::string result = buffer.Text();
    result.insert(buffer.Point(), "|");
    return result;
}

} // namespace

TEST_CASE("Enter carries a list item's marker onto the new line", "[Markdown][Enter]") {
    CHECK(Run("- one|", {"newline"}) == "- one\n- |");
    CHECK(Run("* one|", {"newline"}) == "* one\n* |");
    CHECK(Run("1. one|", {"newline"}) == "1. one\n2. |");
    CHECK(Run("9) one|", {"newline"}) == "9) one\n10) |");
    CHECK(Run("- [x] done|", {"newline"}) == "- [x] done\n- [ ] |");
    CHECK(Run("  - nested|", {"newline"}) == "  - nested\n  - |");
    CHECK(Run("-   wide|", {"newline"}) == "-   wide\n-   |");
    // Mid-line: the rest moves to the new item.
    CHECK(Run("- one|two", {"newline"}) == "- one\n- |two");
}

TEST_CASE("Enter carries a quote's markers onto the new line", "[Markdown][Enter]") {
    CHECK(Run("> quoted|", {"newline"}) == "> quoted\n> |");
    CHECK(Run("> > deep|", {"newline"}) == "> > deep\n> > |");
    CHECK(Run("> - item|", {"newline"}) == "> - item\n> - |");
}

TEST_CASE("Enter on an empty item or quote line ends it", "[Markdown][Enter]") {
    CHECK(Run("- one\n- |", {"newline"}) == "- one\n|");
    CHECK(Run("- one\n- [ ] |", {"newline"}) == "- one\n|");
    CHECK(Run("> a\n> > |", {"newline"}) == "> a\n>|");
    CHECK(Run("> a\n> |", {"newline"}) == "> a\n|");
}

TEST_CASE("Enter is ordinary where the marker isn't Markdown structure", "[Markdown][Enter]") {
    // Inside code, and before an item's content, a newline is just a newline.
    CHECK(Run("```\n- in code|\n```\n", {"newline"}) == "```\n- in code\n|\n```\n");
    // Inside a fence -- closed or still being typed -- the line above's indent carries on.
    CHECK(Run("```\n    code|\n```\n", {"newline"}) == "```\n    code\n    |\n```\n");
    CHECK(Run("```\n    code|", {"newline"}) == "```\n    code\n    |");
    CHECK(Run("|- one", {"newline"}) == "\n|- one");
    CHECK(Run("plain text|", {"newline"}) == "plain text\n|");
}

TEST_CASE("One Backspace right after Enter takes the carried marker back", "[Markdown][Enter]") {
    // What remains is what a plain Enter would have given: the item's hang.
    CHECK(Run("- one|", {"newline", "backward-delete-char"}) == "- one\n    |");
    CHECK(Run("> quoted|", {"newline", "backward-delete-char"}) == "> quoted\n|");
    // Not straight after Enter: an ordinary backspace.
    CHECK(Run("- one\n- |", {"backward-delete-char"}) == "- one\n-|");
}

TEST_CASE("Enter in an ordered list renumbers the items after the new one", "[Markdown][Enter]") {
    CHECK(Run("1. one|\n2. two\n3. three\n", {"newline"}) == "1. one\n2. |\n3. two\n4. three\n");
    // A nested list and an item's own lines don't break the count.
    CHECK(Run("1. one|\n   - sub\n2. two\n   more\n", {"newline"}) == "1. one\n2. |\n   - sub\n3. two\n   more\n");
    // Taking the item back with Backspace numbers the list as it was.
    CHECK(Run("1. one|\n2. two\n", {"newline", "backward-delete-char"}) == "1. one\n    |\n2. two\n");
    // A list numbering every item the same keeps doing so.
    CHECK(Run("1. one|\n1. two\n", {"newline"}) == "1. one\n1. |\n1. two\n");
    // A bullet list has nothing to renumber.
    CHECK(Run("- one|\n- two\n", {"newline"}) == "- one\n- |\n- two\n");
}

TEST_CASE("Enter at the end of an item's later line starts the next item", "[Markdown][Enter]") {
    CHECK(Run("1. one\n   more|\n2. two\n", {"newline"}) == "1. one\n   more\n2. |\n3. two\n");
    CHECK(Run("- [x] done\n  still done|", {"newline"}) == "- [x] done\n  still done\n- [ ] |");
    CHECK(Run("> - item\n>   more|", {"newline"}) == "> - item\n>   more\n> - |");
    // Before the line's text it is an ordinary newline.
    CHECK(Run("- one\n  |more", {"newline"}).find("- |") == std::string::npos);
}
