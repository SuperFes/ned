#include <catch2/catch_test_macros.hpp>

#include "Editor/Snippet.h"

using ned::editor::ParsedSnippet;
using ned::editor::ParseSnippet;
using ned::editor::SnippetField;

namespace {

bool FieldEquals(const SnippetField& field, int index, std::size_t start, std::size_t end) {
    return field.index == index && field.start == start && field.end == end;
}

} // namespace

TEST_CASE("ParseSnippet passes plain text through with an implicit final stop", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("hello");
    REQUIRE(parsed.text == "hello");
    REQUIRE(parsed.fields.size() == 1);
    REQUIRE(FieldEquals(parsed.fields[0], 0, 5, 5));
}

TEST_CASE("ParseSnippet handles a bare $1 as an empty field", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("a$1b");
    REQUIRE(parsed.text == "ab");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 1, 1));
    REQUIRE(FieldEquals(parsed.fields[1], 0, 2, 2));
}

TEST_CASE("ParseSnippet handles the braced ${1} form", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("a${1}b");
    REQUIRE(parsed.text == "ab");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 1, 1));
}

TEST_CASE("ParseSnippet substitutes a placeholder and records its range", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:ph}");
    REQUIRE(parsed.text == "ph");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 0, 2));
    REQUIRE(FieldEquals(parsed.fields[1], 0, 2, 2));
}

TEST_CASE("ParseSnippet respects an explicit $0 instead of appending one", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("a$0b");
    REQUIRE(parsed.text == "ab");
    REQUIRE(parsed.fields.size() == 1);
    REQUIRE(FieldEquals(parsed.fields[0], 0, 1, 1));
}

TEST_CASE("ParseSnippet supports a $0 with placeholder text", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${0:done}");
    REQUIRE(parsed.text == "done");
    REQUIRE(parsed.fields.size() == 1);
    REQUIRE(FieldEquals(parsed.fields[0], 0, 0, 4));
}

TEST_CASE("ParseSnippet expands the for-loop fixture with mirrors", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("for (${1:i}; $1 < n; ++$1)");
    REQUIRE(parsed.text == "for (i; i < n; ++i)");
    REQUIRE(parsed.fields.size() == 4);
    // Primary first (the placeholder-carrying occurrence), then mirrors in
    // document order, then the implicit final stop.
    REQUIRE(FieldEquals(parsed.fields[0], 1, 5, 6));
    REQUIRE(FieldEquals(parsed.fields[1], 1, 8, 9));
    REQUIRE(FieldEquals(parsed.fields[2], 1, 17, 18));
    REQUIRE(FieldEquals(parsed.fields[3], 0, 19, 19));
}

TEST_CASE("ParseSnippet promotes a later placeholder-carrying occurrence to primary", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("$1 = ${1:x}");
    REQUIRE(parsed.text == "x = x");
    REQUIRE(parsed.fields.size() == 3);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 4, 5)); // the ${1:x} occurrence leads
    REQUIRE(FieldEquals(parsed.fields[1], 1, 0, 1));
    REQUIRE(FieldEquals(parsed.fields[2], 0, 5, 5));
}

TEST_CASE("ParseSnippet orders fields by index ascending with 0 last", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${2:b}${1:a}");
    REQUIRE(parsed.text == "ba");
    REQUIRE(parsed.fields.size() == 3);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 1, 2));
    REQUIRE(FieldEquals(parsed.fields[1], 2, 0, 1));
    REQUIRE(FieldEquals(parsed.fields[2], 0, 2, 2));
}

TEST_CASE("ParseSnippet keeps adjacent fields distinct", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:a}${2:b}");
    REQUIRE(parsed.text == "ab");
    REQUIRE(parsed.fields.size() == 3);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 0, 1));
    REQUIRE(FieldEquals(parsed.fields[1], 2, 1, 2));
    REQUIRE(FieldEquals(parsed.fields[2], 0, 2, 2));
}

TEST_CASE("ParseSnippet keeps a nested placeholder's text but drops its inner stop", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:foo ${2:bar}}");
    REQUIRE(parsed.text == "foo bar");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 0, 7));
    REQUIRE(FieldEquals(parsed.fields[1], 0, 7, 7));
}

TEST_CASE("ParseSnippet honors dollar and backslash escapes", "[Snippet]") {
    const ParsedSnippet dollar = ParseSnippet("\\$1");
    REQUIRE(dollar.text == "$1");
    REQUIRE(dollar.fields.size() == 1);
    REQUIRE(dollar.fields[0].index == 0);

    const ParsedSnippet backslash = ParseSnippet("a\\\\b");
    REQUIRE(backslash.text == "a\\b");
}

