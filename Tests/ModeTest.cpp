#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "Editor/Mode.h"

using ned::editor::FundamentalMode;
using ned::editor::HighlightSpan;
using ned::editor::JanetMode;
using ned::editor::JsonMode;
using ned::editor::Keymap;
using ned::editor::ParseKeySequence;
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

TEST_CASE("FundamentalMode has no keybindings and no highlighting", "[Mode]") {
    const auto mode = FundamentalMode();

    REQUIRE(mode.name == "fundamental-mode");
    REQUIRE_FALSE(static_cast<bool>(mode.highlight));
    REQUIRE(mode.keymap.Resolve(ParseKeySequence("C-c")).result == Keymap::LookupResult::NoMatch);
}

TEST_CASE("JanetMode has a highlighting hook installed", "[Mode]") {
    const auto mode = JanetMode();
    REQUIRE(mode.name == "janet-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("JanetMode highlights only the numeric literals in plain code, symbols stay Default", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("(+ 1 2)");

    // `+`, the parens, and the spaces are all Default -- no span covers them.
    // Only the two num_lit tokens get a real capture.
    REQUIRE(HasSpan(spans, 3, 4, SyntaxClass::Number)); // `1`
    REQUIRE(HasSpan(spans, 5, 6, SyntaxClass::Number)); // `2`
    REQUIRE(spans.size() == 2);
}

TEST_CASE("JanetMode highlights a full-line comment as entirely Comment", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("# this is a comment");

    REQUIRE(HasSpan(spans, 0, 19, SyntaxClass::Comment));
}

TEST_CASE("JanetMode highlights a string literal, code around it stays Default", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"((print "hi"))";
    const auto             spans = mode.highlight(line);

    // (print␣  -> Default (7 bytes: '(', p, r, i, n, t, ' ') -- no span
    // "hi" -> String (4 bytes: '"', h, i, '"'), starting at byte 7
    REQUIRE(HasSpan(spans, 7, 11, SyntaxClass::String));
    // trailing ')' at byte 11 stays Default -- no span covers it
    for (const HighlightSpan& span : spans) {
        const bool coversTrailingParen = (span.startByte <= 11 && 11 < span.endByte);
        REQUIRE_FALSE(coversTrailingParen);
    }
}

TEST_CASE("JanetMode treats a backslash-escaped quote as staying inside the string", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"("a\"b")"; // "a\"b" -- 6 bytes: " a \ " b "
    const auto             spans = mode.highlight(line);

    REQUIRE(HasSpan(spans, 0, line.size(), SyntaxClass::String)); // the whole thing is one string literal
}

TEST_CASE("JanetMode switches from string to comment correctly on the same line", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"("str" # comment)";
    const auto             spans = mode.highlight(line);

    REQUIRE(HasSpan(spans, 0, 5, SyntaxClass::String));   // `"str"`
    REQUIRE(HasSpan(spans, 6, 15, SyntaxClass::Comment)); // `# comment`
}

TEST_CASE("JanetMode's spans use byte offsets, correctly spanning a multi-byte codepoint", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("# caf\xC3\xA9"); // "# café" -- 'é' is 2 bytes

    REQUIRE(HasSpan(spans, 0, 7, SyntaxClass::Comment)); // 7 bytes total, not 6 codepoints
}

TEST_CASE("JanetMode highlights a long string literal as one continuous span across a newline", "[Mode]") {
    // The real grammar sees the whole buffer at once, unlike the old
    // hand-rolled per-line scanner it replaced (which reset its state at
    // every '\n' and fundamentally could not represent a construct spanning
    // multiple lines -- see HighlightSpan's own doc comment). A Janet
    // backtick-delimited long string is exactly that construct.
    const auto             mode  = JanetMode();
    const std::string_view text  = "`line1\nline2`";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, text.size(), SyntaxClass::String));
}

TEST_CASE("JanetMode does not classify an unterminated string literal", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("\"unterminated\nplain code");

    REQUIRE(spans.empty()); // no valid str_lit node for the parser to capture
}

TEST_CASE("JsonMode has a highlighting hook installed", "[Mode]") {
    const auto mode = JsonMode();
    REQUIRE(mode.name == "json-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("JsonMode highlights strings, numbers, and literal keywords via a real tree-sitter parse", "[Mode]") {
    const auto             mode  = JsonMode();
    const std::string_view text  = R"({"a": 1, "b": true, "c": null})";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 1, 4, SyntaxClass::String));            // "a"
    REQUIRE(HasSpan(spans, 6, 7, SyntaxClass::Number));            // 1
    REQUIRE(HasSpan(spans, 9, 12, SyntaxClass::String));           // "b"
    REQUIRE(HasSpan(spans, 14, 18, SyntaxClass::ConstantBuiltin)); // true
    REQUIRE(HasSpan(spans, 20, 23, SyntaxClass::String));          // "c"
    REQUIRE(HasSpan(spans, 25, 29, SyntaxClass::ConstantBuiltin)); // null
}

TEST_CASE("JsonMode highlights nothing for a JSON value with no strings/numbers/literals", "[Mode]") {
    const auto mode  = JsonMode();
    const auto spans = mode.highlight("{}");

    REQUIRE(spans.empty());
}
