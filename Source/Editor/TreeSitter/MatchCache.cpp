#include "MatchCache.h"

#include <algorithm>

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
    if (!hasCached_ || !edit.has_value() || AlwaysFullWalk(matcher)) {
        cached_ = matcher.Matches(root, text);
        std::sort(cached_.begin(), cached_.end(), ByRootStartByte);
        hasCached_ = true;
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
    const text::ChangedSpan& span = *edit;
    std::vector<QueryMatch>  result;
    result.reserve(cached_.size());
    for (QueryMatch& match : cached_) {
        if (match.rootEndByte < span.oldStart) {
            result.push_back(std::move(match)); // entirely before, with a real gap -- unaffected
        }
        else if (match.rootStartByte > span.oldEnd) {
            ShiftAfterEdit(match, span);
            result.push_back(std::move(match)); // entirely after, with a real gap -- shift, don't re-derive
        }
        // else: overlaps OR touches the edit -- dropped; MatchesInRange below
        // re-derives it (and anything else whose root now intersects the
        // window).
    }

    // Padded by 1 byte on each side (clamped to the text): the drop test
    // above is strict (excludes anything merely TOUCHING the edit), so the
    // complementary redo window must be strictly wider than [newStart,
    // newEnd) to still catch a touching neighbor -- MatchesInRange's own
    // InRange test is start < endByte / end > startByte, which a node
    // ending exactly at newStart (or starting exactly at newEnd) fails
    // against the unpadded window. This also covers a pure deletion
    // (newStart == newEnd), where an unpadded window is zero-width and
    // would match nothing at all.
    const std::size_t       redoStart = span.newStart > 0 ? span.newStart - 1 : 0;
    const std::size_t       redoEnd   = std::min(text.size(), span.newEnd + 1);
    std::vector<QueryMatch> redone    = matcher.MatchesInRange(root, text, redoStart, redoEnd);
    for (QueryMatch& match : redone) {
        result.push_back(std::move(match));
    }

    std::sort(result.begin(), result.end(), ByRootStartByte);
    cached_    = std::move(result);
    hasCached_ = true;
    return cached_;
}

} // namespace ned::editor::treesitter
