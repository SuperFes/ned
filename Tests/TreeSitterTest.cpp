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

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode());

    REQUIRE(captures.size() == 2);

    REQUIRE(captures[0].name == "string");
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "\"a\"");

    REQUIRE(captures[1].name == "number");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "1");
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
