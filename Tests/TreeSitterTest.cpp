#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/TreeSitter/IncrementalParse.h"
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

TEST_CASE("Node::Parent walks up to the enclosing node, and to a null Node at the root", "[TreeSitter]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    const Node root   = tree.RootNode();
    const Node object = root.Child(0);
    REQUIRE(object.Type() == "object");

    const Node parentOfObject = object.Parent();
    REQUIRE_FALSE(parentOfObject.IsNull());
    REQUIRE(parentOfObject.Type() == "document");

    REQUIRE(root.Parent().IsNull());
}

TEST_CASE("Node::IsNamed distinguishes a grammar rule from an anonymous punctuation token", "[TreeSitter]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    const Node object = tree.RootNode().Child(0);
    REQUIRE(object.IsNamed());

    // The object's own first/last children are the literal "{"/"}" tokens --
    // unnamed, unlike every real grammar rule.
    REQUIRE_FALSE(object.Child(0).IsNamed());
}

TEST_CASE("Node::NamedDescendantForByteRange finds the smallest named node containing a byte range", "[TreeSitter]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    // Byte 6 is inside the "1" number literal.
    const Node number = tree.RootNode().NamedDescendantForByteRange(6, 6);
    REQUIRE_FALSE(number.IsNull());
    REQUIRE(number.Type() == "number");

    // A range spanning the whole "1" should resolve to that same node.
    const Node exact = tree.RootNode().NamedDescendantForByteRange(number.StartByte(), number.EndByte());
    REQUIRE(exact.StartByte() == number.StartByte());
    REQUIRE(exact.EndByte() == number.EndByte());
}

// Emacs-keymap-round-2 follow-up (forward-sexp/backward-sexp).
TEST_CASE("Node::NextNamedSibling/PrevNamedSibling walk between sibling nodes", "[TreeSitter]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = "[1, 2, 3]";
    Tree              tree = parser.Parse(text);

    const Node first = tree.RootNode().NamedDescendantForByteRange(1, 1);
    REQUIRE(first.Type() == "number");
    REQUIRE(first.StartByte() == 1);

    const Node second = first.NextNamedSibling();
    REQUIRE_FALSE(second.IsNull());
    REQUIRE(second.Type() == "number");
    REQUIRE(second.StartByte() == 4);

    const Node third = second.NextNamedSibling();
    REQUIRE_FALSE(third.IsNull());
    REQUIRE(third.StartByte() == 7);
    REQUIRE(third.NextNamedSibling().IsNull()); // no fourth element

    REQUIRE(third.PrevNamedSibling().StartByte() == second.StartByte());
    REQUIRE(second.PrevNamedSibling().StartByte() == first.StartByte());
    REQUIRE(first.PrevNamedSibling().IsNull());
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

TEST_CASE("Query::Matches groups captures from the same match together, not scrambled across matches", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1, "b": 2})";
    Tree              tree = parser.Parse(text);
    Query             query(language, "(pair key: (string) @key value: (number) @value)");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    REQUIRE(matches.size() == 2);
    REQUIRE(matches[0].captures.size() == 2);
    REQUIRE(text.substr(matches[0].captures[0].startByte, matches[0].captures[0].endByte - matches[0].captures[0].startByte) ==
            "\"a\"");
    REQUIRE(text.substr(matches[0].captures[1].startByte, matches[0].captures[1].endByte - matches[0].captures[1].startByte) == "1");
    REQUIRE(matches[1].captures.size() == 2);
    REQUIRE(text.substr(matches[1].captures[0].startByte, matches[1].captures[0].endByte - matches[1].captures[0].startByte) ==
            "\"b\"");
    REQUIRE(text.substr(matches[1].captures[1].startByte, matches[1].captures[1].endByte - matches[1].captures[1].startByte) == "2");
}

TEST_CASE("Query::Matches resolves a #set! string operand into setDirectives", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"(((string) @s (#set! injection.language "javascript")))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].setDirectives.at("injection.language") == "javascript");
}

