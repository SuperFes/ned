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

namespace ned::text {

class KillRing {
  public:
    // 120 matches Emacs' kill-ring-max default.
    explicit KillRing(std::size_t capacity = 120);

    // Pushes a new entry as the yank target, evicting the oldest entry once
    // over capacity.
    void Kill(std::string text);

    [[nodiscard]] bool Empty() const;

    // The current yank target (what C-y would insert). Empty string if Empty().
    [[nodiscard]] const std::string& Current() const;

    // Cycles the yank pointer to the next-older entry and returns it (what
    // repeated M-y cycles through). No-op returning Current() if Empty().
    [[nodiscard]] const std::string& YankPop();

  private:
    std::deque<std::string> ring_; // front = most recent
    std::size_t              capacity_;
    std::size_t              yankIndex_ = 0;
};

} // namespace ned::text

#endif // NED_TEXT_KILLRING_H
