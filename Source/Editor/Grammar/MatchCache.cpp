#include "MatchCache.h"

#include <algorithm>
#include <iterator>

#include "Editor/Parse/Node.h"

namespace ned::editor::grammar {

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

} // namespace

const std::vector<QueryMatch>& MatchCache::Reconcile(const QueryMatcher& matcher, const Tree& tree, std::string_view text,
                                                     const IncrementalParseCache& parse) {
    const std::uint64_t generation = parse.Generation();
    if (hasCached_ && generation_ == generation)
        return cached_;
    const std::optional<TreeEdit> edit = generation_ ? parse.EditSince(*generation_) : std::nullopt;
    (void)Reconcile(matcher, tree, text, edit);
    generation_ = generation;
    return cached_;
}

const std::vector<QueryMatch>& MatchCache::Reconcile(const QueryMatcher& matcher, const Tree& tree, std::string_view text,
                                                     const std::optional<TreeEdit>& edit) {
    generation_     = std::nullopt;
    const Node root = tree.RootNode();

    // See MatchCache.h's header comment for why a parse error forces a full
    // walk instead of an incremental reconciliation: error recovery is a
    // whole-parse property (a real bash case flipped an unrelated keyword's
    // classification 50 bytes from the edit with no overlap at all). Sticky
    // across the CURRENT call too, not just cached_'s own generation: an edit
    // that resolves the error still needs one more full walk to reestablish a
    // trustworthy baseline before incremental reuse resumes.
    const bool needsFullWalk = parse::NodeHasError(root.Raw());
    if (!hasCached_ || !edit.has_value() || needsFullWalk || cachedNeededFullWalk_) {
        cached_ = matcher.Matches(root, text);
        std::stable_sort(cached_.begin(), cached_.end(), ByRootStartByte);
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
    //
    // The window also has to cover every range the reparse changed the
    // tree's STRUCTURE in (TreeEdit::structuralChanges), which is not always
    // near the edit: a token inserted before a Go identifier reclassifies it
    // from @variable to @type with its own bytes untouched, and splitting a
    // heredoc terminator grows the body to the end of the file. Held
    // strictly like the edit span, because a changed range starts where a
    // node that grew into it used to END -- the bytes it already covered kept
    // the same nesting.
    const text::ChangedSpan& span       = edit->span;
    std::size_t              widenStart = span.newStart;
    std::size_t              widenEnd   = span.newEnd;
    for (const auto& [start, end] : edit->structuralChanges) {
        widenStart = std::min(widenStart, start);
        widenEnd   = std::max(widenEnd, end);
    }

    std::vector<QueryMatch> before;
    std::vector<QueryMatch> after;
    std::size_t             redoStart = 0;
    // Unbounded until a kept-after match bounds it, as Matches() is: a
    // zero-width node at the very end of the text (an EOF closer) lies in no
    // window that ends there.
    std::size_t redoEnd = static_cast<std::size_t>(-1);
    for (QueryMatch& match : cached_) {
        if (match.rootEndByte < span.oldStart) {
            if (match.rootEndByte < widenStart) {
                redoStart = std::max(redoStart, match.rootEndByte);
                before.push_back(std::move(match)); // entirely before, with a real gap -- unaffected
                continue;
            }
            // else: inside a structurally changed range -- drop below.
        }
        else if (match.rootStartByte > span.oldEnd) {
            ShiftAfterEdit(match, span);
            if (match.rootStartByte > widenEnd) {
                redoEnd = std::min(redoEnd, match.rootStartByte); // post-shift
                after.push_back(std::move(match));                // entirely after, with a real gap -- shift, don't re-derive
                continue;
            }
            // else: inside a structurally changed range -- drop below.
        }
        // else: overlaps/touches the edit or a structurally changed range --
        // dropped; MatchesInRange below re-derives it (and anything else
        // whose root now intersects the window).
    }

    std::vector<QueryMatch> redone = matcher.MatchesInRange(root, text, redoStart, redoEnd);

    // Zero-width roots on the window's two edges are re-derived from the new
    // tree outright. Which of them the window itself finds depends on their
    // parents, not on whether they changed -- QueryMatcher's InRange admits
    // one at a window's start but not at its end, and prunes one trailing a
    // parent that ends at the start -- and a zero-width node covers no byte a
    // changed range could report. A two-byte window straddling the edge
    // admits one there whatever its parent.
    for (const std::size_t edge : {redoStart, redoEnd}) {
        if (edge > text.size() || (edge == redoEnd && redoEnd == redoStart))
            continue;
        const auto onEdge = [edge](const QueryMatch& match) {
            return match.rootStartByte == edge && match.rootEndByte == edge;
        };
        std::erase_if(before, onEdge);
        std::erase_if(after, onEdge);
        std::erase_if(redone, onEdge);
        for (QueryMatch& match : matcher.MatchesInRange(root, text, edge > 0 ? edge - 1 : 0, edge + 1)) {
            if (onEdge(match))
                redone.push_back(std::move(match));
        }
    }

    // Three sorted runs: kept-before; re-derived, which can start before a
    // kept-before match (an enclosing node the edit touched) but never past
    // redoEnd; kept-after, which starts at or past it.
    std::stable_sort(redone.begin(), redone.end(), ByRootStartByte);
    std::vector<QueryMatch> result;
    result.reserve(before.size() + redone.size() + after.size());
    std::merge(std::make_move_iterator(before.begin()), std::make_move_iterator(before.end()),
               std::make_move_iterator(redone.begin()), std::make_move_iterator(redone.end()), std::back_inserter(result),
               ByRootStartByte);
    result.insert(result.end(), std::make_move_iterator(after.begin()), std::make_move_iterator(after.end()));
    cached_               = std::move(result);
    hasCached_            = true;
    cachedNeededFullWalk_ = needsFullWalk; // false in this branch (the gate above already excluded true)
    return cached_;
}

} // namespace ned::editor::grammar
