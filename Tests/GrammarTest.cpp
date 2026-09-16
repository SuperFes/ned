#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Parse/Node.h"
#include "Editor/Grammar/IncrementalParse.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/QueryMatcher.h"
#include "Editor/Grammar/Tree.h"

using namespace ned::editor::grammar;

TEST_CASE("LanguageByName finds a bundled grammar", "[Grammar]") {
    const std::optional<Language> language = LanguageByName("json");
    REQUIRE(language.has_value());
    REQUIRE(language->Raw() != nullptr);
}

TEST_CASE("LanguageByName returns nullopt for an unbundled name", "[Grammar]") {
    REQUIRE_FALSE(LanguageByName("not-a-real-language").has_value());
}

TEST_CASE("Parser::Parse produces a non-null tree for valid JSON", "[Grammar]") {
    Parser parser(*LanguageByName("json"));
    Tree   tree = parser.Parse(R"({"a": 1})");

    REQUIRE_FALSE(tree.IsNull());
}

TEST_CASE("Tree::RootNode returns the grammar's document node spanning the whole input", "[Grammar]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    const Node root = tree.RootNode();
    REQUIRE_FALSE(root.IsNull());
    REQUIRE(root.Type() == "document");
    REQUIRE(root.StartByte() == 0);
    REQUIRE(root.EndByte() == text.size());
}

TEST_CASE("Node::Child navigates into the parse tree", "[Grammar]") {
    Parser parser(*LanguageByName("json"));
    Tree   tree = parser.Parse(R"({"a": 1})");

    const Node root = tree.RootNode();
    REQUIRE(root.ChildCount() == 1);

    const Node object = root.Child(0);
    REQUIRE(object.Type() == "object");
}

TEST_CASE("Node::Parent walks up to the enclosing node, and to a null Node at the root", "[Grammar]") {
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

TEST_CASE("Node::IsNamed distinguishes a grammar rule from an anonymous punctuation token", "[Grammar]") {
    Parser            parser(*LanguageByName("json"));
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);

    const Node object = tree.RootNode().Child(0);
    REQUIRE(object.IsNamed());

    // The object's own first/last children are the literal "{"/"}" tokens --
    // unnamed, unlike every real grammar rule.
    REQUIRE_FALSE(object.Child(0).IsNamed());
}

TEST_CASE("Node::NamedDescendantForByteRange finds the smallest named node containing a byte range", "[Grammar]") {
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
TEST_CASE("Node::NextNamedSibling/PrevNamedSibling walk between sibling nodes", "[Grammar]") {
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

TEST_CASE("QueryMatcher::Captures finds string and number literals with correct byte ranges", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, "(string) @string (number) @number");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 2);

    REQUIRE(captures[0].name == "string");
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "\"a\"");

    REQUIRE(captures[1].name == "number");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "1");
}

TEST_CASE("QueryMatcher::Captures evaluates #eq? -- only a matching pair passes", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "a", "b": "c"})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, "(pair key: (string) @key value: (string) @value (#eq? @key @value))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    // Only the "a": "a" pair has an equal key/value -- "b": "c" doesn't
    // match at all, not even partially.
    REQUIRE(captures.size() == 2);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "\"a\"");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "\"a\"");
}

TEST_CASE("QueryMatcher::Captures evaluates #match? against a captured node's own text", "[Grammar]") {
    // The exact real-world case nvim-treesitter's own C query uses this
    // predicate for: an ALL-CAPS identifier reads as a constant.
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int MAX_SIZE; int count;";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((identifier) @constant (#match? @constant "^[A-Z_]+$")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "MAX_SIZE");
}

TEST_CASE("QueryMatcher::Captures translates Lua's %u pattern class for #lua-match?", "[Grammar]") {
    // The exact real-world case in the vendored nvim-treesitter cpp query
    // (constructor-name detection): "^%u" has no ECMAScript meaning as-is.
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int Foo; int bar;";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((identifier) @upper (#lua-match? @upper "^%u")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "Foo");
}

