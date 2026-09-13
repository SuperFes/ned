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
// MatchCache is that caller. It does NOT need subtree identity
// (parse::NodeSubtreeIdentity) or a retained Tree::Clone() -- an earlier
// design draft assumed it would, and that assumption uncovered a real
// hazard (see Tree.h's own doc comment on Clone()), but this cache's actual
// reconciliation is pure byte-range arithmetic over its own PREVIOUS
// result, using exactly the same "single contiguous changed region" model
// Text/OffsetRemap.h already established for LSP diagnostic relocation:
// a cached match entirely before the edit is kept as-is, one entirely
// after is shifted by the edit's length delta, and anything overlapping
// the edit is dropped and re-derived via MatchesInRange over the gap
// between its own surviving neighbors (see MatchCache.cpp's own comment on
// why that gap, not a fixed pad, is what makes the redo window sound). No
// comparison against the prior tree is ever needed, so no prior generation
// needs to be kept alive.
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
// KNOWN RESIDUAL GAP, not yet closed (see Tests/ParseConformanceTest.cpp's
// "MatchCache reconciliation..." test, which pins the exact known-failing
// cases): pure grammar-AMBIGUITY reclassification, with no parse error and
// no external-scanner involvement, can still occasionally reclassify a
// match near an edit differently than a full recompute would -- e.g. Go's
// "Grouped var declarations" corpus case, where a token inserted
// immediately before a bare identifier can flip it between @variable and
// @type. No structural flag on the tree currently signals this case the
// way HasError/HasExternalTokens do; closing it is future work.
//

#ifndef NED_EDITOR_TREESITTER_MATCHCACHE_H
#define NED_EDITOR_TREESITTER_MATCHCACHE_H

#include <optional>
#include <string_view>
#include <vector>

#include "QueryMatcher.h"
#include "Text/OffsetRemap.h"

namespace ned::editor::treesitter {

class MatchCache {
  public:
    // Reconciles this cache's stored matches against `root`/`text` and
    // returns the up-to-date match list, sorted by rootStartByte.
    //
    // `matcher` must be the SAME QueryMatcher instance (same compiled
    // pattern set) across this cache's lifetime -- passed per call rather
    // than stored, matching every Mode.cpp capability closure's own shape
    // (a shared_ptr<QueryMatcher> captured alongside a shared_ptr to this).
    //
    // `edit` is the single changed region between the text this cache last
    // reconciled against and `text` now -- nullopt forces a full,
    // unwindowed re-derive (first call, or a caller that doesn't want
    // incremental reuse this time, e.g. a whole-buffer programmatic
    // replace). Passing an `edit` that does not actually describe what
    // changed since the last call is a caller bug this cache cannot
    // detect -- it trusts `edit` the same way Text/OffsetRemap.h's own
    // callers do.
    [[nodiscard]] std::vector<QueryMatch> Reconcile(const QueryMatcher& matcher, const Node& root,
                                                    std::string_view text, std::optional<text::ChangedSpan> edit);

  private:
    std::vector<QueryMatch> cached_;
    bool                    hasCached_            = false;
    bool                    cachedNeededFullWalk_ = false;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_MATCHCACHE_H
