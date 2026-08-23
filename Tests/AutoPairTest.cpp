#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "Editor/AutoPair.h"
#include "Editor/Mode.h"

using ned::editor::AutoPairQuery;
using ned::editor::DecideSelfInsert;
using ned::editor::DefaultAutoPairs;
using ned::editor::PairAction;
using ned::editor::ShouldDeleteAdjacentPair;
using ned::editor::SyntaxClass;

namespace {
const std::vector<std::pair<char, char>>& kPairs = DefaultAutoPairs();

// AutoPairEnabled is process-wide state (see AutoPair.h); every test that
// flips it must leave it default-on for the next test, guaranteed via RAII --
// same AutoRevertGuard pattern AutoRevertTest.cpp already establishes.
struct AutoPairEnabledGuard {
    ~AutoPairEnabledGuard() {
        ned::editor::SetAutoPairEnabled(true);
    }
};
} // namespace

TEST_CASE("DecideSelfInsert pairs a bracket opener at end of line", "[AutoPair]") {
    AutoPairQuery query;
    query.typed = '(';
    query.pairs = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPair);
}

TEST_CASE("DecideSelfInsert pairs a bracket opener even directly before an existing closer", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = '(';
    query.charAfter = ")";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPair);
}

TEST_CASE("DecideSelfInsert skips over a redundant bracket closer", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = ')';
    query.charAfter = ")";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::SkipOver);
}

TEST_CASE("DecideSelfInsert inserts a bracket closer plainly when nothing to skip over", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = ')';
    query.charAfter = "x";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert pairs a quote at end of buffer", "[AutoPair]") {
    AutoPairQuery query;
    query.typed = '"';
    query.pairs = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPair);
}

TEST_CASE("DecideSelfInsert pairs a quote before sane boundary punctuation", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = '"';
    query.charAfter = ")";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPair);
}

TEST_CASE("DecideSelfInsert skips over a redundant quote closer", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = '"';
    query.charAfter = "\"";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::SkipOver);
}

TEST_CASE("DecideSelfInsert does not pair a quote right after a word character (contraction)", "[AutoPair]") {
    AutoPairQuery query;
    query.typed      = '\'';
    query.charBefore = "n"; // "don|'t" -- point right after the 'n' in "don"
    query.pairs      = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert does not pair a quote when point is inside an existing string", "[AutoPair]") {
    AutoPairQuery query;
    query.typed        = '"';
    query.classAtPoint = SyntaxClass::String;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert does not pair a quote when point is inside a comment", "[AutoPair]") {
    AutoPairQuery query;
    query.typed        = '"';
    query.classAtPoint = SyntaxClass::Comment;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert does not pair a quote when the next character is mid-word", "[AutoPair]") {
    AutoPairQuery query;
    query.typed     = '"';
    query.charAfter = "x";
    query.pairs     = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert pairs a quote typed right after an auto-paired bracket (if (\" scenario)", "[AutoPair]") {
    // "if (" -- the "(" already auto-paired to "if (|)" (point between the
    // pair), then '"' is typed: charBefore is the bracket opener, charAfter
    // is its closer. Neither should block quote pairing.
    AutoPairQuery query;
    query.typed      = '"';
    query.charBefore = "(";
    query.charAfter  = ")";
    query.pairs      = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPair);
}

TEST_CASE("DecideSelfInsert wraps a selection when the typed character is an opener", "[AutoPair]") {
    AutoPairQuery query;
    query.typed       = '(';
    query.hasSelection = true;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::WrapSelection);
}

TEST_CASE("DecideSelfInsert wraps a selection when the typed character is a symmetric quote", "[AutoPair]") {
    AutoPairQuery query;
    query.typed        = '"';
    query.hasSelection = true;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::WrapSelection);
}

TEST_CASE("DecideSelfInsert does not wrap a selection for a bare closer", "[AutoPair]") {
    AutoPairQuery query;
    query.typed        = ')';
    query.hasSelection = true;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert leaves an ordinary character alone regardless of context", "[AutoPair]") {
    AutoPairQuery query;
    query.typed        = 'x';
    query.hasSelection = true;
    query.pairs        = &kPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("DecideSelfInsert does nothing when no pair table is configured", "[AutoPair]") {
    AutoPairQuery query;
    query.typed = '(';
    query.pairs = nullptr;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
    query.pairs = nullptr;
}

TEST_CASE("DecideSelfInsert leaves a single quote alone under LispAutoPairs (the reader's quote macro, not a delimiter)",
          "[AutoPair]") {
    const std::vector<std::pair<char, char>>& lispPairs = ned::editor::LispAutoPairs();
    AutoPairQuery                             query;
    query.typed = '\'';
    query.pairs = &lispPairs;
    REQUIRE(DecideSelfInsert(query) == PairAction::InsertPlain);
}

TEST_CASE("LispAutoPairs still pairs double quotes and every bracket", "[AutoPair]") {
    const std::vector<std::pair<char, char>>& lispPairs = ned::editor::LispAutoPairs();

    AutoPairQuery quote;
    quote.typed = '"';
    quote.pairs = &lispPairs;
    REQUIRE(DecideSelfInsert(quote) == PairAction::InsertPair);

    AutoPairQuery paren;
    paren.typed = '(';
    paren.pairs = &lispPairs;
    REQUIRE(DecideSelfInsert(paren) == PairAction::InsertPair);
}

TEST_CASE("AutoPairEnabled defaults to true and round-trips through SetAutoPairEnabled", "[AutoPair]") {
    AutoPairEnabledGuard guard;
    REQUIRE(ned::editor::AutoPairEnabled()); // documented default

    ned::editor::SetAutoPairEnabled(false);
    REQUIRE_FALSE(ned::editor::AutoPairEnabled());

    ned::editor::SetAutoPairEnabled(true);
    REQUIRE(ned::editor::AutoPairEnabled());
}

TEST_CASE("ShouldDeleteAdjacentPair is true for an empty bracket pair", "[AutoPair]") {
    REQUIRE(ShouldDeleteAdjacentPair("(", ")", kPairs));
}

TEST_CASE("ShouldDeleteAdjacentPair is true for an empty quote pair", "[AutoPair]") {
    REQUIRE(ShouldDeleteAdjacentPair("\"", "\"", kPairs));
}

TEST_CASE("ShouldDeleteAdjacentPair is false when the characters don't match a configured pair", "[AutoPair]") {
    REQUIRE_FALSE(ShouldDeleteAdjacentPair("(", "x", kPairs));
    REQUIRE_FALSE(ShouldDeleteAdjacentPair("(", "]", kPairs));
}

TEST_CASE("ShouldDeleteAdjacentPair is false at a buffer boundary", "[AutoPair]") {
    REQUIRE_FALSE(ShouldDeleteAdjacentPair("", ")", kPairs));
    REQUIRE_FALSE(ShouldDeleteAdjacentPair("(", "", kPairs));
}
