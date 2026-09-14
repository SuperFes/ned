//
// per-subtree-fact-memoization follow-up (ROADMAP: Phase 4b remaining payoffs).
//
// The problem: every Mode capability whose result is a flat set of query
// matches (highlight via captures grouped per pattern, symbolKind, locals,
// indent captures) re-derives its ENTIRE match set from a full QueryMatcher
// walk on every call, even when only a small contiguous region of the
// document changed since the last call. `QueryMatcher::MatchesInRange`
// already exists and already prunes tree traversal to whatever intersects a
// byte window (see its own doc comment) -- what's missing is a caller that
// remembers the PREVIOUS call's result and only asks MatchesInRange for the
// slice that could have changed, instead of re-running the query over the
// whole document every time.
//
// MatchCache is that caller. Its core reconciliation is pure byte-range
// arithmetic over its own PREVIOUS result, using exactly the same "single
// contiguous changed region" model Text/OffsetRemap.h already established
// for LSP diagnostic relocation: a cached match entirely before the edit is
// kept as-is, one entirely after is shifted by the edit's length delta, and
// anything overlapping the edit is dropped and re-derived via
// MatchesInRange over the gap between its own surviving neighbors (see
// MatchCache.cpp's own comment on why that gap, not a fixed pad, is what
// makes the redo window sound). This part needs no subtree identity
// (parse::NodeSubtreeIdentity) or prior-tree retention at all -- an earlier
// design draft assumed it would, and that assumption uncovered a real
// hazard (see Tree.h's own doc comment on Clone()).
//
// ambiguity-reclassification follow-up: a SECOND, narrower mechanism does
// retain one prior generation (a Tree::Clone(), the same cheap
// shared_ptr-copy Tree.h's own doc comment describes -- no reparse, no deep
// copy, and structurally-shared subtrees stay alive via the NEW tree's own
// references regardless, so the only incremental memory cost is whatever
// the edit actually replaced). Its only job is widening the redo window
// past what the byte-gap logic alone would compute, for exactly the class
// of gap the two full-walk gates below can't see either: a production's
// SHAPE changing because the surrounding token stream changed, with the
// affected node's own bytes untouched (see the KNOWN RESIDUAL GAP note
// below for the case this closes and why dynamicPrecedence -- the more
// surgical-looking alternative -- doesn't work). It compares the smallest
// named node containing the edit's OLD span (against the retained prior
// tree) with the smallest named node containing its NEW span (against the
// current tree); a type or (shifted) range mismatch means the enclosing
// structure changed shape, and the redo window widens to cover the union
// of both nodes' ranges. This can only WIDEN what gets re-derived, never
// narrow it, so it cannot make an already-correct reconciliation wrong --
// only, in the common case where nothing structural changed, cost one
// extra pair of O(depth) tree walks per edit for no benefit.
//
// Excluded from this entirely, always falling back to a full unwindowed
// re-derive:
//
//  - A query set containing ANY pattern flagged QueryMatch::ancestorCrossing
//    (see QueryPredicates.h's PredicateReadsOutsideSubtree) -- such a
//    pattern's result can change outside the edited byte range (its
//    ancestry changed, not its own bytes), which no byte-range-scoped
//    reconciliation can detect. This is a whole-query-set decision
//    (QueryMatcher::AncestorCrossingPatternCount(), fixed for the life of a
//    compiled matcher), not a per-match one: MatchesInRange has no way to
//    ask for "only these patterns," so getting incremental benefit for the
//    REST of a query set that contains even one such pattern would need a
//    new QueryMatcher entry point this file doesn't build.
//
//  - A tree with a parse ERROR anywhere in it, on either side of the edit.
//    Measured live (2026-09-13) against real bash corpus text: an edit that
//    introduces or resolves a syntax error can change how content FAR AWAY
//    from the edit -- both before and after it -- is interpreted, because
//    error recovery/ambiguity resolution is a property of the WHOLE parse,
//    not a local one. A real case: splitting an identifier mid-word broke
//    one statement, and a completely unrelated "fi" keyword 50 bytes
//    earlier in the same file switched from @keyword to @function in the
//    resulting (correct) parse. No local window, however it's computed,
//    can bound that.
//
//  - A tree with EXTERNAL SCANNER involvement anywhere in it
//    (parse::NodeHasExternalTokens -- aggregated up the tree the same way
//    error cost is, Green.cpp's SubtreeCombine: a parent's hasExternalTokens
//    is set if any child's is). Also measured live against real bash corpus
//    text: heredocs and similar constructs are exactly the class whose true
//    extent a local byte window cannot bound, for the same reason as parse
//    errors -- this single gate took a differential run over real corpus
//    text + a scripted edit sequence from 818 divergences down to 9.
//
// Both gates are sticky across the call that resolves the condition too
// (cachedNeededFullWalk_), not just the cache's own prior generation: an
// edit that fixes the error/external-token condition still needs one more
// full walk to reestablish a trustworthy baseline.
//
// RESIDUAL GAP -- CLOSED (2026-09-13, ambiguity-reclassification follow-up).
// Was: pure grammar-AMBIGUITY reclassification, with no parse error and no
// external-scanner involvement, could occasionally reclassify a match near
// an edit differently than a full recompute would -- e.g. Go's "Grouped var
// declarations" corpus case, where a token inserted immediately before a
// bare identifier flips it between @variable and @type with the
// identifier's OWN bytes untouched. Pinned at exactly 9 known-failing cases
// across Tests/ParseConformanceTest.cpp's own corpus+edit-script
// differential (bash, clojure x2, go x5) -- a real, understood gap, not
// silently accepted, but also not blocking indefinitely (same precedent as
// that file's own scratchDivergences==7).
//
// A candidate structural FLAG was investigated and DISPROVEN first:
// Subtree::dynamicPrecedence (Green.h, aggregated up the tree the same way
// errorCost/hasExternalTokens are, per grammar.js's prec.dynamic(n, rule)
// declarations -- Go's own grammar.js declares exactly this for
// $._type_identifier, the symbol "zero" gets reclassified to) looked like a
// plausible signal: "this node's own reduce was one the grammar declared as
// ambiguity-prone." Live-verified false by dumping the reparsed tree's
// per-node dynamicPrecedence (own contribution, i.e. the aggregate minus the
// sum of direct children's own -- the reverse of Green.cpp's
// SubtreeSummarizeChildren): the reclassified type_identifier node carries
// dynamicPrecedence == 0 despite grammar.js's declared -1, because
// dynamicPrecedence is only ever written into a REDUCE action's table entry
// when the table GENERATOR found an actual LR conflict to disambiguate at
// that state -- the var_spec shape choice here ("does a second identifier
// start a Type or continue an identifier_list") is a perfectly deterministic
// lookahead decision in the compiled table, not a GLR fork, so no precedence
// value was ever attached to it. No structural flag on the NEW tree alone
// can see this class of change (a production's shape changing because the
// surrounding token stream changed, with the reclassified node's own bytes
// untouched) -- what closed it instead is the OLD-vs-NEW structural
// comparison described above (retained Tree::Clone(), smallest enclosing
// named node on each side). Confirmed against the same corpus+edit-script
// differential: 0 divergences, down from 9, with no other test in the suite
// regressing (the mechanism can only widen a redo window, never narrow one).
//