TEST_CASE("QueryMatcher::Captures evaluates #any-of? against a literal set", "[Grammar]") {
    const Language    language = *LanguageByName("c");
    Parser            parser(language);
    const std::string text = "int foo; int bar; int baz;";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((identifier) @kw (#any-of? @kw "foo" "bar")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 2);
    REQUIRE(text.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "foo");
    REQUIRE(text.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "bar");
}

TEST_CASE("QueryMatcher::Captures evaluates #has-parent?/#has-ancestor? -- immediate vs. any level",
          "[Grammar]") {
    // Both numbers' immediate parent is "array", not "object" -- but
    // "object" is still an ancestor further up (array -> pair -> object).
    // This is exactly the distinction #has-parent? (immediate only) vs.
    // #has-ancestor? (any level) is for.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": [1, 2]})";
    Tree              tree = parser.Parse(text);

    QueryMatcher hasParentArray(language, "((number) @n (#has-parent? @n array))");
    REQUIRE(hasParentArray.Captures(tree.RootNode(), text).size() == 2);

    QueryMatcher hasParentObject(language, "((number) @n (#has-parent? @n object))");
    REQUIRE(hasParentObject.Captures(tree.RootNode(), text).empty());

    QueryMatcher hasAncestorObject(language, "((number) @n (#has-ancestor? @n object))");
    REQUIRE(hasAncestorObject.Captures(tree.RootNode(), text).size() == 2);
}

TEST_CASE("QueryMatcher::Captures evaluates a variadic #has-parent?/#not-has-parent? as any-of "
          "over its trailing type operands (nvim's own convention, e.g. cpp/highlights.janet:370's "
          "3-operand (#has-parent? @c template_method function_declarator))",
          "[Grammar]") {
    // 1 and 2's immediate parent is "array"; 3's immediate parent is "pair"
    // (the value half of "b": 3) -- three distinct immediate-parent shapes
    // to distinguish a real any-of from an accidental "first operand only".
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": [1, 2], "b": 3})";
    Tree              tree = parser.Parse(text);

    QueryMatcher hasParentArrayOrPair(language, "((number) @n (#has-parent? @n array pair))");
    REQUIRE(hasParentArrayOrPair.Captures(tree.RootNode(), text).size() == 3);

    QueryMatcher                    hasParentObjectOrPair(language, "((number) @n (#has-parent? @n object pair))");
    const std::vector<QueryCapture> objectOrPair = hasParentObjectOrPair.Captures(tree.RootNode(), text);
    REQUIRE(objectOrPair.size() == 1);
    REQUIRE(text.substr(objectOrPair[0].startByte, objectOrPair[0].endByte - objectOrPair[0].startByte) == "3");

    QueryMatcher                    notHasParentObjectOrPair(language, "((number) @n (#not-has-parent? @n object pair))");
    const std::vector<QueryCapture> notObjectOrPair = notHasParentObjectOrPair.Captures(tree.RootNode(), text);
    REQUIRE(notObjectOrPair.size() == 2);
    REQUIRE(text.substr(notObjectOrPair[0].startByte, notObjectOrPair[0].endByte - notObjectOrPair[0].startByte) ==
            "1");
    REQUIRE(text.substr(notObjectOrPair[1].startByte, notObjectOrPair[1].endByte - notObjectOrPair[1].startByte) ==
            "2");
}

TEST_CASE("QueryMatcher::Captures never suppresses a match for a predicate it doesn't recognize", "[Grammar]") {
    // #set! is a real, non-filtering directive query files use for match
    // priority -- and any other unrecognized predicate name gets the same
    // treatment: inert, never suppresses a match. Matches the pre-existing
    // behavior (before predicate evaluation existed at all, every match was
    // unconditionally included) for anything not explicitly handled.
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((string) @s (#set! "priority" 100) (#some-made-up-predicate? @s "x")))");

    const std::vector<QueryCapture> captures = query.Captures(tree.RootNode(), text);

    REQUIRE(captures.size() == 1);
}

TEST_CASE("QueryMatcher::Matches groups captures from the same match together, not scrambled across matches", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1, "b": 2})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, "(pair key: (string) @key value: (number) @value)");

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

TEST_CASE("QueryMatcher::Matches resolves a #set! string operand into setDirectives", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((string) @s (#set! injection.language "javascript")))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].setDirectives.at("injection.language") == "javascript");
}

TEST_CASE("QueryMatcher::Matches stores an empty value for a zero-operand #set! directive", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": 1})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, R"(((string) @s (#set! injection.combined)))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].setDirectives.contains("injection.combined"));
    REQUIRE(matches[0].setDirectives.at("injection.combined").empty());
}

TEST_CASE("QueryMatcher::Matches still respects predicate filtering, e.g. #eq?", "[Grammar]") {
    const Language    language = *LanguageByName("json");
    Parser            parser(language);
    const std::string text = R"({"a": "a", "b": "c"})";
    Tree              tree = parser.Parse(text);
    QueryMatcher      query(language, "(pair key: (string) @key value: (string) @value (#eq? @key @value))");

    const std::vector<QueryMatch> matches = query.Matches(tree.RootNode(), text);

    // Only the "a": "a" pair has an equal key/value.
    REQUIRE(matches.size() == 1);
}

TEST_CASE("QueryMatcher constructor throws on a malformed query", "[Grammar]") {
    const Language language = *LanguageByName("json");
    REQUIRE_THROWS_AS(QueryMatcher(language, "(not_a_real_node_type) @foo"), std::runtime_error);
}

TEST_CASE("Parser is move-constructible and move-assignable", "[Grammar]") {
    Parser parser(*LanguageByName("json"));
    Parser moved(std::move(parser));

    Tree tree = moved.Parse(R"({"a": 1})");
    REQUIRE_FALSE(tree.IsNull());

    Parser other(*LanguageByName("json"));
    other           = std::move(moved);
    Tree secondTree = other.Parse(R"({"b": 2})");
    REQUIRE_FALSE(secondTree.IsNull());
}

TEST_CASE("Tree is move-constructible and move-assignable", "[Grammar]") {
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

TEST_CASE("IncrementalParseCache returns the cached tree unchanged when text is identical", "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;
    const std::string     text = R"({"a": 1})";

    const Tree& first  = cache.Update(parser, text);
    const Tree& second = cache.Update(parser, text);

    REQUIRE(&first == &second);
}

TEST_CASE("IncrementalParseCache's incremental reparse matches a fresh full parse after a single edit", "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    (void)cache.Update(parser, R"({"a": 1, "b": 2})");
    const std::string edited = R"({"a": 100, "b": 2})"; // widens "1" to "100" in place
    const Tree&       tree   = cache.Update(parser, edited);
    REQUIRE_FALSE(tree.IsNull());

    Parser     freshParser(*LanguageByName("json"));
    const Tree freshTree = freshParser.Parse(edited);

    RequireNodesMatch(tree.RootNode(), freshTree.RootNode());
}

// per-subtree-fact-memoization follow-up. Measured live (2026-09-13), not
// assumed: NodeSubtreeIdentity alone is NOT a safe "same content" signal
// across a call to IncrementalParseCache::Update -- only a genuinely SHARED
// subtree (refcount > 1) is guaranteed immutable-and-reused (Green.h:
// "immutable once shared; MakeMut clones on sharing"). Update()'s own
// sequence (lastTree_->Edit(edit); lastTree_ = parser.Parse(newText,
// *lastTree_);) holds exactly ONE Tree/GreenTree alive at a time -- the
// pre-edit value is destroyed by the reassignment, so by the time the
// incremental reparse actually runs, everything along the touched spine has
// refcount 1 and tree-sitter's subtree pool is free (not obligated -- see
// below) to recycle an edited node's OLD allocation for its NEW content
// instead of allocating fresh (upstream's own space-saving design, not a
// bug). Whether that recycling actually happens is an ALLOCATOR ARTIFACT,
// not part of the algorithm's contract: under the default (RelWithDebInfo)
// preset the edited node's address is measured to repeat with new content;
// under the `sanitize` preset ASan's replacement allocator does not hand
// back a just-freed block immediately, so the same probe measures a
// DIFFERENT address instead. Both outcomes are consistent with "no
// guarantee either way" -- which is exactly why this test asserts neither:
// pinning either specific outcome would be pinning an allocator's mood, not
// a real contract, and would make this test allocator/build-dependent.
// What IS deterministic and portable (asserted below, on both presets): an
// UNCHANGED subtree's identity is always preserved -- genuine reuse, not an
// artifact. The next test shows the actual fix for the edited case
// (Tree::Clone(), retaining a second reference across the call).
TEST_CASE("Node subtree identity for an edited node is not a safe content signal unless the prior generation is "
          "retained",
          "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    const Tree& before = cache.Update(parser, R"({"a": 1, "b": 2})");
    // Byte 6 is inside "1" (the "a" pair's value); byte 14 is inside "2"
    // (the "b" pair's value).
    const Node aPairBefore = before.RootNode().NamedDescendantForByteRange(6, 6).Parent();
    const Node bPairBefore = before.RootNode().NamedDescendantForByteRange(14, 14).Parent();
    REQUIRE(aPairBefore.Type() == "pair");
    REQUIRE(bPairBefore.Type() == "pair");
    const void* aIdentityBefore = ned::editor::parse::NodeSubtreeIdentity(aPairBefore.Raw());
    const void* bIdentityBefore = ned::editor::parse::NodeSubtreeIdentity(bPairBefore.Raw());
    REQUIRE(aIdentityBefore != nullptr);
    REQUIRE(bIdentityBefore != nullptr);

    // Only "b"'s value widens (2 -> 200); "a"'s pair is untouched, and the
    // shared prefix up to byte 14 is identical so the same probe point
    // still lands inside "b"'s value in the edited text. NOTHING retains
    // `before` across this call -- it's a dangling-after-Update reference
    // per IncrementalParseCache::Update's own contract, only read before
    // making the call.
    const Tree& after      = cache.Update(parser, R"({"a": 1, "b": 200})");
    const Node  aPairAfter = after.RootNode().NamedDescendantForByteRange(6, 6).Parent();
    const Node  bPairAfter = after.RootNode().NamedDescendantForByteRange(14, 14).Parent();
    REQUIRE(aPairAfter.Type() == "pair");
    REQUIRE(bPairAfter.Type() == "pair");
    // "b"'s content genuinely changed either way -- that part IS reliable
    // and is the actual hazard: a cache keyed on identity alone, with no
    // retained prior generation, cannot tell this apart from true reuse by
    // address comparison alone. bIdentityBefore/bPairAfter are deliberately
    // never compared against each other -- see the header comment above.
    CHECK(ned::editor::parse::NodeSubtreeIdentity(aPairAfter.Raw()) == aIdentityBefore);
}

// The fix the previous test's finding requires: retaining an explicit
// Tree::Clone() of the prior generation across the Update() call that
// produces the next one restores the guarantee -- the retained clone keeps
// every subtree it references at refcount >= 2 for the whole call, so
// tree-sitter's own "immutable once shared" rule applies and an edited
// node's replacement gets a genuinely NEW address instead of recycling the
// old one. This is the shape a real per-subtree fact cache must use: hold
// the Tree its facts were derived against until the next reconciliation.
TEST_CASE("Tree::Clone() retained across Update() makes subtree identity a safe content signal",
          "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    const Tree retainedBefore = cache.Update(parser, R"({"a": 1, "b": 2})").Clone();
    const Node aPairBefore    = retainedBefore.RootNode().NamedDescendantForByteRange(6, 6).Parent();
    const Node bPairBefore    = retainedBefore.RootNode().NamedDescendantForByteRange(14, 14).Parent();
    REQUIRE(aPairBefore.Type() == "pair");
    REQUIRE(bPairBefore.Type() == "pair");
    const void* aIdentityBefore = ned::editor::parse::NodeSubtreeIdentity(aPairBefore.Raw());
    const void* bIdentityBefore = ned::editor::parse::NodeSubtreeIdentity(bPairBefore.Raw());
    REQUIRE(aIdentityBefore != nullptr);
    REQUIRE(bIdentityBefore != nullptr);

    // retainedBefore is still alive here, spanning this call -- the
    // difference from the previous test.
    const Tree& after      = cache.Update(parser, R"({"a": 1, "b": 200})");
    const Node  aPairAfter = after.RootNode().NamedDescendantForByteRange(6, 6).Parent();
    const Node  bPairAfter = after.RootNode().NamedDescendantForByteRange(14, 14).Parent();
    REQUIRE(aPairAfter.Type() == "pair");
    REQUIRE(bPairAfter.Type() == "pair");

    CHECK(ned::editor::parse::NodeSubtreeIdentity(aPairAfter.Raw()) == aIdentityBefore);
    CHECK(ned::editor::parse::NodeSubtreeIdentity(bPairAfter.Raw()) != bIdentityBefore);
}

TEST_CASE("IncrementalParseCache handles an edit that inserts newlines", "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    (void)cache.Update(parser, "{\"a\": 1,\n \"b\": 2}");
    const std::string edited = "{\"a\": 1,\n \"b\": 2,\n \"c\": 3}"; // appends a third key on a new line
    const Tree&       tree   = cache.Update(parser, edited);
    REQUIRE_FALSE(tree.IsNull());

    Parser     freshParser(*LanguageByName("json"));
    const Tree freshTree = freshParser.Parse(edited);

    RequireNodesMatch(tree.RootNode(), freshTree.RootNode());
}

// per-subtree-fact-memoization follow-up: LastEdit() lets a capability
// closure (Mode.cpp's symbolKind, via MatchCache) share this cache's own
// diff instead of re-diffing text independently.
TEST_CASE("IncrementalParseCache::LastEdit reports nullopt on a cache hit and on the first call", "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    REQUIRE_FALSE(cache.LastEdit().has_value()); // nothing diffed yet

    (void)cache.Update(parser, R"({"a": 1})");
    REQUIRE_FALSE(cache.LastEdit().has_value()); // first call: nothing to diff against

    (void)cache.Update(parser, R"({"a": 1})"); // identical text -- cache hit
    REQUIRE_FALSE(cache.LastEdit().has_value());
}

TEST_CASE("IncrementalParseCache::LastEdit reports the exact changed region for a real edit", "[Grammar]") {
    Parser                parser(*LanguageByName("json"));
    IncrementalParseCache cache;

    (void)cache.Update(parser, R"({"a": 1, "b": 2})");
    (void)cache.Update(parser, R"({"a": 1, "b": 200})"); // widens "2" to "200"

    const std::optional<ned::text::ChangedSpan> span = cache.LastEdit();
    REQUIRE(span.has_value());
    // Matches the prefix/suffix diff computed by hand in
    // Tests/MatchCacheTest.cpp for this exact case.
    CHECK(span->oldStart == 15);
    CHECK(span->oldEnd == 15);
    CHECK(span->newStart == 15);
    CHECK(span->newEnd == 17);
}

TEST_CASE("IncrementalParseCache stays correct across a sequence of edits", "[Grammar]") {
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