TEST_CASE("ParseSnippet honors a brace escape inside a placeholder", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:a\\}b}");
    REQUIRE(parsed.text == "a}b");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 0, 3));
}

TEST_CASE("ParseSnippet passes ill-formed syntax through as literal text", "[Snippet]") {
    REQUIRE(ParseSnippet("${").text == "${");
    REQUIRE(ParseSnippet("${1:unterminated").text == "${1:unterminated");
    REQUIRE(ParseSnippet("$").text == "$");
    // A genuinely unterminated choice/transform still passes through whole.
    REQUIRE(ParseSnippet("${1|a,b").text == "${1|a,b");
    REQUIRE(ParseSnippet("${1/a/b").text == "${1/a/b");
}

// --- Choices ---------------------------------------------------------------

TEST_CASE("ParseSnippet resolves a choice field to its first option as a placeholder", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1|foo,bar,baz|}");
    REQUIRE(parsed.text == "foo");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 0, 3));
}

TEST_CASE("ParseSnippet honors comma/pipe escapes inside a choice list", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1|a\\,b,c|}");
    REQUIRE(parsed.text == "a,b");
}

// --- $TM_*/other editor-context variables -----------------------------------

TEST_CASE("ParseSnippet resolves a known variable to its supplied value", "[Snippet]") {
    ned::editor::SnippetVariables vars;
    vars.filename              = "main.cpp";
    const ParsedSnippet parsed = ParseSnippet("// $TM_FILENAME", vars);
    REQUIRE(parsed.text == "// main.cpp");
    REQUIRE(parsed.fields.size() == 1); // just the implicit final stop -- variables aren't fields
}

TEST_CASE("ParseSnippet resolves the braced ${VAR} form identically to bare $VAR", "[Snippet]") {
    ned::editor::SnippetVariables vars;
    vars.lineNumber = "42";
    REQUIRE(ParseSnippet("L${TM_LINE_NUMBER}", vars).text == "L42");
}

TEST_CASE("ParseSnippet falls back to a variable's own default when unset", "[Snippet]") {
    REQUIRE(ParseSnippet("${TM_SELECTED_TEXT:fallback}").text == "fallback");
}

TEST_CASE("ParseSnippet inserts an unknown variable's bare name with no default", "[Snippet]") {
    REQUIRE(ParseSnippet("$NOT_A_REAL_VAR").text == "NOT_A_REAL_VAR");
    REQUIRE(ParseSnippet("${NOT_A_REAL_VAR}").text == "NOT_A_REAL_VAR");
}

TEST_CASE("ParseSnippet applies a transform to a variable", "[Snippet]") {
    ned::editor::SnippetVariables vars;
    vars.filenameBase          = "MyClass";
    const ParsedSnippet parsed = ParseSnippet("${TM_FILENAME_BASE/(.*)/${1:/upcase}/}", vars);
    REQUIRE(parsed.text == "MYCLASS");
}

// --- Tabstop transforms (`${N/regex/format/flags}`) -------------------------

TEST_CASE("ParseSnippet applies an upcase transform to a tabstop mirror", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:hello} ${1/(.*)/${1:/upcase}/}");
    REQUIRE(parsed.text == "hello HELLO");
}

TEST_CASE("ParseSnippet transform only replaces the first match without the g flag", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:aXaXa} ${1/a/b/}");
    REQUIRE(parsed.text == "aXaXa bXaXa");
}

TEST_CASE("ParseSnippet transform replaces every match with the g flag", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("${1:aXaXa} ${1/a/b/g}");
    REQUIRE(parsed.text == "aXaXa bXbXb");
}

TEST_CASE("ParseSnippet transform's :+/:-/bare conditional forms follow group participation", "[Snippet]") {
    // (a)? against "b" leaves group 1 unmatched.
    REQUIRE(ParseSnippet("${1:b} ${1/(a)?(b)/${1:+yes}/}").text == "b ");
    REQUIRE(ParseSnippet("${1:b} ${1/(a)?(b)/${1:-no}/}").text == "b no");
    REQUIRE(ParseSnippet("${1:b} ${1/(a)?(b)/${1:no}/}").text == "b no");
    REQUIRE(ParseSnippet("${1:ab} ${1/(a)?(b)/${1:+yes}/}").text == "ab yes");
}

