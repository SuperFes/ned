#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatEdit.h"
#include "Editor/FormatRewrite.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeRewriteEdits;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::JavaScriptMode;
using ned::editor::Mode;
using ned::editor::QuoteStyle;
using ned::editor::SetRewriteQuoteStyle;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetRewriteQuoteStyle("rewrite.quote", std::nullopt);
    }
};

std::vector<FormatCapture> CapturesNamed(const std::vector<FormatCapture>& captures, std::string_view name) {
    std::vector<FormatCapture> result;
    for (const FormatCapture& c : captures) {
        if (c.name == name) {
            result.push_back(c);
        }
    }
    return result;
}

} // namespace

TEST_CASE("javascript-mode's format.janet names rewrite.quote on a string literal, never on a "
          "template literal",
          "[FormatRewrite]") {
    const Mode mode = JavaScriptMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("const x = 'a';\n"), "rewrite.quote").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const x = `a`;\n"), "rewrite.quote").empty());
}

TEST_CASE("ComputeRewriteEdits does nothing when unconfigured", "[FormatRewrite]") {
    const Mode        mode   = JavaScriptMode();
    const std::string source = "const x = 'a';\n";
    REQUIRE(ComputeRewriteEdits(source, "javascript", mode.formatCaptures(source)).empty());
}

TEST_CASE("End to end: single quotes rewrite to double", "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Double);

    const Mode mode = JavaScriptMode();
    Buffer     buffer("t.js");
    buffer.InsertAtPoint("const x = 'hello';\n");
    ApplyFormatTextEdits(buffer, ComputeRewriteEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "const x = \"hello\";\n");
}

TEST_CASE("End to end: double quotes rewrite to single", "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Single);

    const Mode mode = JavaScriptMode();
    Buffer     buffer("t.js");
    buffer.InsertAtPoint("const x = \"hello\";\n");
    ApplyFormatTextEdits(buffer, ComputeRewriteEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "const x = 'hello';\n");
}

TEST_CASE("End to end: idempotent -- a string already in the target style is left untouched",
          "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Double);

    const Mode        mode   = JavaScriptMode();
    const std::string source = "const x = \"hello\";\n";
    REQUIRE(ComputeRewriteEdits(source, "javascript", mode.formatCaptures(source)).empty());
}

TEST_CASE("Declined: an unescaped target quote inside the string would need escaping",
          "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Double);

    const Mode        mode   = JavaScriptMode();
    const std::string source = "const x = 'say \"hi\"';\n";
    REQUIRE(ComputeRewriteEdits(source, "javascript", mode.formatCaptures(source)).empty());
}

TEST_CASE("Declined: any backslash in the interior is left alone rather than re-escaped",
          "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Double);

    const Mode        mode   = JavaScriptMode();
    const std::string source = "const x = 'line\\nbreak';\n";
    REQUIRE(ComputeRewriteEdits(source, "javascript", mode.formatCaptures(source)).empty());
}

TEST_CASE("RewriteRuleFor(name, language) resolves the language-scoped key first, matching every "
          "other rule kind's own precedent",
          "[FormatRewrite]") {
    struct Guard {
        ~Guard() {
            SetRewriteQuoteStyle("rewrite.quote", std::nullopt);
            SetRewriteQuoteStyle("javascript/rewrite.quote", std::nullopt);
        }
    } guard;

    SetRewriteQuoteStyle("rewrite.quote", QuoteStyle::Double);
    SetRewriteQuoteStyle("javascript/rewrite.quote", QuoteStyle::Single);

    REQUIRE(ned::editor::RewriteRuleFor("rewrite.quote", "javascript").quoteStyle == QuoteStyle::Single);
    REQUIRE(ned::editor::RewriteRuleFor("rewrite.quote", "python").quoteStyle == QuoteStyle::Double);
}
