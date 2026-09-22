#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/InlineDebugValues.h"

using ned::editor::InlineDebugValue;
using ned::editor::InlineDebugValueLine;
using ned::editor::InlineDebugValueTier;
using ned::editor::LocalCapture;
using ned::editor::LocalCaptureKind;
using ned::editor::ResolveInlineDebugValues;

namespace {

// Offsets are searched for rather than hand-counted, the same reason
// LocalScopesTest.cpp does it: these tests are about which line gets which
// value, and a miscounted offset would fail them for the wrong reason.
std::size_t Nth(std::string_view text, std::string_view needle, int n) {
    std::size_t at = 0;
    for (int i = 0; i <= n; ++i) {
        at = text.find(needle, i == 0 ? 0 : at + 1);
        REQUIRE(at != std::string_view::npos);
    }
    return at;
}

LocalCapture Definition(std::string_view text, std::string_view name, int n = 0) {
    const std::size_t at = Nth(text, name, n);
    return LocalCapture{at, at + name.size(), LocalCaptureKind::Definition, {}};
}

LocalCapture Reference(std::string_view text, std::string_view name, int n = 0) {
    const std::size_t at = Nth(text, name, n);
    return LocalCapture{at, at + name.size(), LocalCaptureKind::Reference, {}};
}

// The scope running from `from` to the '}' matching the next '{' after it --
// a function's scope has to cover its parameter list, not just its body.
LocalCapture ScopeFrom(std::string_view text, std::string_view from) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string_view::npos);
    const std::size_t open = text.find('{', at);
    REQUIRE(open != std::string_view::npos);
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        }
        else if (text[i] == '}' && --depth == 0) {
            return LocalCapture{at, i + 1, LocalCaptureKind::Scope, {}};
        }
    }
    FAIL("unbalanced braces in test fixture");
    return LocalCapture{at, text.size(), LocalCaptureKind::Scope, {}};
}

// Every line of `text`, as the resolver wants them: 0-indexed, newline
// excluded. Mirrors what BufferView derives from its own visible rows.
std::vector<InlineDebugValueLine> AllLines(std::string_view text) {
    std::vector<InlineDebugValueLine> lines;
    std::size_t                       start = 0;
    std::size_t                       line  = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end     = newline == std::string_view::npos ? text.size() : newline;
        lines.push_back(InlineDebugValueLine{.line = line, .startByte = start, .endByte = end});
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
        ++line;
    }
    return lines;
}

std::size_t LineStart(std::string_view text, std::size_t line) {
    std::size_t at = 0;
    for (std::size_t i = 0; i < line; ++i) {
        at = text.find('\n', at) + 1;
    }
    return at;
}

bool Has(const std::vector<InlineDebugValue>& values, std::size_t line, std::string_view name) {
    for (const InlineDebugValue& value : values) {
        if (value.line == line && value.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("The textual tier matches a whole word anywhere on the line", "[InlineDebugValues]") {
    const std::string                        text = "int count = 0;\nint counter = 1;\ntotal += count;\n";
    const std::map<std::string, std::string> locals{{"count", "7"}};

    const std::vector<InlineDebugValue> values = ResolveInlineDebugValues(
        text, {}, InlineDebugValueTier::Textual, locals, /*stopByte=*/0, AllLines(text));

    REQUIRE(Has(values, 0, "count"));
    REQUIRE(Has(values, 2, "count"));
    // "counter" is not a whole-word "count" -- the one thing this tier does get right.
    REQUIRE_FALSE(Has(values, 1, "count"));
}

TEST_CASE("The scoped tier keeps a local off the lines of another function", "[InlineDebugValues]") {
    // The case a whole-word match gets wrong on every screen: `count` in
    // g() is a different variable entirely, and the frame stopped in f()
    // has no business annotating it.
    const std::string               text = "void f() {\n"
                                           "    int count = 1;\n"
                                           "    use(count);\n"
                                           "}\n"
                                           "void g() {\n"
                                           "    int count = 2;\n"
                                           "    use(count);\n"
                                           "}\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),
        Definition(text, "count", 0),
        Reference(text, "count", 1),
        ScopeFrom(text, "void g"),
        Definition(text, "count", 2),
        Reference(text, "count", 3),
    };
    const std::map<std::string, std::string> locals{{"count", "1"}};
    const std::size_t                        stopInF = Nth(text, "use(count)", 0);

    const std::vector<InlineDebugValue> values =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, stopInF, AllLines(text));

    REQUIRE(Has(values, 1, "count"));
    REQUIRE(Has(values, 2, "count"));
    REQUIRE_FALSE(Has(values, 5, "count"));
    REQUIRE_FALSE(Has(values, 6, "count"));

    // Same text, same captures, stopped in g() instead: exactly the other
    // two lines, which is the proof that the scope is doing the work and
    // not some property of where the lines sit.
    const std::size_t                   stopInG = Nth(text, "use(count)", 1);
    const std::vector<InlineDebugValue> inG =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, stopInG, AllLines(text));
    REQUIRE_FALSE(Has(inG, 1, "count"));
    REQUIRE(Has(inG, 5, "count"));
    REQUIRE(Has(inG, 6, "count"));
}

