//
// Global (buffer-independent) ring of killed text, Emacs-style: kill in one
// buffer, yank in another. Deliberately has no dependency on Buffer -- kill
// commands (kill-line, kill-region, ...) delete text via Buffer and push the
// result here; yank commands read Current()/YankPop() and insert it back via
// Buffer. Keeping KillRing decoupled from Buffer is what lets both be tested,
// and later be composed by Janet, independently.
//

#ifndef NED_TEXT_KILLRING_H
#define NED_TEXT_KILLRING_H

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace ned::text {

class KillRing {
  public:
    // 120 matches Emacs' kill-ring-max default.
    explicit KillRing(std::size_t capacity = 120);

    // Pushes a new entry as the yank target, evicting the oldest entry once
    // over capacity. Equivalent to KillPieces({text}).
    void Kill(std::string text);

    // multi-cursor-kill-ring follow-up: pushes one entry holding one piece
    // per active cursor, in cursor order -- what a multi-cursor kill-line/
    // kill-region/kill-ring-save pushes. pieces.size() == 1 behaves exactly
    // like Kill(pieces[0]).
    void KillPieces(std::vector<std::string> pieces);

    // Emacs-keymap-round-2 follow-up (kill-append): appends (or, if
    // `prepend`, prepends -- for a backward-direction kill like
    // backward-kill-word) text onto the most recent entry instead of
    // pushing a new one, matching real Emacs' kill-append: consecutive
    // kill commands (kill-line, kill-word, ...) with no other command in
    // between accumulate into one kill-ring entry rather than each
    // shadowing the last. Only meaningful against a single-piece entry --
    // falls back to Kill(text) (a fresh entry) when the ring is empty or
    // the most recent entry has more than one piece (a multi-cursor kill,
    // which has no single sensible append target), so a caller never needs
    // a special first-kill/first-multi-cursor-kill case. Resets the yank
    // pointer to this (now-extended) entry, same as Kill/KillPieces.
    void AppendToCurrent(std::string text, bool prepend);

    [[nodiscard]] bool Empty() const;

    // The current yank target (what C-y would insert): the current entry's
    // pieces joined by "\n" -- unchanged single-cursor meaning; for a
    // multi-cursor entry this is the whole-blob fallback a piece-count
    // mismatch on yank falls back to. Empty string if Empty().
    [[nodiscard]] const std::string& Current() const;

    // multi-cursor-kill-ring follow-up: the current entry's individual
    // pieces (size 1 for a plain Kill()) -- what a multi-cursor yank
    // compares its live cursor count against to decide per-cursor vs
    // whole-blob distribution.
    [[nodiscard]] const std::vector<std::string>& CurrentPieces() const;

    // Cycles the yank pointer to the next-older entry and returns its
    // joined form (what repeated M-y cycles through) -- CurrentPieces()
    // read right after reflects the same, now-current entry. No-op
    // returning Current() if Empty().
    [[nodiscard]] const std::string& YankPop();

  private:
    struct Entry {
        std::vector<std::string> pieces; // always >= 1
        std::string              joined; // pieces joined by "\n", cached at push time
    };
    std::deque<Entry> ring_; // front = most recent
    std::size_t       capacity_;
    std::size_t       yankIndex_ = 0;
};

} // namespace ned::text

#endif // NED_TEXT_KILLRING_H
