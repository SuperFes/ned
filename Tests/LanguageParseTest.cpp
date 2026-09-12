#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "Editor/BundledLanguages.h"
#include "Editor/JanetData.h"
#include "Editor/LanguageParse.h"

using ned::editor::BundledLanguage;
using ned::editor::BundledLanguages;
using ned::editor::DiscoverQueryFiles;
using ned::editor::LanguageDefinition;
using ned::editor::ParseLanguageDefinition;
using ned::editor::SyntaxClass;
using ned::editor::janetdata::JanetDataError;
using ned::editor::janetdata::ParseJanetData;
using ned::editor::janetdata::Value;

TEST_CASE("The data reader reads structs, tuples, keywords, strings and bools", "[LanguageParse]") {
    const Value value = ParseJanetData(R"(
# a comment
{:name "cpp"
 :wrap true
 :list ["a" :kw (1sym) nil]})");
    REQUIRE(value.IsStruct());
    REQUIRE(value.Get("name")->text == "cpp");
    REQUIRE(value.Get("wrap")->boolean);
    const Value* list = value.Get("list");
    REQUIRE(list->IsTuple());
    REQUIRE(list->items.size() == 4);
    REQUIRE(list->items[1].IsKeyword());
    REQUIRE(list->items[2].IsTuple());
    REQUIRE(list->items[3].kind == Value::Kind::Nil);
    REQUIRE(value.Get("missing") == nullptr);
}

TEST_CASE("The data reader rejects code-shaped input loudly", "[LanguageParse]") {
    REQUIRE_THROWS_AS(ParseJanetData("{:f (fn [] 1)} {:second true}"), JanetDataError); // two top-level values
    REQUIRE_THROWS_AS(ParseJanetData("'(quoted)"), JanetDataError);
    REQUIRE_THROWS_AS(ParseJanetData("{:key}"), JanetDataError);
    REQUIRE_THROWS_AS(ParseJanetData("{:key \"unclosed}"), JanetDataError);
    REQUIRE_THROWS_AS(ParseJanetData(""), JanetDataError);
}

TEST_CASE("A definition parses every field, and rejects what it does not know", "[LanguageParse]") {
    const LanguageDefinition definition = ParseLanguageDefinition("demo", R"(
{:name "demo"
 :grammar "clojure"
 :extensions [".demo"]
 :filenames ["Demofile"]
 :line-comment ";"
 :auto-pairs :lisp
 :wrap-lines true
 :embedded-documents true
 :keymap [["TAB" "demo-align"]]
 :capture-classes {"punctuation.special" :markup-marker}
 :queries-from "clojure"
 :queries {:indents ["demo/indents.janet"]}
 :escapes ["demo.highlight"]
 :lsp-root-markers ["demo.toml"]
 :import-resolution {:extensions ["demo"] :index-basenames ["init"] :search-package-dirs true}
 :injection-aliases ["dm"]
 :snippets {"main" "fn main"}})");
    REQUIRE(definition.name == "demo");
    REQUIRE(definition.grammar == "clojure");
    REQUIRE(definition.extensions == std::vector<std::string>{".demo"});
    REQUIRE(definition.filenames == std::vector<std::string>{"Demofile"});
    REQUIRE(definition.lineCommentPrefix == ";");
    REQUIRE(definition.autoPairs == ned::editor::AutoPairSet::Lisp);
    REQUIRE(definition.wrapLines);
    REQUIRE(definition.embeddedDocuments);
    REQUIRE(definition.keymap.front() == std::pair<std::string, std::string>{"TAB", "demo-align"});
    REQUIRE(definition.captureClasses.front().second == SyntaxClass::MarkupMarker);
    REQUIRE(definition.queriesFrom == "clojure");
    REQUIRE(definition.queries.indents == std::vector<std::string>{"demo/indents.janet"});
    REQUIRE(definition.escapes == std::vector<std::string>{"demo.highlight"});
    REQUIRE(definition.lspRootMarkers == std::vector<std::string>{"demo.toml"});
    REQUIRE(definition.importResolution.has_value());
    REQUIRE(definition.importResolution->extensions == std::vector<std::string>{"demo"});
    REQUIRE(definition.importResolution->indexBasenames == std::vector<std::string>{"init"});
    REQUIRE(definition.importResolution->searchPackageDirs);
    REQUIRE(definition.injectionAliases == std::vector<std::string>{"dm"});
    REQUIRE(definition.snippets.front() == std::pair<std::string, std::string>{"main", "fn main"});

    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:name \"other\"}"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:unknown-key true}"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:extensions [\"demo\"]}"), std::runtime_error); // no dot
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:auto-pairs :vim}"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:queries {:folds \"x\"}}"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "{:capture-classes {\"x\" :not-a-class}}"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseLanguageDefinition("demo", "[\"not\" \"a\" \"struct\"]"), std::runtime_error);
}

