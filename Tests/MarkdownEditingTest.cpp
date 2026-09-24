// Markdown's structure-aware editing: fill-paragraph (Mode::fillParagraph --
// one paragraph node's words, reflowed under its own prefixes, everything else
// left alone), the outline, fenced-code language names and folds.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>
#include <string>

#include "Editor/Injection.h"
#include "Editor/Mode.h"

namespace {

// `text` with the paragraph at `point` filled to `width`.
std::string Fill(const std::string& text, std::size_t point, std::size_t width = 20) {
    const ned::editor::Mode                     mode = ned::editor::MarkdownMode();
    const std::optional<ned::editor::FillEdit> edit = mode.fillParagraph(text, point, width);
    if (!edit) {
        return text;
    }
    return text.substr(0, edit->start) + edit->text + text.substr(edit->end);
}

} // namespace

TEST_CASE("Markdown fill reflows one paragraph and stops at its blocks", "[Markdown][Fill]") {
    // The heading, the next list item and the table are not the paragraph's.
    CHECK(Fill("# Heading one\nSome text that follows the heading.\n", 20) ==
          "# Heading one\nSome text that\nfollows the heading.\n");
    CHECK(Fill("- first item here\n- second item\n", 0) == "- first item here\n- second item\n");
    CHECK(Fill("| a | b |\n|---|---|\n| 1 | 2 |\n", 0) == "| a | b |\n|---|---|\n| 1 | 2 |\n");
    CHECK(Fill("Setext heading\n==============\n", 0) == "Setext heading\n==============\n");
    CHECK(Fill("```\ncode line that is long\n```\n", 5) == "```\ncode line that is long\n```\n");
}

TEST_CASE("Markdown fill keeps a list item's marker and hangs the rest under it", "[Markdown][Fill]") {
    CHECK(Fill("- one two three four five six\n", 0) == "- one two three four\n  five six\n");
    CHECK(Fill("1. one two three four five six\n", 0) == "1. one two three\n   four five six\n");
    CHECK(Fill("- [ ] one two three four five\n", 0) == "- [ ] one two three\n      four five\n");
    // A second line's own hang is the author's, and kept.
    CHECK(Fill("- one two three\n    four five six seven\n", 0) == "- one two three four\n    five six seven\n");
}

TEST_CASE("Markdown fill keeps quote markers on every line", "[Markdown][Fill]") {
    CHECK(Fill("> one two three four five six\n> seven\n", 2) == "> one two three four\n> five six seven\n");
    CHECK(Fill("> - one two three four five\n", 4) == "> - one two three\n>   four five\n");
}

TEST_CASE("Markdown fill stays inside one quoted paragraph and keeps nested markers", "[Markdown][Fill]") {
    // The paragraph node reaches over the quote's `>` blank line; the next
    // paragraph is not part of it.
    CHECK(Fill("> one\n>\n> two\n", 0) == "> one\n>\n> two\n");
    // A lazy continuation line takes the first line's prefix, list marker as spaces.
    CHECK(Fill("> 1. > one two three\nfour five\n", 0) == "> 1. > one two three\n>    > four five\n");
}

TEST_CASE("Markdown fill keeps hard line breaks where they are", "[Markdown][Fill]") {
    CHECK(Fill("one two  \nthree four five six seven\n", 0) == "one two  \nthree four five six\nseven\n");
    CHECK(Fill("one two\\\nthree\n", 0) == "one two\\\nthree\n");
}

TEST_CASE("Markdown fill never starts a line with something that opens a block", "[Markdown][Fill]") {
    // "- five" or "1. six" at a line start would become a list item.
    CHECK(Fill("one two three four - five\n", 0) == "one two three four -\nfive\n");
    CHECK(Fill("aaa bbb ccc ddd eee # fff\n", 0) == "aaa bbb ccc ddd eee #\nfff\n");
    CHECK(Fill("one two three four 1. six\n", 0) == "one two three four 1.\nsix\n");
}

TEST_CASE("Markdown outline names setext headings and drops closing hashes", "[Markdown][Outline]") {
    const ned::editor::Mode mode = ned::editor::MarkdownMode();
    const std::string       text = "Setext One\n==========\n\n# Two ##\n\nSetext Three\n------------\n\n## Learn C#\n\n### Four ###   \n";
    std::vector<std::string> names;
    for (const auto& marker : mode.symbolKind(text)) {
        names.push_back(marker.name);
    }
    CHECK(names == std::vector<std::string>{"Setext One", "Two", "Setext Three", "Learn C#", "Four"});
}

TEST_CASE("Common fenced-code info strings resolve to a language", "[Markdown][Injection]") {
    for (const char* tag : {"rs", "rb", "md", "kt", "cs", "c#", "hs", "golang", "jsonc", "python3", "patch", "makefile", "ml"}) {
        INFO(tag);
        ned::editor::EmbeddedLanguageCache cache;
        CHECK(ned::editor::ResolveEmbeddedLanguageHighlight(tag, cache) != nullptr);
    }
}

TEST_CASE("Markdown folds sections, code, list items, quotes and tables", "[Markdown][Fold]") {
    const ned::editor::Mode mode = ned::editor::MarkdownMode();
    REQUIRE(mode.fold);
    const std::string text = "intro\n\n# One\ntext\n\n```\ncode\n```\n\n- item\n  more\n- single\n\n> q\n> r\n\n| a |\n|---|\n| 1 |\n\n## Two\nx\n";
    std::vector<std::string> first;
    for (const auto& [start, end] : mode.fold(text)) {
        first.push_back(text.substr(start, text.find('\n', start) - start));
    }
    std::sort(first.begin(), first.end());
    // No fold for the untitled lead-in, nor for a one-line item.
    CHECK(first == std::vector<std::string>{"# One", "## Two", "- item", "> q", "```", "| a |"});
}
