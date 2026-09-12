#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/LanguageFiles.h"
#include "Editor/QueryData.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Query.h"

namespace fs = std::filesystem;
using ned::editor::querydata::ConvertScmToJanet;
using ned::editor::querydata::Form;
using ned::editor::querydata::ParseJanet;
using ned::editor::querydata::ParseScm;
using ned::editor::querydata::QueryDataError;
using ned::editor::querydata::ToQueryText;

namespace {

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

fs::path RepoRoot() {
    return fs::path(NED_REPO_ROOT);
}
fs::path DepsDir() {
    return RepoRoot() / "build" / "_deps";
}
fs::path LanguagesDir() {
    return RepoRoot() / "Source" / "Languages";
}

// The upstream query files ned consumes unmodified, vendored under
// Source/Languages/<name>/upstream/ in the Janet spelling. The source path
// is relative to the FetchContent tree, so this is also the drift gate: a
// grammar bump whose query changed fails "vendored upstream queries match
// their source" until NED_BLESS_QUERIES=1 re-vendors it.
struct UpstreamQuery {
    const char* source;   // under build/_deps
    const char* vendored; // under Source/Languages
};

const std::vector<UpstreamQuery>& UpstreamQueries() {
    static const std::vector<UpstreamQuery> kQueries = {
        {"tree-sitter-json-src/queries/highlights.scm", "json/upstream/highlights.janet"},
        {"tree-sitter-php-src/queries/highlights.scm", "php/upstream/highlights.janet"},
        {"tree-sitter-php-src/queries/tags.scm", "php/upstream/tags.janet"},
        {"tree-sitter-javascript-src/queries/highlights.scm", "javascript/upstream/highlights.janet"},
        {"tree-sitter-javascript-src/queries/tags.scm", "javascript/upstream/tags.janet"},
        {"tree-sitter-typescript-src-src/queries/highlights.scm", "typescript/upstream/highlights.janet"},
        {"tree-sitter-typescript-src-src/queries/tags.scm", "typescript/upstream/tags.janet"},
        {"tree-sitter-html-src/queries/highlights.scm", "html/upstream/highlights.janet"},
        {"tree-sitter-html-src/queries/injections.scm", "html/upstream/injections.janet"},
        {"tree-sitter-css-src/queries/highlights.scm", "css/upstream/highlights.janet"},
        {"tree-sitter-python-src/queries/highlights.scm", "python/upstream/highlights.janet"},
        {"tree-sitter-python-src/queries/tags.scm", "python/upstream/tags.janet"},
        {"tree-sitter-bash-src/queries/highlights.scm", "bash/upstream/highlights.janet"},
        {"tree-sitter-janet-simple-src/queries/highlights.scm", "janet/upstream/highlights.janet"},
        {"tree-sitter-markdown-src/tree-sitter-markdown/queries/highlights.scm", "markdown/upstream/highlights.janet"},
        {"tree-sitter-markdown-src/tree-sitter-markdown/queries/injections.scm", "markdown/upstream/injections.janet"},
        {"tree-sitter-markdown-inline-src/tree-sitter-markdown-inline/queries/highlights.scm",
         "markdown-inline/upstream/highlights.janet"},
        {"tree-sitter-org-src/queries/injections.scm", "org/upstream/injections.janet"},
        {"tree-sitter-yaml-src/queries/highlights.scm", "yaml/upstream/highlights.janet"},
        {"tree-sitter-toml-src/queries/highlights.scm", "toml/upstream/highlights.janet"},
        {"tree-sitter-fish-src/queries/highlights.scm", "fish/upstream/highlights.janet"},
        {"tree-sitter-xml-src/queries/xml/highlights.scm", "xml/upstream/highlights.janet"},
        {"tree-sitter-rust-src/queries/highlights.scm", "rust/upstream/highlights.janet"},
        {"tree-sitter-rust-src/queries/tags.scm", "rust/upstream/tags.janet"},
        {"tree-sitter-go-src/queries/highlights.scm", "go/upstream/highlights.janet"},
        {"tree-sitter-go-src/queries/tags.scm", "go/upstream/tags.janet"},
        {"tree-sitter-c-sharp-src/queries/highlights.scm", "csharp/upstream/highlights.janet"},
        {"tree-sitter-c-sharp-src/queries/tags.scm", "csharp/upstream/tags.janet"},
        {"tree-sitter-java-src/queries/highlights.scm", "java/upstream/highlights.janet"},
        {"tree-sitter-java-src/queries/tags.scm", "java/upstream/tags.janet"},
        {"tree-sitter-kotlin-src/queries/highlights.scm", "kotlin/upstream/highlights.janet"},
    };
    return kQueries;
}

bool Blessing() {
    const char* env = std::getenv("NED_BLESS_QUERIES");
    return env != nullptr && *env != '\0';
}

void WriteFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    out << content;
}

} // namespace

