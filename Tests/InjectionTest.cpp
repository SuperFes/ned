#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Injection.h"
#include "Editor/Mode.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

using namespace ned::editor;
using namespace ned::editor::treesitter;

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

TEST_CASE("ResolveEmbeddedLanguageHighlight resolves a real bundled Mode via ModeByName", "[Injection]") {
    EmbeddedLanguageCache    cache;
    const HighlightFunction* highlight = ResolveEmbeddedLanguageHighlight("python", cache);
    REQUIRE(highlight != nullptr);

    bool sawKeyword = false;
    for (const HighlightSpan& span : (*highlight)("def f():\n    pass\n")) {
        if (span.syntaxClass == SyntaxClass::Keyword) {
            sawKeyword = true;
        }
    }
    REQUIRE(sawKeyword);
}

TEST_CASE("ResolveEmbeddedLanguageHighlight caches nullopt for an unresolvable name without crashing on repeat calls",
          "[Injection]") {
    EmbeddedLanguageCache cache;
    REQUIRE(ResolveEmbeddedLanguageHighlight("not-a-real-language", cache) == nullptr);
    REQUIRE(ResolveEmbeddedLanguageHighlight("not-a-real-language", cache) == nullptr);
}

TEST_CASE("CollectInjectedHighlightSpans resolves each match's injected language independently, not scrambled",
          "[Injection]") {
    // A synthetic host query using a JSON document, in the same
    // #set!-driven shape HTML's own real injections.scm uses -- this
    // exercises the generic engine's match-pairing without depending on any
    // specific real host grammar's own node shapes.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "def f(): pass", "b": "x = 1"})";
    Tree              tree = parser.Parse(text);
    Query             injectionQuery(
        language, R"(((pair value: (string (string_content) @injection.content)) (#set! injection.language "python")))");

    EmbeddedLanguageCache      cache;
    std::vector<HighlightSpan> spans;
    CollectInjectedHighlightSpans(tree.RootNode(), text, injectionQuery, cache, spans);

    const std::size_t defOffset = text.find("def");
    REQUIRE(defOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, defOffset, SyntaxClass::Keyword));

    // The second fragment ("x = 1") has no Python keyword -- confirm no
    // spurious Keyword span leaked in from the first match's own content.
    const std::size_t secondStart = text.find("x = 1");
    const std::size_t secondEnd   = secondStart + std::string("x = 1").size();
    for (const HighlightSpan& span : spans) {
        if (span.startByte >= secondStart && span.endByte <= secondEnd) {
            REQUIRE_FALSE(span.syntaxClass == SyntaxClass::Keyword);
        }
    }
}

TEST_CASE("CollectInjectedHighlightSpans adds nothing for an unresolvable injected language", "[Injection]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "whatever"})";
    Tree              tree = parser.Parse(text);
    Query             injectionQuery(language, R"(((pair value: (string (string_content) @injection.content))
                                                       (#set! injection.language "notarealthing")))");

    EmbeddedLanguageCache      cache;
    std::vector<HighlightSpan> spans;
    CollectInjectedHighlightSpans(tree.RootNode(), text, injectionQuery, cache, spans);

    REQUIRE(spans.empty());
}

TEST_CASE("CollectInjectedHighlightSpans resolves a grammar-only sub-language (markdown-inline) with no real Mode",
          "[Injection]") {
    // "markdown_inline" (upstream's own underscore spelling, per real
    // injections.scm files) exercises the alias table; markdown-inline has
    // no ModeByName entry at all (TreeSitter/Languages.cpp), so this also
    // exercises ResolveEmbeddedLanguageHighlight's tier-2 fallback.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "**bold**"})";
    Tree              tree = parser.Parse(text);
    Query             injectionQuery(language, R"(((pair value: (string (string_content) @injection.content))
                                                       (#set! injection.language "markdown_inline")))");

    EmbeddedLanguageCache      cache;
    std::vector<HighlightSpan> spans;
    CollectInjectedHighlightSpans(tree.RootNode(), text, injectionQuery, cache, spans);

    const std::size_t boldOffset = text.find("bold");
    REQUIRE(boldOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, boldOffset, SyntaxClass::Strong));
}
