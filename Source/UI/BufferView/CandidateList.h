//
// A filtered, ranked list of choices with a selection in it -- see
// Docs/BufferViewDecomposition.md.
//
// Every "type to narrow, arrow keys to pick" prompt in the editor (M-x,
// project-find-file, switch-to-buffer, switch-project, recent files, bookmarks,
// themes, branch names, agent names) does the same three things on every
// keystroke: re-rank the candidates against what has been typed, keep the
// selection pointing at something that still exists, and move it with wrap-around
// on the arrow keys. Each prompt used to spell that out itself, as a candidate
// vector plus a loose selection index, with the modulo arithmetic and the
// shrink-clamp written out per prompt.
//
// The clamp is the part worth centralising. Narrowing a list can leave the
// selection past its end, and every prompt that got that subtly wrong would get
// it wrong in the same way -- a selection that survives as a stale index and then
// picks the wrong entry, or reads one past the end.
//
// Ranking is FuzzyMatch.h's subsequence scorer, the same one the prompts already
// used; this only decides what happens to the selection around it.
//

#ifndef NED_UI_BUFFERVIEW_CANDIDATELIST_H
#define NED_UI_BUFFERVIEW_CANDIDATELIST_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::ui::bufferview {

class CandidateList {
  public:
    // Replace the pool being filtered and re-rank against `query`. The selection
    // goes back to the top: a new pool means the previous selection referred to
    // something from a different list.
    //
    // `pinnedCount` marks the first N entries as *actions* rather than results
    // (the theme picker's "Current theme" and "None (detect)"). They are still
    // filtered by the query -- typing "gruv" drops both, which is right -- but
    // they never compete with the results for position: whichever of them still
    // match sit at the top, in the order given, and the rest rank below.
    //
    // Without this a pinned row keeps its place only by accident of collation.
    // FuzzyFilterAndRank tie-breaks equal scores alphabetically, and with an
    // empty query every candidate scores equally, so "Current theme" was first
    // purely because every registry name beside it was lowercase and 'C' sorts
    // before 'c'. Proper-casing the theme names sent it into the middle of the
    // list.
    void Reset(std::vector<std::string> candidates, std::string_view query = {}, std::size_t pinnedCount = 0);

    // Re-rank the existing pool. The selection stays where it is where it still
    // can, clamped to the last entry when the list shrank under it -- typing
    // narrows a list far more often than it invalidates the choice already made.
    void Refilter(std::string_view query);

    // Re-rank a pool that is recomputed per keystroke rather than stored (the
    // command registry, the open buffer list). Same selection handling as
    // Refilter, not Reset -- the pool is nominally the same one.
    void Refilter(std::vector<std::string> candidates, std::string_view query);

    // Refilter and hand the result straight back, for the common case of
    // wanting both in one expression.
    const std::vector<std::string>& Refiltered(std::string_view query);
    const std::vector<std::string>& Refiltered(std::vector<std::string> candidates, std::string_view query);

    [[nodiscard]] const std::vector<std::string>& Ranked() const {
        return ranked_;
    }
    [[nodiscard]] bool Empty() const {
        return ranked_.empty();
    }
    [[nodiscard]] std::size_t Size() const {
        return ranked_.size();
    }

    // Always a valid index while the list is non-empty; 0 when it is empty, so
    // reading it without checking cannot be out of range on its own.
    [[nodiscard]] std::size_t Selection() const {
        return selection_;
    }

    // The selected entry. Empty string when there is nothing to select, so a
    // caller that forgets to check gets a harmless value rather than UB.
    [[nodiscard]] const std::string& Selected() const;

    // Back to the first entry -- what typing does, since the best match for what
    // was just typed is what the ranking put at the top.
    void SelectTop() {
        selection_ = 0;
    }

    // Wrap around at both ends, which is what every one of these prompts did.
    void SelectNext();
    void SelectPrevious();

    // Point at a specific row, for a click. Out-of-range is ignored rather than
    // clamped: a click that lands nowhere should do nothing.
    void SelectIndex(std::size_t index);

    void Clear();

  private:
    void RankAgainst(std::string_view query, bool keepSelection);

    std::vector<std::string> source_;
    std::vector<std::string> ranked_;
    std::size_t              selection_   = 0;
    std::size_t              pinnedCount_ = 0; // see Reset
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_CANDIDATELIST_H
