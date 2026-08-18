#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>

#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

using namespace ned::editor::treesitter;

TEST_CASE("LanguageByName finds a bundled grammar", "[TreeSitter]") {
    const std::optional<Language> language = LanguageByName("json");
    REQUIRE(language.has_value());
    REQUIRE(language->Raw() != nullptr);
}

TEST_CASE("LanguageByName returns nullopt for an unbundled name", "[TreeSitter]") {
    REQUIRE_FALSE(LanguageByName("not-a-real-language").has_value());
}

TEST_CASE("Parser::Parse produces a non-null tree for valid JSON", "[TreeSitter]") {
    Parser parser(*LanguageByName("json"));
    Tree   tree = parser.Parse(R"({"a": 1})");

    REQUIRE_FALSE(tree.IsNull());
}

TEST_CASE("Tree::RootNode returns the grammar's document node spanning the whole input", "[TreeSitter]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    const Node root = tree.RootNode();
    REQUIRE_FALSE(root.IsNull());
    REQUIRE(root.Type() == "document");
    REQUIRE(root.StartByte() == 0);
    REQUIRE(root.EndByte() == text.size());
}

TEST_CASE("Node::Child navigates into the parse tree", "[TreeSitter]") {
    Parser parser(*LanguageByName("json"));
    Tree   tree = parser.Parse(R"({"a": 1})");

    const Node root = tree.RootNode();
    REQUIRE(root.ChildCount() == 1);

    const Node object = root.Child(0);
    REQUIRE(object.Type() == "object");
}

TEST_CASE("Query::Captures finds string and number literals with correct byte ranges", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    Query             query(language, "(string) @string (number) @number");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 2);

    REQUIRE(captures[0].name == "string");
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "\"a\"");

    REQUIRE(captures[1].name == "number");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "1");
}

TEST_CASE("Query::Captures evaluates #eq? -- only a matching pair passes", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "a", "b": "c"})";
    Tree              tree = parser.Parse(text);
    Query             query(language, "(pair key: (string) @key value: (string) @value (#eq? @key @value))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    // Only the "a": "a" pair has an equal key/value -- "b": "c" doesn't
    // match at all, not even partially.
    REQUIRE(captures.size() == 2);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "\"a\"");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "\"a\"");
}

TEST_CASE("Query::Captures evaluates #match? against a captured node's own text", "[TreeSitter]") {
    // The exact real-world case nvim-treesitter's own C query uses this
    // predicate for: an ALL-CAPS identifier reads as a constant.
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int MAX_SIZE; int count;";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"(((identifier) @constant (#match? @constant "^[A-Z_]+$")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "MAX_SIZE");
}

TEST_CASE("Query::Captures translates Lua's %u pattern class for #lua-match?", "[TreeSitter]") {
    // The exact real-world case in the vendored nvim-treesitter cpp query
    // (constructor-name detection): "^%u" has no ECMAScript meaning as-is.
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int Foo; int bar;";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"(((identifier) @upper (#lua-match? @upper "^%u")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "Foo");
}

TEST_CASE("Query::Captures evaluates #any-of? against a literal set", "[TreeSitter]") {
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int foo; int bar; int baz;";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"(((identifier) @kw (#any-of? @kw "foo" "bar")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 2);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "foo");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "bar");
}

TEST_CASE("Query::Captures evaluates #has-parent?/#has-ancestor? -- immediate vs. any level",
          "[TreeSitter]") {
    // Both numbers' immediate parent is "array", not "object" -- but
    // "object" is still an ancestor further up (array -> pair -> object).
    // This is exactly the distinction #has-parent? (immediate only) vs.
    // #has-ancestor? (any level) is for.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": [1, 2]})";
    Tree              tree = parser.Parse(text);

    Query hasParentArray(language, "((number) @n (#has-parent? @n array))");
    REQUIRE(hasParentArray.Captures(tree.RootNode(), text).size() == 2);

    Query hasParentObject(language, "((number) @n (#has-parent? @n object))");
    REQUIRE(hasParentObject.Captures(tree.RootNode(), text).empty());

    Query hasAncestorObject(language, "((number) @n (#has-ancestor? @n object))");
    REQUIRE(hasAncestorObject.Captures(tree.RootNode(), text).size() == 2);
}

TEST_CASE("Query::Captures never suppresses a match for a predicate it doesn't recognize", "[TreeSitter]") {
    // #set! is a real, non-filtering directive query files use for match
    // priority -- and any other unrecognized predicate name gets the same
    // treatment: inert, never suppresses a match. Matches the pre-existing
    // behavior (before predicate evaluation existed at all, every match was
    // unconditionally included) for anything not explicitly handled.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"((string) @s (#set! "priority" 100) (#some-made-up-predicate? @s "x"))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
}

TEST_CASE("Query constructor throws on a malformed query", "[TreeSitter]") {
    const Language language = *LanguageByName("json");
    REQUIRE_THROWS_AS(Query(language, "(not_a_real_node_type) @foo"), std::runtime_error);
}

TEST_CASE("Parser is move-constructible and move-assignable", "[TreeSitter]") {
    Parser parser(*LanguageByName("json"));
    Parser moved(std::move(parser));

    Tree tree = moved.Parse(R"({"a": 1})");
    REQUIRE_FALSE(tree.IsNull());

    Parser other(*LanguageByName("json"));
    other           = std::move(moved);
    Tree secondTree = other.Parse(R"({"b": 2})");
    REQUIRE_FALSE(secondTree.IsNull());
}

TEST_CASE("Tree is move-constructible and move-assignable", "[TreeSitter]") {
    Parser parser(*LanguageByName("json"));
    Tree   tree(parser.Parse(R"({"a": 1})"));
    Tree   moved(std::move(tree));

    REQUIRE_FALSE(moved.IsNull());
    REQUIRE(moved.RootNode().Type() == "document");

    Tree other = parser.Parse(R"({"b": 2})");
    other      = std::move(moved);
    REQUIRE_FALSE(other.IsNull());
}