TEST_CASE("ParseSnippet treats a plain-dollar-digits run as a real stop", "[Snippet]") {
    // "$5.00" above parses "$5" as a stop -- verify the documented behavior
    // explicitly: the digits are consumed, the rest stays literal.
    const ParsedSnippet parsed = ParseSnippet("$5.00");
    REQUIRE(parsed.text == ".00");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 5, 0, 0));
}

TEST_CASE("ParseSnippet keeps byte offsets exact through multibyte placeholder text", "[Snippet]") {
    const ParsedSnippet parsed = ParseSnippet("x${1:héllo}y");
    REQUIRE(parsed.text == "xhélloy");
    REQUIRE(parsed.fields.size() == 2);
    REQUIRE(FieldEquals(parsed.fields[0], 1, 1, 7)); // "héllo" is 6 bytes
}

// --- SnippetSession (the Buffer-driving half) ----------------------------

#include "Text/Buffer.h"

using ned::editor::SnippetSession;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("SnippetSession::Start expands the trigger as one undo step and activates field 1", "[Snippet]") {
    Buffer buffer("scratch", Rope("for"));
    buffer.SetPoint(3);
    auto session = SnippetSession::Start(buffer, "scratch", 0, 3, ParseSnippet("for (${1:i}; $1 < n; ++$1)"));
    REQUIRE(session.has_value());
    REQUIRE(buffer.Text() == "for (i; i < n; ++i)");
    REQUIRE(buffer.Point() == 6);
    REQUIRE(session->Pristine());

    const auto& ranges = buffer.SnippetRanges();
    REQUIRE(ranges.size() == 4);
    REQUIRE(ranges[0] == Buffer::SnippetRange{1, 1, 5, 6, true});
    REQUIRE(ranges[1] == Buffer::SnippetRange{2, 1, 8, 9, false});
    REQUIRE(ranges[2] == Buffer::SnippetRange{3, 1, 17, 18, false});
    REQUIRE(ranges[3] == Buffer::SnippetRange{4, 0, 19, 19, false});
    REQUIRE(session->ActiveFieldRange(buffer) == std::make_pair(std::size_t{5}, std::size_t{6}));

    buffer.Undo();
    REQUIRE(buffer.Text() == "for");
    REQUIRE_FALSE(session->RangesValid(buffer));
}

TEST_CASE("SnippetSession::Start with only a final stop inserts and declines a session", "[Snippet]") {
    Buffer buffer("scratch", Rope("td"));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 2, ParseSnippet("TODO: $0 (review)"));
    REQUIRE_FALSE(session.has_value());
    REQUIRE(buffer.Text() == "TODO:  (review)");
    REQUIRE(buffer.Point() == 6);
}

TEST_CASE("SnippetSession typing propagates to mirrors keystroke by keystroke", "[Snippet]") {
    Buffer buffer("scratch", Rope("for"));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 3, ParseSnippet("for (${1:i}; $1 < n; ++$1)"));
    REQUIRE(session.has_value());

    // "i" on a pristine field: placeholder deleted first, then the
    // keystroke, then the sync -- the same shape BufferView's per-keystroke
    // undo-group hook drives.
    buffer.BeginUndoGroup();
    session->DeleteActiveFieldContent(buffer);
    session->ClearPristine();
    buffer.InsertAtPoint("i");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "for (i; i < n; ++i)");
    REQUIRE(buffer.Point() == 6);

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("d");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "for (id; id < n; ++id)");
    REQUIRE(buffer.Point() == 7);
    REQUIRE(buffer.SnippetRanges()[0] == Buffer::SnippetRange{1, 1, 5, 7, true});
    REQUIRE(buffer.SnippetRanges()[1] == Buffer::SnippetRange{2, 1, 9, 11, false});
    REQUIRE(buffer.SnippetRanges()[2] == Buffer::SnippetRange{3, 1, 19, 21, false});
    REQUIRE(buffer.SnippetRanges()[3] == Buffer::SnippetRange{4, 0, 22, 22, false});

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("x");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "for (idx; idx < n; ++idx)");
    REQUIRE(buffer.Point() == 8);

    // One undo step reverts the last keystroke and its mirror sync together.
    buffer.Undo();
    REQUIRE(buffer.Text() == "for (id; id < n; ++id)");
    buffer.Undo();
    REQUIRE(buffer.Text() == "for (i; i < n; ++i)"); // the "i" keystroke undone (same text: it replaced the placeholder)
    buffer.Undo();
    REQUIRE(buffer.Text() == "for (i; i < n; ++i)"); // back to the expansion node itself
    buffer.Undo();
    REQUIRE(buffer.Text() == "for");
}

