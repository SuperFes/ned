#include "MatchCache.h"

#include <algorithm>

#include "Editor/Parse/Node.h"

namespace ned::editor::treesitter {

namespace {

    bool ByRootStartByte(const QueryMatch& a, const QueryMatch& b) {
        return a.rootStartByte < b.rootStartByte;
    }

    // Shifts every byte offset in `match` by the edit's length delta --
    // valid only for a match this cache already knows lies entirely AFTER
    // the edit (RemapOffset's "after" branch), since that's the only case
    // where every one of a match's captures is guaranteed on the same side
    // of the edit as its root.
    void ShiftAfterEdit(QueryMatch& match, const text::ChangedSpan& span) {
        match.rootStartByte = text::RemapOffset(match.rootStartByte, span);
        match.rootEndByte   = text::RemapOffset(match.rootEndByte, span);
        for (QueryMatchCapture& capture : match.captures) {
            capture.startByte = text::RemapOffset(capture.startByte, span);
            capture.endByte   = text::RemapOffset(capture.endByte, span);
        }
    }

    // A query set containing even one ancestor-crossing pattern can't be
    // reconciled by byte-range alone (see MatchCache.h's header comment) --
    // MatchesInRange has no per-pattern filter, so incrementally reusing the
    // REST of such a query set would need a new QueryMatcher entry point.
    // Deliberately not built for v1: every bundled language's highlight
    // query set with an ancestor-crossing pattern (c, cpp) is exactly the
    // set ROADMAP already names as "parse-bound," i.e. dominated by the
    // underlying reparse cost this cache was never going to touch anyway.
    bool AlwaysFullWalk(const QueryMatcher& matcher) {
        return matcher.AncestorCrossingPatternCount() > 0;
    }

} // namespace

std::vector<QueryMatch> MatchCache::Reconcile(const QueryMatcher& matcher, const Node& root, std::string_view text,
                                              std::optional<text::ChangedSpan> edit) {
    // See MatchCache.h's header comment for why either of these forces a
    // full walk instead of an incremental reconciliation: a parse error's
    // recovery is a whole-parse property (a real bash case flipped an
    // unrelated keyword's classification 50 bytes from the edit with no
    // overlap at all), and external-scanner involvement (heredocs and
    // similar) is exactly the class of construct whose extent a local byte
    // window cannot bound -- measured live to matter for real bash corpus
    // text (818 -> 9 divergences on this file's own corpus+edit-script
    // differential once this gate was added). Sticky across the CURRENT
    // call too, not just cached_'s own generation: an edit that resolves an
    // error/external-token condition still needs one more full walk to
    // reestablish a trustworthy baseline before incremental reuse resumes.
    const bool needsFullWalk = parse::NodeHasError(root.Raw()) || parse::NodeHasExternalTokens(root.Raw());
    if (!hasCached_ || !edit.has_value() || AlwaysFullWalk(matcher) || needsFullWalk || cachedNeededFullWalk_) {
        cached_ = matcher.Matches(root, text);
        std::sort(cached_.begin(), cached_.end(), ByRootStartByte);
        hasCached_            = true;
        cachedNeededFullWalk_ = needsFullWalk;
        return cached_;
    }

    // Strict (<, >), deliberately NOT RemapOffset's own <=/>= convention --
    // measured live (2026-09-13) to matter: a prefix/suffix text diff can
    // split what becomes a single re-lexed token (e.g. "2" -> "200" diffs
    // as "insert 00 right after the 2", oldStart == oldEnd == 15, touching
    // the "2" token's own end byte exactly). RemapOffset's <=/>= is the
    // right convention for relocating a POINT (a cursor sitting exactly at
    // an insertion point should not be shoved forward) but the wrong one
    // here: a cached match merely TOUCHING the edit boundary can still have
    // been re-lexed into something new, so it must be dropped and
    // re-derived, not kept because "the point comparison says before/after."
    //
    // The redo window is NOT a fixed pad around the edit -- measured live
    // (2026-09-13, real bash corpus text) to be insufficient: a statement's
    // boundary can move because of what follows the whitespace the edit
    // touches, not because the statement's own bytes changed (inserting a
    // newline after "file1.txt " in "cmd file1.txt file2.txt" ends the
    // command right after "file1.txt", one real byte before the edit's own
    // position -- a fixed small pad keeps missing this by however much
    // intervening insignificant content there happens to be, which has no
    // bound). Instead the window is derived from THIS cache's own surviving
    // neighbors: everything from the end of the nearest kept-before match to
    // the start of the nearest kept-after match is unaccounted for and must
    // be re-derived, however wide that turns out to be -- proportional to
    // local structure, not a guessed constant. A wider enclosing match is
    // still found via MatchesInRange's own "captures outside the window
    // still arrive" intersection contract even when this window is narrow.
    const text::ChangedSpan& span = *edit;
    std::vector<QueryMatch>  result;
    result.reserve(cached_.size());
    std::size_t redoStart = 0;
    std::size_t redoEnd   = text.size();
    for (QueryMatch& match : cached_) {
        if (match.rootEndByte < span.oldStart) {
            redoStart = std::max(redoStart, match.rootEndByte);
            result.push_back(std::move(match)); // entirely before, with a real gap -- unaffected
        }
        else if (match.rootStartByte > span.oldEnd) {
            ShiftAfterEdit(match, span);
            redoEnd = std::min(redoEnd, match.rootStartByte); // post-shift
            result.push_back(std::move(match));               // entirely after, with a real gap -- shift, don't re-derive
        }
        // else: overlaps OR touches the edit -- dropped; MatchesInRange below
        // re-derives it (and anything else whose root now intersects the
        // window).
    }

    std::vector<QueryMatch> redone = matcher.MatchesInRange(root, text, redoStart, redoEnd);
    for (QueryMatch& match : redone) {
        result.push_back(std::move(match));
    }

    std::sort(result.begin(), result.end(), ByRootStartByte);
    cached_               = std::move(result);
    hasCached_            = true;
    cachedNeededFullWalk_ = needsFullWalk; // false in this branch (the gate above already excluded true)
    return cached_;
}

} // namespace ned::editor::treesitter
