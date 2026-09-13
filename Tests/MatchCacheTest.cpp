//
// per-subtree-fact-memoization follow-up. Pure/standalone per MatchCache.h's
// own design: no Mode/Buffer/Screen involved, just QueryMatcher + a bundled
// grammar. The load-bearing property every test here pins: reconciling
// incrementally must always equal a fresh full Matches() call on the same
// text -- the chunking/shifting is a scheduling optimization, never a
// rendering one (same bar ChunkedHighlightTest.cpp already holds its own
// windowing to).
//

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/MatchCache.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/QueryMatcher.h"
#include "Editor/TreeSitter/Tree.h"
#include "Text/OffsetRemap.h"

using namespace ned::editor::treesitter;
using ned::text::ChangedByteRange;
using ned::text::ChangedSpan;

namespace {

// A minimal ITextStorage-free stand-in ChangedByteRange can't read directly
// (it wants ITextStorage), so this test computes the span the same way
// IncrementalParseCache does: plain string prefix/suffix diffing. Small and
// local -- these are short, hand-crafted texts, not a huge-buffer path.
ChangedSpan DiffSpan(const std::string& oldText, const std::string& newText) {
    const std::size_t maxCommon = std::min(oldText.size(), newText.size());
    std::size_t       prefix    = 0;
    while (prefix < maxCommon && oldText[prefix] == newText[prefix]) {
        ++prefix;
    }
    const std::size_t maxSuffix = maxCommon - prefix;
    std::size_t       suffix    = 0;
    while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
        ++suffix;
    }
    return ChangedSpan{
        .oldStart = prefix,
        .oldEnd   = oldText.size() - suffix,
        .newStart = prefix,
        .newEnd   = newText.size() - suffix,
    };
}

std::string DescribeMatch(const QueryMatch& match) {
    std::string out = "{root[" + std::to_string(match.rootStartByte) + "," + std::to_string(match.rootEndByte) + ")";
    for (const auto& capture : match.captures) {
        out += " @" + capture.name + "[" + std::to_string(capture.startByte) + "," + std::to_string(capture.endByte) +
               ")";
    }
    out += "}";
    return out;
}

std::vector<std::string> DescribeAll(const std::vector<QueryMatch>& matches) {
    std::vector<std::string> out;
    out.reserve(matches.size());
    for (const QueryMatch& match : matches) {
        out.push_back(DescribeMatch(match));
    }
    std::sort(out.begin(), out.end());
    return out;
}

void RequireMatchesFreshRecompute(const QueryMatcher& matcher, const std::string& text,
                                  const std::vector<QueryMatch>& reconciled) {
    Parser     parser(*LanguageByName("json"));
    const Tree fresh = parser.Parse(text);
    const auto want  = DescribeAll(matcher.Matches(fresh.RootNode(), text));
    const auto got   = DescribeAll(reconciled);
    REQUIRE(got == want);
}

} // namespace

TEST_CASE("MatchCache's first Reconcile (no edit) is a full derive, matching Matches() directly", "[MatchCache]") {
    const auto        language = *LanguageByName("json");
    QueryMatcher      matcher(language, "(pair key: (string) @key value: (_) @value)");
    Parser            parser(language);
    const std::string text = R"({"a": 1, "b": 2})";
    const Tree        tree = parser.Parse(text);

    MatchCache cache;
    const auto reconciled = cache.Reconcile(matcher, tree.RootNode(), text, std::nullopt);

    RequireMatchesFreshRecompute(matcher, text, reconciled);
    REQUIRE(reconciled.size() == 2);
}

TEST_CASE("MatchCache reconciles a localized edit to match a fresh full recompute", "[MatchCache]") {
    const auto   language = *LanguageByName("json");
    QueryMatcher matcher(language, "(pair key: (string) @key value: (_) @value)");
    Parser       parser(language);

    const std::string before     = R"({"a": 1, "b": 2, "c": 3})";
    const Tree        treeBefore = parser.Parse(before);

    MatchCache cache;
    (void)cache.Reconcile(matcher, treeBefore.RootNode(), before, std::nullopt);

    // Widen "b"'s value only; "a" and "c" pairs are untouched.
    const std::string after     = R"({"a": 1, "b": 200, "c": 3})";
    const ChangedSpan span      = DiffSpan(before, after);
    const Tree        treeAfter = parser.Parse(after);

    const auto reconciled = cache.Reconcile(matcher, treeAfter.RootNode(), after, span);
    RequireMatchesFreshRecompute(matcher, after, reconciled);
    REQUIRE(reconciled.size() == 3);
}