TEST_CASE("Query::Matches stores an empty value for a zero-operand #set! directive", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    Query             query(language, R"(((string) @s (#set! injection.combined)))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].setDirectives.contains("injection.combined"));
    REQUIRE(matches[0].setDirectives.at("injection.combined").empty());
}

TEST_CASE("Query::Matches still respects predicate filtering, e.g. #eq?", "[TreeSitter]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "a", "b": "c"})";
    Tree              tree = parser.Parse(text);
    Query             query(language, "(pair key: (string) @key value: (string) @value (#eq? @key @value))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    // Only the "a": "a" pair has an equal key/value.
    REQUIRE(matches.size() == 1);
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

namespace {

    // Deep structural comparison, for asserting an incrementally reparsed
    // tree is isomorphic to a from-scratch full parse of the same final
    // text -- a shallow root-only check wouldn't catch a bad TSInputEdit
    // (wrong byte offset or row/column) that only corrupts a subtree deeper
    // than the root.
    void RequireNodesMatch(const Node& a, const Node& b) {
        REQUIRE(a.Type() == b.Type());
        REQUIRE(a.StartByte() == b.StartByte());
        REQUIRE(a.EndByte() == b.EndByte());
        REQUIRE(a.IsNamed() == b.IsNamed());
        REQUIRE(a.ChildCount() == b.ChildCount());
        for (std::size_t i = 0; i < a.ChildCount(); ++i) {
            RequireNodesMatch(a.Child(i), b.Child(i));
        }
    }

} // namespace

TEST_CASE("IncrementalParseCache returns the cached tree unchanged when text is identical", "[TreeSitter]") {
    Parser                 parser(*LanguageByName("json"));
    IncrementalParseCache  cache;
    const std::string      text = R"({"a": 1})";

    const Tree& first  = cache.Update(parser, text);
    const Tree& second = cache.Update(parser, text);

    REQUIRE(&first == &second);
}

TEST_CASE("IncrementalParseCache's incremental reparse matches a fresh full parse after a single edit", "[TreeSitter]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    (void)cache.Update(parser, R"({"a": 1, "b": 2})");
    const std::string edited = R"({"a": 100, "b": 2})"; // widens "1" to "100" in place
    const Tree&        tree   = cache.Update(parser, edited);
    REQUIRE_FALSE(tree.IsNull());

    Parser     freshParser(*LanguageByName("json"));
    const Tree freshTree = freshParser.Parse(edited);

    RequireNodesMatch(tree.RootNode(), freshTree.RootNode());
}

TEST_CASE("IncrementalParseCache handles an edit that inserts newlines", "[TreeSitter]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    (void)cache.Update(parser, "{\"a\": 1,\n \"b\": 2}");
    const std::string edited = "{\"a\": 1,\n \"b\": 2,\n \"c\": 3}"; // appends a third key on a new line
    const Tree&        tree   = cache.Update(parser, edited);
    REQUIRE_FALSE(tree.IsNull());

    Parser     freshParser(*LanguageByName("json"));
    const Tree freshTree = freshParser.Parse(edited);

    RequireNodesMatch(tree.RootNode(), freshTree.RootNode());
}

TEST_CASE("IncrementalParseCache stays correct across a sequence of edits", "[TreeSitter]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    // Simulates typing a value in one keystroke at a time, each call
    // incrementally reparsing against the previous edit's result rather
    // than the original text.
    const std::vector<std::string> steps = {
        R"({"a": ""})",
        R"({"a": "h"})",
        R"({"a": "he"})",
        R"({"a": "hel"})",
        R"({"a": "hell"})",
        R"({"a": "hello"})",
    };
    const Tree* tree = nullptr;
    for (const std::string& step : steps) {
        tree = &cache.Update(parser, step);
    }
    REQUIRE_FALSE(tree->IsNull());

    Parser     freshParser(*LanguageByName("json"));
    const Tree freshTree = freshParser.Parse(steps.back());

    RequireNodesMatch(tree->RootNode(), freshTree.RootNode());
}
