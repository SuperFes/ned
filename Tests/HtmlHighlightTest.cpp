#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Mode.h"

using ned::editor::HighlightSpan;
using ned::editor::HtmlMode;
using ned::editor::SyntaxClass;

namespace {

bool HasSpanContaining(const std::vector<HighlightSpan>& spans, std::size_t offset, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= offset && offset < span.endByte && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("HtmlMode has a highlighting hook installed", "[Html]") {
    const auto mode = HtmlMode();
    REQUIRE(mode.name == "html-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("HtmlMode <script> content gets real JavaScript highlighting", "[Html]") {
    const auto        mode  = HtmlMode();
    const std::string text  = "<html><script>function greet() { return 1; }</script></html>";
    const auto        spans = mode.highlight(text);

    const std::size_t functionOffset = text.find("function");
    REQUIRE(functionOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, functionOffset, SyntaxClass::Keyword));

    const std::size_t returnOffset = text.find("return");
    REQUIRE(returnOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, returnOffset, SyntaxClass::Keyword));
}

TEST_CASE("HtmlMode <style> content gets real CSS highlighting", "[Html]") {
    const auto        mode  = HtmlMode();
    const std::string text  = "<html><style>body { color: red; }</style></html>";
    const auto        spans = mode.highlight(text);

    const std::size_t bodyOffset = text.find("body");
    REQUIRE(bodyOffset != std::string::npos);
    bool sawTagSelector = false;
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= bodyOffset && bodyOffset < span.endByte && span.syntaxClass != SyntaxClass::Default) {
            sawTagSelector = true;
        }
    }
    REQUIRE(sawTagSelector);
}

TEST_CASE("HtmlMode ordinary markup outside <script>/<style> is unaffected by injection", "[Html]") {
    const auto        mode  = HtmlMode();
    const std::string text  = "<div class=\"a\">hello</div>";
    const auto        spans = mode.highlight(text);

    // Just a basic sanity check that plain markup still highlights (tag
    // name/attribute) the same way it always has -- injection only ever
    // adds spans inside <script>/<style>, never touches ordinary markup.
    const std::size_t divOffset = text.find("div");
    REQUIRE(divOffset != std::string::npos);
    bool sawSomething = false;
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= divOffset && divOffset < span.endByte) {
            sawSomething = true;
        }
    }
    REQUIRE(sawSomething);
}