TEST_CASE("SnippetSession navigation hops fields and finishes at the final stop", "[Snippet]") {
    Buffer buffer("scratch", Rope(""));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 0, ParseSnippet("${1:a} ${2:b} $0 tail"));
    REQUIRE(session.has_value());
    REQUIRE(buffer.Text() == "a b  tail");
    REQUIRE(buffer.Point() == 1);

    REQUIRE(session->NextField(buffer) == SnippetSession::NavResult::Moved);
    REQUIRE(buffer.Point() == 3);
    REQUIRE(session->Pristine());
    REQUIRE(session->ActiveFieldRange(buffer) == std::make_pair(std::size_t{2}, std::size_t{3}));

    REQUIRE(session->PreviousField(buffer) == SnippetSession::NavResult::Moved);
    REQUIRE(buffer.Point() == 1);
    REQUIRE(session->PreviousField(buffer) == SnippetSession::NavResult::Moved); // stays at the first
    REQUIRE(buffer.Point() == 1);

    session->NextField(buffer);
    REQUIRE(session->NextField(buffer) == SnippetSession::NavResult::Finished);
    REQUIRE(buffer.Point() == 4); // the explicit $0
    session->Finish(buffer);
    REQUIRE(buffer.SnippetRanges().empty());
}

TEST_CASE("SnippetSession keeps an emptied field navigable and refillable", "[Snippet]") {
    Buffer buffer("scratch", Rope(""));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 0, ParseSnippet("${1:abc}-$1"));
    REQUIRE(session.has_value());
    REQUIRE(buffer.Text() == "abc-abc");

    // Backspace on the pristine placeholder: delete + sync, consumed.
    buffer.BeginUndoGroup();
    session->DeleteActiveFieldContent(buffer);
    session->ClearPristine();
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "-");
    REQUIRE(buffer.SnippetRanges()[0] == Buffer::SnippetRange{1, 1, 0, 0, true});
    REQUIRE(buffer.SnippetRanges()[1] == Buffer::SnippetRange{2, 1, 1, 1, false});
    REQUIRE(session->RangesValid(buffer));

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("z");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "z-z");
    REQUIRE(buffer.Point() == 1);
}

TEST_CASE("SnippetSession sync keeps adjacent mirrors and point coherent", "[Snippet]") {
    Buffer buffer("scratch", Rope(""));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 0, ParseSnippet("${1:a}$1"));
    REQUIRE(session.has_value());
    REQUIRE(buffer.Text() == "aa");

    buffer.BeginUndoGroup();
    session->DeleteActiveFieldContent(buffer);
    session->ClearPristine();
    buffer.InsertAtPoint("b");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "bb");
    REQUIRE(buffer.SnippetRanges()[0] == Buffer::SnippetRange{1, 1, 0, 1, true});
    REQUIRE(buffer.SnippetRanges()[1] == Buffer::SnippetRange{2, 1, 1, 2, false});
    REQUIRE(buffer.Point() == 1); // point stays at the active field's end, not the mirror's

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("c");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "bcbc");
    REQUIRE(buffer.SnippetRanges()[0] == Buffer::SnippetRange{1, 1, 0, 2, true});
    REQUIRE(buffer.SnippetRanges()[1] == Buffer::SnippetRange{2, 1, 2, 4, false});
    REQUIRE(buffer.Point() == 2);
}

TEST_CASE("SnippetSession sync propagates from a mirror ahead of the active field", "[Snippet]") {
    // The primary (placeholder-carrying) occurrence is second in the
    // document -- the mirror sits before the active field, so its rewrite
    // shifts the active field and point.
    Buffer buffer("scratch", Rope(""));
    auto   session = SnippetSession::Start(buffer, "scratch", 0, 0, ParseSnippet("$1 = ${1:x};"));
    REQUIRE(session.has_value());
    REQUIRE(buffer.Text() == "x = x;");
    REQUIRE(buffer.Point() == 5); // the primary's end

    buffer.BeginUndoGroup();
    session->DeleteActiveFieldContent(buffer);
    session->ClearPristine();
    buffer.InsertAtPoint("total");
    session->SyncMirrors(buffer);
    buffer.EndUndoGroup();
    REQUIRE(buffer.Text() == "total = total;");
    REQUIRE(buffer.Point() == 13); // still at the active field's end
    REQUIRE(session->ActiveFieldRange(buffer) == std::make_pair(std::size_t{8}, std::size_t{13}));
}
