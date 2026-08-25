#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Mode.h"

using ned::editor::HighlightSpan;
using ned::editor::OrgMode;
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

TEST_CASE("OrgMode #+BEGIN_SRC block gets real per-language highlighting", "[Org]") {
    const auto        mode  = OrgMode();
    const std::string text  = "#+begin_src python\ndef f():\n    return 1\n#+end_src\n";
    const auto        spans = mode.highlight(text);

    const std::size_t defOffset = text.find("def");
    REQUIRE(defOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, defOffset, SyntaxClass::Keyword));

    const std::size_t returnOffset = text.find("return");
    REQUIRE(returnOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, returnOffset, SyntaxClass::Keyword));
}

TEST_CASE("OrgMode #+BEGIN_SRC picks the language from the first parameter, ignoring header args", "[Org]") {
    const auto        mode  = OrgMode();
    const std::string text  = "#+begin_src python :results output\ndef f():\n    pass\n#+end_src\n";
    const auto        spans = mode.highlight(text);

    const std::size_t defOffset = text.find("def");
    REQUIRE(defOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, defOffset, SyntaxClass::Keyword));
}

TEST_CASE("OrgMode #+BEGIN_EXPORT block gets real backend highlighting", "[Org]") {
    const auto        mode  = OrgMode();
    const std::string text  = "#+begin_export html\n<p>hi</p>\n#+end_export\n";
    const auto        spans = mode.highlight(text);

    const std::size_t pOffset = text.find("p>hi");
    REQUIRE(pOffset != std::string::npos);
    bool sawSomething = false;
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= pOffset && pOffset < span.endByte && span.syntaxClass != SyntaxClass::Default) {
            sawSomething = true;
        }
    }
    REQUIRE(sawSomething);
}

TEST_CASE("OrgMode #+BEGIN_QUOTE (no parameter, no language) doesn't crash and adds no injected spans", "[Org]") {
    const auto        mode  = OrgMode();
    const std::string text  = "#+begin_quote\nsome quoted text\n#+end_quote\n";
    const auto        spans = mode.highlight(text);

    const std::size_t textOffset = text.find("some quoted text");
    REQUIRE(textOffset != std::string::npos);
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= textOffset && textOffset < span.endByte) {
            REQUIRE_FALSE(span.syntaxClass == SyntaxClass::Keyword);
        }
    }
}