TEST_CASE("Query discovery: upstream first, own delta after; explicit entries win; queries-from redirects",
          "[LanguageParse]") {
    const std::set<std::string> files = {
        "demo/upstream/highlights.janet",
        "demo/highlights.janet",
        "demo/tags.janet",
        "other/upstream/locals.janet",
        "other/indents.janet",
    };
    const auto exists = [&files](std::string_view path) { return files.contains(std::string(path)); };

    LanguageDefinition plain = ParseLanguageDefinition("demo", "{:name \"demo\"}");
    DiscoverQueryFiles(plain, exists);
    REQUIRE(plain.queries.highlights == std::vector<std::string>{"demo/upstream/highlights.janet", "demo/highlights.janet"});
    REQUIRE(plain.queries.tags == std::vector<std::string>{"demo/tags.janet"});
    REQUIRE(plain.queries.locals.empty());

    LanguageDefinition redirected = ParseLanguageDefinition("demo", R"(
{:name "demo" :queries-from "other" :queries {:indents ["demo/tags.janet"]}})");
    DiscoverQueryFiles(redirected, exists);
    REQUIRE(redirected.queries.locals == std::vector<std::string>{"other/upstream/locals.janet"});
    REQUIRE(redirected.queries.indents == std::vector<std::string>{"demo/tags.janet"}); // explicit wins over other/indents
    REQUIRE(redirected.queries.highlights.empty());                                     // other/ has no highlights

    LanguageDefinition none = ParseLanguageDefinition("bare", "{:name \"bare\" :grammar :none}");
    DiscoverQueryFiles(none, exists);
    REQUIRE(none.queries.highlights.empty()); // grammarless discovers nothing
}

// The derived per-language facts, frozen the way the extension table is:
// these used to be four independent C++ tables (RootResolver,
// ImportResolutionConfig, Injection aliases, BundledSnippets), and a
// definition losing one silently would only show up feature-by-feature.
TEST_CASE("The definitions carry the facts the old C++ tables held", "[LanguageParse]") {
    const std::set<std::string>              kRootMarkerLanguages       = {"c", "cpp", "python", "javascript", "typescript", "tsx",
                                                                           "php", "rust", "go", "csharp", "java", "kotlin"};
    const std::set<std::string>              kImportResolutionLanguages = {"php", "javascript", "typescript", "tsx", "python", "bash",
                                                                           "clojure", "jank", "css", "janet", "rust"};
    const std::map<std::string, std::string> kAliases                   = {
        {"js", "javascript"},
        {"jsx", "tsx"},
        {"ts", "typescript"},
        {"py", "python"},
        {"sh", "bash"},
        {"shell", "bash"},
        {"zsh", "bash"},
        {"c++", "cpp"},
        {"cc", "cpp"},
        {"cxx", "cpp"},
        {"hpp", "cpp"},
        {"h", "c"},
        {"yml", "yaml"},
        {"clj", "clojure"},
        {"markdown_inline", "markdown-inline"},
    };

    std::set<std::string>              rootMarkers, importResolution;
    std::map<std::string, std::string> aliases;
    std::size_t                        snippetCount = 0;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (!definition.lspRootMarkers.empty()) {
            rootMarkers.insert(definition.name);
        }
        if (definition.importResolution.has_value()) {
            importResolution.insert(definition.name);
        }
        for (const std::string& alias : definition.injectionAliases) {
            REQUIRE_FALSE(aliases.contains(alias)); // two languages claiming one alias is a bug
            aliases[alias] = definition.name;
        }
        snippetCount += definition.snippets.size();
    }
    REQUIRE(rootMarkers == kRootMarkerLanguages);
    REQUIRE(importResolution == kImportResolutionLanguages);
    REQUIRE(aliases == kAliases);
    REQUIRE(snippetCount == 83);

    REQUIRE(BundledLanguage("csharp")->lspRootMarkers == std::vector<std::string>{"global.json", "*.csproj", "*.sln"});
    REQUIRE(BundledLanguage("rust")->importResolution->indexBasenames == std::vector<std::string>{"mod"});
    REQUIRE(BundledLanguage("jank")->queriesFrom == "clojure");
    REQUIRE(BundledLanguage("cpp")->queries.imports == std::vector<std::string>{"c/imports.janet"});
    REQUIRE(BundledLanguage("markdown-inline")->queries.highlights ==
            std::vector<std::string>{"markdown-inline/upstream/highlights.janet", "markdown-inline/highlights.janet"});
}