TEST_CASE("MatchCache stays correct across a sequence of incremental edits (typing simulation)", "[MatchCache]") {
    const auto   language = *LanguageByName("json");
    QueryMatcher matcher(language, "(pair key: (string) @key value: (_) @value)");
    Parser       parser(language);

    const std::vector<std::string> steps = {
        R"({"a": ""})",
        R"({"a": "h"})",
        R"({"a": "he"})",
        R"({"a": "hel"})",
        R"({"a": "hell"})",
        R"({"a": "hello"})",
        R"({"a": "hello", "b": 1})",
    };

    MatchCache  cache;
    std::string previous;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Tree tree = parser.Parse(steps[i]);
        const auto reconciled =
            i == 0 ? cache.Reconcile(matcher, tree.RootNode(), steps[i], std::nullopt)
                   : cache.Reconcile(matcher, tree.RootNode(), steps[i], DiffSpan(previous, steps[i]));
        INFO("step " << i << ": " << steps[i]);
        RequireMatchesFreshRecompute(matcher, steps[i], reconciled);
        previous = steps[i];
    }
}

TEST_CASE("MatchCache handles a deletion that merges two previously-separate matches into one",
          "[MatchCache]") {
    // Deleting the ", " between two array numbers merges "12" and "34" into
    // a single "1234" -- the exact boundary-merge hazard a byte-range-only
    // cache must not miss: both old matches TOUCH the deleted region on one
    // side, and the edit collapses to a zero-width new span (a pure
    // deletion), which is also what exercises the padded redo window (an
    // unpadded MatchesInRange([newStart, newStart)) window would match
    // nothing at all).
    const auto   language = *LanguageByName("json");
    QueryMatcher matcher(language, "(number) @num");
    Parser       parser(language);

    const std::string before     = "[12, 34]";
    const Tree        treeBefore = parser.Parse(before);
    MatchCache        cache;
    (void)cache.Reconcile(matcher, treeBefore.RootNode(), before, std::nullopt);

    const std::string after = "[1234]";
    const ChangedSpan span  = DiffSpan(before, after);
    REQUIRE(span.newStart == span.newEnd); // confirms this is the zero-width-window case
    const Tree treeAfter = parser.Parse(after);

    const auto reconciled = cache.Reconcile(matcher, treeAfter.RootNode(), after, span);
    RequireMatchesFreshRecompute(matcher, after, reconciled);
    REQUIRE(reconciled.size() == 1); // "12" and "34" merged into one "1234"
}

TEST_CASE("MatchCache handles an insertion that extends an existing token right up to its edge",
          "[MatchCache]") {
    // The case that originally caught the boundary bug: "2" -> "200" diffs
    // as a zero-width insertion whose oldStart lands exactly on the "2"
    // token's own end byte.
    const auto   language = *LanguageByName("json");
    QueryMatcher matcher(language, "(number) @num");
    Parser       parser(language);

    const std::string before     = "[1, 2, 3]";
    const Tree        treeBefore = parser.Parse(before);
    MatchCache        cache;
    (void)cache.Reconcile(matcher, treeBefore.RootNode(), before, std::nullopt);

    const std::string after = "[1, 200, 3]";
    const ChangedSpan span  = DiffSpan(before, after);
    REQUIRE(span.oldStart == span.oldEnd); // confirms this is the touching-boundary case
    const Tree treeAfter = parser.Parse(after);

    const auto reconciled = cache.Reconcile(matcher, treeAfter.RootNode(), after, span);
    RequireMatchesFreshRecompute(matcher, after, reconciled);
    REQUIRE(reconciled.size() == 3);
}

TEST_CASE("MatchCache always fully re-derives a query set containing an ancestor-crossing pattern", "[MatchCache]") {
    // #has-ancestor? makes this pattern's result depend on structure outside
    // its own node -- MatchCache must never trust a byte-range-scoped
    // reconciliation for it.
    const auto   language = *LanguageByName("json");
    QueryMatcher matcher(language, R"(((number) @nested (#has-ancestor? @nested pair)))");
    REQUIRE(matcher.AncestorCrossingPatternCount() == 1);
    Parser parser(language);

    const std::string before     = R"({"a": 1, "b": 2})";
    const Tree        treeBefore = parser.Parse(before);
    MatchCache        cache;
    (void)cache.Reconcile(matcher, treeBefore.RootNode(), before, std::nullopt);

    const std::string after      = R"({"a": 1, "b": 200})";
    const Tree        treeAfter  = parser.Parse(after);
    const auto        reconciled = cache.Reconcile(matcher, treeAfter.RootNode(), after, DiffSpan(before, after));

    RequireMatchesFreshRecompute(matcher, after, reconciled);
    REQUIRE(reconciled.size() == 2);
}
