//
// Incremental reuse of a query's match set across edits. Every Mode
// capability whose result is a flat set of query matches (fold, symbolKind,
// locals, indent captures, test discovery, ...) would otherwise re-run its
// query over the whole document on every call. MatchCache remembers the
// previous call's matches and asks QueryMatcher::MatchesInRange only for the
// slice that could have changed.
//
// Reconciliation is byte-range arithmetic over the previous result: a match
// entirely before the changed region is kept as-is, one entirely after is
// shifted by the edit's length delta, and anything in between is dropped and
// re-derived via MatchesInRange over the gap between its surviving
// neighbors (MatchCache.cpp on why that gap, not a fixed pad, is what makes
// the redo window sound). The changed region is the edit's own span widened
// by every range where the reparse changed the tree's structure
// (TreeEdit::structuralChanges, parse::ChangedRanges) -- which is what covers
// an edit reinterpreting text away from itself: a grammar ambiguity
// reclassifying a neighbouring identifier, a heredoc terminator split so the
// body runs to the end of the file, an indent change re-nesting later lines.
//
// That also covers patterns whose predicates read outside their own subtree
// (#has-ancestor?/#has-parent?, QueryPredicates.h's
// PredicateReadsOutsideSubtree): they test the types of a node's visible
// ancestors, and a change to those is a change to the node types enclosing
// every byte of the node, which is exactly what a changed range reports --
// so such a match's root lies in one and is re-derived.
//
// A tree with a parse ERROR anywhere in it always falls back to a full,
// unwindowed re-derive. Measured against real bash corpus text: error
// recovery is a property of the whole parse, and a split identifier flipped
// an unrelated "fi" 50 bytes earlier from @keyword to @function. Sticky for
// one more call after the error clears (cachedNeededFullWalk_), to
// reestablish a trustworthy baseline.
//
// Tests/ParseConformanceTest.cpp holds Reconcile against a fresh Matches()
// over every scripted corpus edit of the bundled grammars' real highlight
// queries.
//

#ifndef NED_EDITOR_GRAMMAR_MATCHCACHE_H
#define NED_EDITOR_GRAMMAR_MATCHCACHE_H

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "IncrementalParse.h"
#include "QueryMatcher.h"
#include "Tree.h"
#include "TreeEdit.h"

namespace ned::editor::grammar {

class MatchCache {
  public:
    // Reconciles this cache's stored matches against `tree`/`text` and
    // returns the up-to-date match list, sorted by rootStartByte -- valid
    // until the next Reconcile call.
    //
    // `matcher` must be the SAME QueryMatcher instance (same compiled
    // pattern set) across this cache's lifetime -- passed per call rather
    // than stored, matching every Mode.cpp capability closure's own shape
    // (a shared_ptr<QueryMatcher> captured alongside a shared_ptr to this).
    //
    // `tree` must be the tree `text` was parsed into.
    //
    // `edit` is the transition between the text this cache last reconciled
    // against and `text` now -- nullopt forces a full, unwindowed re-derive.
    // Passing an `edit` that does not describe what changed since the last
    // call is a caller bug this cache cannot detect.
    [[nodiscard]] const std::vector<QueryMatch>& Reconcile(const QueryMatcher& matcher, const Tree& tree,
                                                           std::string_view text, const std::optional<TreeEdit>& edit);

    // The same against `parse`'s current generation (`tree` is what its
    // Update() returned for `text`), taking the edit from the generation this
    // cache last reconciled against rather than from whichever caller last
    // advanced `parse` -- the only correct edit when several capabilities
    // share one IncrementalParseCache, since any of them may have seen the
    // new text first and a cache that skipped a generation has no single
    // edit to its baseline at all (it re-derives fully).
    [[nodiscard]] const std::vector<QueryMatch>& Reconcile(const QueryMatcher& matcher, const Tree& tree,
                                                           std::string_view text, const IncrementalParseCache& parse);

  private:
    std::vector<QueryMatch> cached_;
    bool                    hasCached_            = false;
    bool                    cachedNeededFullWalk_ = false;
    // The IncrementalParseCache generation cached_ matches, when it came from
    // the generation-tracking overload.
    std::optional<std::uint64_t> generation_;
};

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_MATCHCACHE_H
