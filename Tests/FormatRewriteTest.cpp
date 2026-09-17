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
using ned::editor::PhpMode;
using ned::editor::QuoteStyle;
using ned::editor::SetRewriteExpandElseif;
using ned::editor::SetRewriteQuoteStyle;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetRewriteQuoteStyle("rewrite.quote", std::nullopt);
        SetRewriteExpandElseif("rewrite.elseif", std::nullopt);
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

// rewrite-kind widening: a second, structurally different rewrite family --
// see php/format.janet's own header comment for rewrite.elseif.
TEST_CASE("php-mode's format.janet names rewrite.elseif spanning exactly the \"elseif\" "
          "keyword token, across the brace and colon-alternate body forms alike",
          "[FormatRewrite]") {
    const Mode mode = PhpMode();

    const std::string braceChain = "<?php\nif ($x) {\n} elseif ($y) {\n}\n";
    const auto        brace      = CapturesNamed(mode.formatCaptures(braceChain), "rewrite.elseif");
    REQUIRE(brace.size() == 1);
    REQUIRE(braceChain.substr(brace[0].startByte, brace[0].endByte - brace[0].startByte) == "elseif");

    const std::string colonChain = "<?php\nif ($x):\nelseif ($y):\nendif;\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(colonChain), "rewrite.elseif").size() == 1);
}

TEST_CASE("ComputeRewriteEdits does nothing for rewrite.elseif when unconfigured", "[FormatRewrite]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} elseif ($y) {\n}\n";
    REQUIRE(ComputeRewriteEdits(source, "php", mode.formatCaptures(source)).empty());
}

TEST_CASE("End to end: \"elseif\" rewrites to \"else if\"", "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteExpandElseif("rewrite.elseif", true);

    const Mode mode = PhpMode();
    Buffer     buffer("t.php");
    buffer.InsertAtPoint("<?php\nif ($x) {\n} elseif ($y) {\n}\n");
    ApplyFormatTextEdits(buffer, ComputeRewriteEdits(buffer.Text(), "php", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "<?php\nif ($x) {\n} else if ($y) {\n}\n");
}

TEST_CASE("End to end: rewriting \"elseif\" is idempotent -- a re-parse of the result names no "
          "more rewrite.elseif captures at all (a plain else + if, not an else_if_clause)",
          "[FormatRewrite]") {
    const FormatRulesGuard guard;
    SetRewriteExpandElseif("rewrite.elseif", true);

    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} else if ($y) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "rewrite.elseif").empty());
    REQUIRE(ComputeRewriteEdits(source, "php", mode.formatCaptures(source)).empty());
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
