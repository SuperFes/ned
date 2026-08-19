#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Mode.h"

using ned::editor::HighlightSpan;
using ned::editor::MarkdownMode;
using ned::editor::SyntaxClass;

namespace {

// True if any span in spans covers exactly [start, end) with class cls.
bool HasSpan(const std::vector<HighlightSpan>& spans, std::size_t start, std::size_t end, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte == start && span.endByte == end && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("MarkdownMode has a highlighting hook installed", "[Markdown]") {
    const auto mode = MarkdownMode();
    REQUIRE(mode.name == "markdown-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("MarkdownMode ATX headings cycle HeadlineLevel1/2/3 by level, whole line included", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "# H1\n## H2\n### H3\n#### H4\n##### H5\n###### H6\n";
    const auto        spans = mode.highlight(text);

    // Marker itself is dimmed (MarkupMarker), but the whole node (marker +
    // text + trailing newline) gets the cyclic heading span appended after,
    // so it wins per the "later capture wins" overlap rule -- matching
    // Org's own whole-headline-line convention.
    REQUIRE(HasSpan(spans, 0, 1, SyntaxClass::MarkupMarker));   // "#"
    REQUIRE(HasSpan(spans, 0, 5, SyntaxClass::HeadlineLevel1)); // "# H1\n"
    REQUIRE(HasSpan(spans, 5, 11, SyntaxClass::HeadlineLevel2)); // "## H2\n"
    REQUIRE(HasSpan(spans, 11, 18, SyntaxClass::HeadlineLevel3)); // "### H3\n"
    REQUIRE(HasSpan(spans, 18, 26, SyntaxClass::HeadlineLevel1)); // "#### H4\n" -- cycles back
    REQUIRE(HasSpan(spans, 26, 35, SyntaxClass::HeadlineLevel2)); // "##### H5\n"
    REQUIRE(HasSpan(spans, 35, 45, SyntaxClass::HeadlineLevel3)); // "###### H6\n"
}

TEST_CASE("MarkdownMode setext headings resolve level from the underline style", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "Title1\n======\n\nTitle2\n------\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, 14, SyntaxClass::HeadlineLevel1)); // "Title1\n======\n"
    REQUIRE(HasSpan(spans, 15, 29, SyntaxClass::HeadlineLevel2)); // "Title2\n------\n"
}

TEST_CASE("MarkdownMode inline formatting: bold/italic/code span/strikethrough", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "a **bold** b *italic* c `code` d ~~strike~~ e\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 2, 10, SyntaxClass::Strong));       // "**bold**"
    REQUIRE(HasSpan(spans, 13, 21, SyntaxClass::Emphasis));    // "*italic*"
    REQUIRE(HasSpan(spans, 24, 30, SyntaxClass::String));      // "`code`" -- reuses String, see CaptureTable
    REQUIRE(HasSpan(spans, 33, 43, SyntaxClass::Strikethrough)); // "~~strike~~" -- Ned's own addition, see Mode.cpp
    // Emphasis delimiters are dimmed via Punctuation, not swallowed into
    // the Strong/Emphasis span -- e.g. the opening "**".
    REQUIRE(HasSpan(spans, 2, 3, SyntaxClass::Punctuation));
    REQUIRE(HasSpan(spans, 3, 4, SyntaxClass::Punctuation));
}

TEST_CASE("MarkdownMode links and images resolve to Link", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "[text](http://example.com) and ![alt](img.png)\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 1, 5, SyntaxClass::Link));   // link text
    REQUIRE(HasSpan(spans, 7, 25, SyntaxClass::Link));  // link destination
    REQUIRE(HasSpan(spans, 33, 36, SyntaxClass::Link)); // image alt text
    REQUIRE(HasSpan(spans, 38, 45, SyntaxClass::Link)); // image destination
    // The brackets/parens/bang are dimmed via Punctuation, not left Default.
    REQUIRE(HasSpan(spans, 0, 1, SyntaxClass::Punctuation)); // "["
    REQUIRE(HasSpan(spans, 31, 32, SyntaxClass::Punctuation)); // "!"
}

TEST_CASE("MarkdownMode list/blockquote/thematic-break markers are dimmed via MarkupMarker", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "- item\n> quote\n---\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, 2, SyntaxClass::MarkupMarker));  // "- "
    REQUIRE(HasSpan(spans, 7, 9, SyntaxClass::MarkupMarker));  // "> "
    REQUIRE(HasSpan(spans, 15, 19, SyntaxClass::MarkupMarker)); // "---\n"
}

TEST_CASE("MarkdownMode GFM task-list checkboxes resolve to Checkbox", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "- [ ] todo\n- [x] done\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 2, 5, SyntaxClass::Checkbox));  // "[ ]"
    REQUIRE(HasSpan(spans, 13, 16, SyntaxClass::Checkbox)); // "[x]"
}

TEST_CASE("MarkdownMode fenced code block content doesn't get clobbered by the upstream @none capture", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "```\ncode here\n```\n";
    const auto        spans = mode.highlight(text);

    // The whole fenced_code_block (delimiters + content) is text.literal ->
    // String. Without the IsHighlightableCapture("none") fix,
    // code_fence_content's own @none capture would produce a spurious
    // Default span over [4, 14) that clobbers this one, per "later capture
    // wins" -- since code_fence_content is a child node, its capture is
    // collected after the parent's in query-cursor order.
    REQUIRE(HasSpan(spans, 0, 18, SyntaxClass::String));
    for (const HighlightSpan& span : spans) {
        REQUIRE_FALSE(span.syntaxClass == SyntaxClass::Default);
    }
}

TEST_CASE("MarkdownMode backslash escapes resolve to StringEscape", "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "a \\* b\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 2, 4, SyntaxClass::StringEscape)); // "\*"
}

TEST_CASE("MarkdownMode a bold word inside a heading keeps its bold weight, overriding the heading's own tint",
          "[Markdown]") {
    const auto        mode  = MarkdownMode();
    const std::string text  = "# a **bold** heading\n";
    const auto        spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, 21, SyntaxClass::HeadlineLevel1)); // whole line
    REQUIRE(HasSpan(spans, 4, 12, SyntaxClass::Strong));         // "**bold**" wins locally, appended later
}