#ifndef NED_EDITOR_TREESITTER_MATCHCACHE_H
#define NED_EDITOR_TREESITTER_MATCHCACHE_H

#include <optional>
#include <string_view>
#include <vector>

#include "QueryMatcher.h"
#include "Text/OffsetRemap.h"
#include "Tree.h"

namespace ned::editor::treesitter {

class MatchCache {
  public:
    // Reconciles this cache's stored matches against `tree`/`text` and
    // returns the up-to-date match list, sorted by rootStartByte.
    //
    // `matcher` must be the SAME QueryMatcher instance (same compiled
    // pattern set) across this cache's lifetime -- passed per call rather
    // than stored, matching every Mode.cpp capability closure's own shape
    // (a shared_ptr<QueryMatcher> captured alongside a shared_ptr to this).
    //
    // `tree` (ambiguity-reclassification follow-up: a Tree rather than just
    // its RootNode(), so this cache can retain a Clone() for the structural
    // widening check described above) must be the tree `text` was parsed
    // into.
    //
    // `edit` is the single changed region between the text this cache last
    // reconciled against and `text` now -- nullopt forces a full,
    // unwindowed re-derive (first call, or a caller that doesn't want
    // incremental reuse this time, e.g. a whole-buffer programmatic
    // replace). Passing an `edit` that does not actually describe what
    // changed since the last call is a caller bug this cache cannot
    // detect -- it trusts `edit` the same way Text/OffsetRemap.h's own
    // callers do.
    [[nodiscard]] std::vector<QueryMatch> Reconcile(const QueryMatcher& matcher, const Tree& tree,
                                                    std::string_view text, std::optional<text::ChangedSpan> edit);

  private:
    std::vector<QueryMatch> cached_;
    bool                    hasCached_            = false;
    bool                    cachedNeededFullWalk_ = false;
    // ambiguity-reclassification follow-up: a Clone() of the tree `cached_`
    // was reconciled against, retained ONLY for the structural widening
    // check's old-vs-new comparison -- nullopt before the first call. See
    // the header comment above and Tree::Clone()'s own doc comment for why
    // retaining this is cheap.
    std::optional<Tree> priorTree_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_MATCHCACHE_H
