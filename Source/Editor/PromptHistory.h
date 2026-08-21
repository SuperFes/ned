//
// Global, per-prompt-kind input history (Emacs' minibuffer-history-variable
// idea) for BufferView's prompt-driven InteractiveRequest sessions
// (find-file, goto-line, execute-command, VCS commit message, ...) -- M-p/M-n
// recall a previously entered value instead of every prompt starting blank.
// Deliberately has no notion of "browsing"/cursor position itself, mirroring
// KillRing's own split: this is just the ring storage, keyed by a short
// static string identifying the prompt kind (BufferView::InputMode is a
// private UI-layer enum this file must not depend on); BufferView owns the
// per-session browsing cursor and calls Record()/Entries() around it.
//
// In-memory only, cleared on restart -- no XDG persistence, matching Emacs'
// own default (savehist-mode is opt-in there too).
//

#ifndef NED_EDITOR_PROMPTHISTORY_H
#define NED_EDITOR_PROMPTHISTORY_H

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ned::editor {

class PromptHistory {
  public:
    // 32 per kind -- Emacs' history-length default (100) is a global cap
    // shared across every history variable; scoping per-kind here means a
    // much smaller number already covers realistic recall depth.
    explicit PromptHistory(std::size_t capacityPerKind = 32);

    // No-op if entry is empty, or equal to the most recent entry already
    // recorded for key (consecutive dedup -- repeatedly submitting the same
    // value shouldn't burn ring slots). Otherwise pushed as the newest entry,
    // evicting the oldest once over capacity.
    void Record(std::string_view key, std::string entry);

    // Newest-first. Empty (a static, never a per-key default-constructed
    // entry) for a key nothing has been recorded under yet.
    [[nodiscard]] const std::vector<std::string>& Entries(std::string_view key) const;

  private:
    std::unordered_map<std::string, std::vector<std::string>> rings_; // each vector front = most recent
    std::size_t                                                capacityPerKind_;
};

} // namespace ned::editor

#endif // NED_EDITOR_PROMPTHISTORY_H
