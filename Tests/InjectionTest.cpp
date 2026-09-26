#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/QueryMatcher.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/Injection.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/LanguageFiles.h"
#include "Editor/Mode.h"

using namespace ned::editor;
using namespace ned::editor::grammar;

namespace {

bool HasSpanContaining(const std::vector<HighlightSpan>& spans, std::size_t offset, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= offset && offset < span.endByte && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

// (language, injected text) for each region a bundled language's own
// injections query finds in `text`.
std::vector<std::pair<std::string, std::string>> BundledInjections(std::string_view   languageName,
                                                                   const std::string& text) {
    const LanguageDefinition* definition = BundledLanguage(languageName);
    REQUIRE(definition != nullptr);
    REQUIRE_FALSE(definition->queries.injections.empty());
    const Language language =
        *LanguageByName(definition->grammar.empty() ? definition->name : definition->grammar);
    Parser       parser(language);
    Tree         tree = parser.Parse(text);
    QueryMatcher query(language, CompileQueryFiles(definition->queries.injections).text);

    std::vector<std::pair<std::string, std::string>> out;
    for (const InjectionRegion& region : CollectInjectionRegions(tree.RootNode(), text, query)) {
        out.emplace_back(region.language, text.substr(region.startByte, region.endByte - region.startByte));
    }
    return out;
}

} // namespace

TEST_CASE("ResolveEmbeddedLanguageHighlight resolves a real bundled Mode via ModeByName", "[Injection]") {
    EmbeddedLanguageCache    cache;
    const HighlightFunction* highlight = ResolveEmbeddedLanguageHighlight("python", cache);
    REQUIRE(highlight != nullptr);

    bool sawKeyword = false;
    for (const HighlightSpan& span : (*highlight)("def f():\n    pass\n", ned::editor::HighlightWindow{})) {
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
    QueryMatcher      injectionQuery(
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
    QueryMatcher      injectionQuery(language, R"(((pair value: (string (string_content) @injection.content))
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
    // no ModeByName entry at all (Grammar/Languages.cpp), so this also
    // exercises ResolveEmbeddedLanguageHighlight's tier-2 fallback.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "**bold**"})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      injectionQuery(language, R"(((pair value: (string (string_content) @injection.content))
                                                       (#set! injection.language "markdown_inline")))");

    EmbeddedLanguageCache      cache;
    std::vector<HighlightSpan> spans;
    CollectInjectedHighlightSpans(tree.RootNode(), text, injectionQuery, cache, spans);

    const std::size_t boldOffset = text.find("bold");
    REQUIRE(boldOffset != std::string::npos);
    REQUIRE(HasSpanContaining(spans, boldOffset, SyntaxClass::Strong));
}

TEST_CASE("CollectInjectionRegions returns raw byte ranges with canonicalized language names, independent of highlighting",
          "[Injection]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "def f(): pass", "b": "x = 1"})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      injectionQuery(
        language, R"(((pair value: (string (string_content) @injection.content)) (#set! injection.language "py")))");

    const std::vector<InjectionRegion> regions = CollectInjectionRegions(tree.RootNode(), text, injectionQuery);
    REQUIRE(regions.size() == 2);
    for (const InjectionRegion& region : regions) {
        // "py" canonicalizes to "python" (CanonicalEmbeddedLanguageName's
        // alias table) -- confirms this function canonicalizes, unlike
        // ResolveEmbeddedLanguageHighlight's caller-side canonicalization,
        // which this function has no need of at all (no HighlightFunction
        // resolution happens here).
        REQUIRE(region.language == "python");
        REQUIRE(region.startByte < region.endByte);
        REQUIRE(region.endByte <= text.size());
    }
}

TEST_CASE("CollectInjectionRegions reports a region even for a language with no bundled Mode", "[Injection]") {
    // Unlike CollectInjectedHighlightSpans (which drops an unresolvable
    // language since it has no HighlightFunction to run), CollectInjectionRegions
    // has no such dependency -- it should still report the byte range.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "whatever"})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      injectionQuery(language, R"(((pair value: (string (string_content) @injection.content))
                                                       (#set! injection.language "notarealthing")))");

    const std::vector<InjectionRegion> regions = CollectInjectionRegions(tree.RootNode(), text, injectionQuery);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].language == "notarealthing");
}

TEST_CASE("Bundled hosts inject the languages their embedded text is written in", "[Injection]") {
    using R = std::vector<std::pair<std::string, std::string>>;

    CHECK(BundledInjections("javascript", "const a = html`<p>${x}</p>`;\n"
                                          "const b = styled.div`color: red;`;\n"
                                          "const c = styled(Button)`margin: 0;`;\n"
                                          "const d = sql`SELECT 1`;\n"
                                          "const e = keyframes`from { opacity: 0; }`;\n"
                                          "const f = plain`not injected`;\n") ==
          R{{"html", "<p>${x}</p>"},
            {"css", "color: red;"},
            {"css", "margin: 0;"},
            {"sql", "SELECT 1"},
            {"css", "from { opacity: 0; }"}});
    CHECK(BundledInjections("typescript", "const q = sql`SELECT 1`;\n") == R{{"sql", "SELECT 1"}});
    CHECK(BundledInjections("tsx", "const q = css`color: red;`;\n") == R{{"css", "color: red;"}});

    CHECK(BundledInjections("ruby", "q = <<~SQL\n  SELECT 1\nSQL\nx = <<-EOS\n  hi\nEOS\n") ==
          R{{"sql", "\n  SELECT 1\n"}, {"eos", "\n  hi\n"}});

    CHECK(BundledInjections("dockerfile", "FROM alpine\nRUN apk add curl && \\\n    echo hi\nRUN [\"echo\", \"x\"]\n") ==
          R{{"bash", "apk add curl && \\\n    echo hi"}});

    CHECK(BundledInjections("make", "all:\n\techo hi\nX = 1\n") == R{{"bash", "echo hi"}});

    CHECK(BundledInjections("nix", "{\n"
                                   "  buildPhase = ''\n    make\n  '';\n"
                                   "  x = pkgs.writeShellScript \"hi\" ''echo hi'';\n"
                                   "  y = writeShellApplication { name = \"y\"; text = ''ls''; };\n"
                                   "  description = ''not a script'';\n"
                                   "}\n") ==
          R{{"bash", "\n    make\n  "}, {"bash", "echo hi"}, {"bash", "ls"}});

    CHECK(BundledInjections("markdown-inline", "Some <b>bold</b> and $x^2$.") ==
          R{{"html", "<b>"}, {"html", "</b>"}, {"latex", "$x^2$"}});

    const R rst = BundledInjections("rst", ".. code-block:: python\n\n   print(1)\n\n.. math::\n\n   x^2\n");
    REQUIRE(rst.size() == 2);
    CHECK(rst[0].first == "python");
    CHECK(rst[0].second.find("print(1)") != std::string::npos);
    CHECK(rst[1].first == "latex");

    const R latex = BundledInjections("latex", "\\begin{minted}{python}\nprint(1)\n\\end{minted}\n");
    REQUIRE(latex.size() == 1);
    CHECK(latex[0].first == "python");
    CHECK(latex[0].second.find("print(1)") != std::string::npos);
}