TEST_CASE("The scoped tier ignores a name in a comment, a string or a member access", "[InlineDebugValues]") {
    // None of the three is a locals capture, so none of them is a candidate
    // -- no skip list, no per-language exception.
    const std::string               text = "void f() {\n"
                                           "    int count = 1;\n"
                                           "    // count the things\n"
                                           "    log(\"count\");\n"
                                           "    other.count += 1;\n"
                                           "}\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),
        Definition(text, "count", 0),
    };
    const std::map<std::string, std::string> locals{{"count", "1"}};
    const std::size_t                        stop = Nth(text, "int count", 0);

    const std::vector<InlineDebugValue> values =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, stop, AllLines(text));

    REQUIRE(Has(values, 1, "count"));
    REQUIRE_FALSE(Has(values, 2, "count"));
    REQUIRE_FALSE(Has(values, 3, "count"));
    REQUIRE_FALSE(Has(values, 4, "count"));

    // The textual tier, on the same input, annotates all four -- the
    // difference between the tiers, pinned rather than described.
    const std::vector<InlineDebugValue> textual =
        ResolveInlineDebugValues(text, {}, InlineDebugValueTier::Textual, locals, stop, AllLines(text));
    REQUIRE(Has(textual, 2, "count"));
    REQUIRE(Has(textual, 3, "count"));
    REQUIRE(Has(textual, 4, "count"));
}

TEST_CASE("The scoped tier picks the shadowing binding the stopped frame can see", "[InlineDebugValues]") {
    const std::string               text = "void f() {\n"
                                           "    int x = 1;\n"
                                           "    {\n"
                                           "        int x = 2;\n"
                                           "        use(x);\n"
                                           "    }\n"
                                           "    use(x);\n"
                                           "}\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),
        Definition(text, "x", 0),
        ScopeFrom(text, "    {"),
        Definition(text, "x", 1),
        Reference(text, "x", 2),
        Reference(text, "x", 3),
    };
    const std::map<std::string, std::string> locals{{"x", "2"}};

    // Stopped inside the inner block: the inner binding's two lines only.
    const std::size_t                   inner = Nth(text, "use(x)", 0);
    const std::vector<InlineDebugValue> values =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, inner, AllLines(text));
    REQUIRE(Has(values, 3, "x"));
    REQUIRE(Has(values, 4, "x"));
    REQUIRE_FALSE(Has(values, 6, "x"));

    // The outer binding's scope encloses the inner one, so stopping outside
    // the block still sees the outer `x` -- and not the shadow.
    const std::size_t                   outer = Nth(text, "use(x)", 1);
    const std::vector<InlineDebugValue> outerValues =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, outer, AllLines(text));
    REQUIRE(Has(outerValues, 1, "x"));
    REQUIRE(Has(outerValues, 6, "x"));
    REQUIRE_FALSE(Has(outerValues, 3, "x"));
}