TEST_CASE("Janet-syntax query data reads to the same forms as tree-sitter's own", "[QueryData]") {
    const std::string       scm       = "; a comment\n(call_expression function: (identifier) @name (#eq? @name \"foo\")) @call\n"
                                        "[(a) (b)]* . (_)? !field \"lit\"+ _?\n";
    const std::string       janet     = "# a comment\n(call_expression function: (identifier) @name (:eq? @name \"foo\")) @call\n"
                                        "[(a) (b)]* . (_)? !field \"lit\"+ _?\n";
    const std::vector<Form> fromScm   = ParseScm(scm);
    const std::vector<Form> fromJanet = ParseJanet(janet);
    REQUIRE(fromScm == fromJanet);
    REQUIRE(fromScm.size() == 9); // comment, pattern, @call, alternation, anchor, (_)?, !field, "lit"+, _?
    REQUIRE(fromScm[0].kind == Form::Kind::Comment);
    REQUIRE(fromScm[1].kind == Form::Kind::List);
    REQUIRE(fromScm[1].items.back().kind == Form::Kind::Predicate);
    REQUIRE(fromScm[1].items.back().text == "eq?");
    REQUIRE(fromScm[2].text == "@call");
    REQUIRE(fromScm[3].kind == Form::Kind::Alternation);
    REQUIRE(fromScm[3].quantifier == '*');
    REQUIRE(fromScm[4].text == ".");
    REQUIRE(fromScm[5].kind == Form::Kind::List);
    REQUIRE(fromScm[5].quantifier == '?');
    REQUIRE(fromScm[6].text == "!field");
    REQUIRE(fromScm[7].kind == Form::Kind::String);
    REQUIRE(fromScm[7].quantifier == '+');
    REQUIRE(fromScm[8].text == "_");
    REQUIRE(fromScm[8].quantifier == '?');
}

TEST_CASE("Query text is emitted with each form on its source line", "[QueryData]") {
    const std::string janet = "# header\n\n(a) @x\n(b\n  (c) @y)\n";
    const std::string text  = ToQueryText(ParseJanet(janet));
    REQUIRE(text == "\n\n(a) @x\n(b\n(c) @y)\n");
    REQUIRE(ToQueryText(ParseJanet(janet), /*preserveLines=*/false) == "(a) @x\n(b (c) @y)\n");
}

TEST_CASE("The Janet reader rejects a file still in tree-sitter's spelling", "[QueryData]") {
    REQUIRE_THROWS_AS(ParseJanet("; a tree-sitter comment\n(a) @x"), QueryDataError);
    REQUIRE_THROWS_AS(ParseJanet("((a) @x (#eq? @x \"y\"))"), QueryDataError); // '#' comments out the rest of the line
    REQUIRE_THROWS_AS(ParseJanet("(a"), QueryDataError);
    REQUIRE_THROWS_AS(ParseJanet("* (a)"), QueryDataError); // a quantifier with nothing before it
    try {
        ParseJanet("(a)\n(b\n");
        FAIL("expected a parse error");
    }
    catch (const QueryDataError& error) {
        REQUIRE(error.Line() == 3);
    }
}

TEST_CASE("String escapes decode by tree-sitter's rules and re-encode by Janet's", "[QueryData]") {
    // tree-sitter: \d is a literal d; \n is a newline; \\ is a backslash.
    const std::string janet = ConvertScmToJanet("((x) (#match? @c \"^[A-Z\\d_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"");
    REQUIRE(janet == "((x) (:match? @c \"^[A-Zd_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"");
    const std::vector<Form> forms = ParseJanet(janet);
    REQUIRE(forms[0].items[1].items[1].text == "^[A-Zd_]+$");
    REQUIRE(forms[1].text == "a\nb");
    REQUIRE(forms[2].text == "q\"");
    REQUIRE(forms[3].text == "\\d");
    REQUIRE(ToQueryText(forms, false) == "((x) (#match? @c \"^[A-Zd_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"\n");
}

TEST_CASE("Conversion preserves layout and comments byte for byte", "[QueryData]") {
    const std::string scm   = ";; Header ; with \"quotes\" and (#not a predicate)\n(a)   @x   ; trailing\n\n\n  [(b) (c)]\n";
    const std::string janet = ConvertScmToJanet(scm);
    REQUIRE(janet == "#; Header ; with \"quotes\" and (#not a predicate)\n(a)   @x   # trailing\n\n\n  [(b) (c)]\n");
    REQUIRE(ParseJanet(janet) == ParseScm(scm));
}

TEST_CASE("Every vendored upstream query matches its source in the FetchContent tree", "[QueryData]") {
    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }
    for (const UpstreamQuery& query : UpstreamQueries()) {
        const fs::path source   = DepsDir() / query.source;
        const fs::path vendored = LanguagesDir() / query.vendored;
        INFO(query.vendored << " <- " << query.source);
        REQUIRE(fs::exists(source));
        const std::string converted = ConvertScmToJanet(ReadFile(source));
        if (Blessing()) {
            WriteFile(vendored, converted);
        }
        REQUIRE(fs::exists(vendored));
        REQUIRE(ReadFile(vendored) == converted);
    }
}

TEST_CASE("Every embedded query file loads, and compiles under its grammar", "[QueryData]") {
    std::size_t checked = 0;
    for (const ned::editor::EmbeddedLanguageFile& file : ned::editor::EmbeddedLanguageFiles()) {
        const std::string path(file.path);
        if (!path.ends_with(".janet") || path.ends_with("language.janet")) {
            continue;
        }
        INFO(path);
        const std::vector<Form> forms = ParseJanet(file.content);
        REQUIRE_FALSE(forms.empty());
        // <name>/... -> the grammar it targets; jank has no files of its own.
        const std::string grammar  = path.substr(0, path.find('/'));
        const auto        language = ned::editor::treesitter::LanguageByName(grammar);
        REQUIRE(language.has_value());
        const ned::editor::QueryText text = ned::editor::CompileQueryFiles({path});
        REQUIRE_NOTHROW(ned::editor::treesitter::Query(*language, text.text));
        ++checked;
    }
    REQUIRE(checked >= 90);
}