TEST_CASE("A file-level binding is visible from anywhere in the file", "[InlineDebugValues]") {
    const std::string               text = "int total = 0;\n"
                                           "void f() {\n"
                                           "    total += 1;\n"
                                           "}\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),
        Definition(text, "total", 0),
        Reference(text, "total", 1),
    };
    const std::map<std::string, std::string> locals{{"total", "3"}};
    const std::size_t                        stop = Nth(text, "total += 1", 0);

    const std::vector<InlineDebugValue> values =
        ResolveInlineDebugValues(text, captures, InlineDebugValueTier::Scoped, locals, stop, AllLines(text));
    REQUIRE(Has(values, 0, "total"));
    REQUIRE(Has(values, 2, "total"));
}

TEST_CASE("Annotations are capped per line and ordered by line then name", "[InlineDebugValues]") {
    const std::string                        text = "a b c d\n";
    const std::map<std::string, std::string> locals{{"a", "1"}, {"b", "2"}, {"c", "3"}, {"d", "4"}};

    const std::vector<InlineDebugValue> values = ResolveInlineDebugValues(
        text, {}, InlineDebugValueTier::Textual, locals, /*stopByte=*/0, AllLines(text), /*maxPerLine=*/3);

    REQUIRE(values.size() == 3);
    REQUIRE(values[0].name == "a");
    REQUIRE(values[1].name == "b");
    REQUIRE(values[2].name == "c");

    const std::vector<InlineDebugValue> none = ResolveInlineDebugValues(
        text, {}, InlineDebugValueTier::Textual, locals, /*stopByte=*/0, AllLines(text), /*maxPerLine=*/0);
    REQUIRE(none.empty());
}

TEST_CASE("The scoped tier reports nothing for a mode whose locals query found nothing", "[InlineDebugValues]") {
    // Precision over recall, deliberately: an unresolvable buffer draws no
    // annotation rather than falling back to a textual guess, because a
    // mode WITH a locals query that captured nothing here has said there is
    // no binding to speak of. The textual tier is chosen per buffer, by the
    // caller, for the modes that have no query at all.
    const std::string                        text = "count = count + 1\n";
    const std::map<std::string, std::string> locals{{"count", "7"}};

    const std::vector<InlineDebugValue> values = ResolveInlineDebugValues(
        text, {}, InlineDebugValueTier::Scoped, locals, /*stopByte=*/0, AllLines(text));
    REQUIRE(values.empty());
}

TEST_CASE("Nothing is resolved without a stop's values or a line to draw on", "[InlineDebugValues]") {
    const std::string text = "int count = 0;\n";
    REQUIRE(ResolveInlineDebugValues(text, {}, InlineDebugValueTier::Textual, {}, 0, AllLines(text)).empty());

    const std::map<std::string, std::string> locals{{"count", "7"}};
    REQUIRE(ResolveInlineDebugValues(text, {}, InlineDebugValueTier::Textual, locals, 0, {}).empty());

    // A line range past the end of the text is skipped, not read -- a
    // stale viewport against a shortened buffer must not index out of it.
    const std::vector<InlineDebugValueLine> stale{
        InlineDebugValueLine{.line = 0, .startByte = 0, .endByte = text.size() + 64}};
    REQUIRE(ResolveInlineDebugValues(text, {}, InlineDebugValueTier::Textual, locals, 0, stale).empty());
}

TEST_CASE("Only the lines asked about are answered for", "[InlineDebugValues]") {
    // The viewport case: BufferView hands over the visible rows only, and
    // a line outside them is not annotated even though it mentions the local.
    const std::string                        text = "int count = 1;\nuse(count);\nuse(count);\n";
    const std::map<std::string, std::string> locals{{"count", "1"}};

    const std::vector<InlineDebugValueLine> visible{
        InlineDebugValueLine{.line = 1, .startByte = LineStart(text, 1), .endByte = LineStart(text, 2) - 1}};
    const std::vector<InlineDebugValue> values =
        ResolveInlineDebugValues(text, {}, InlineDebugValueTier::Textual, locals, 0, visible);

    REQUIRE(values.size() == 1);
    REQUIRE(values[0].line == 1);
}
