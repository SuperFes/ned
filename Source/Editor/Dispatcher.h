//
// Accumulates fed key chords against a KeymapStack and invokes the matched
// command via a CommandRegistry. This is the piece Phase 4 feeds real
// terminal key events into; it has no TermOx/UI dependency of its own, which
// is what makes it testable without a running terminal.
//

#ifndef NED_EDITOR_DISPATCHER_H
#define NED_EDITOR_DISPATCHER_H

#include <vector>

#include "Command.h"
#include "Key.h"
#include "Keymap.h"

namespace ned::editor {

class Dispatcher {
  public:
    Dispatcher(const CommandRegistry& registry, KeymapStack keymaps);

    enum class Outcome {
        Invoked, // a command matched and was run
        Pending, // sequence so far is a valid prefix; waiting for more keys
        Unbound, // sequence doesn't match anything; the pending sequence was discarded
    };

    // May invoke a command against context as a side effect. Sets
    // context.triggeringKey to chord before doing so (so e.g.
    // self-insert-command can read what was actually pressed).
    Outcome Feed(const KeyChord& chord, CommandContext& context);

    // Discards any in-progress prefix sequence (e.g. on C-g / Escape).
    void Reset();

    [[nodiscard]] const std::vector<KeyChord>& Pending() const;

  private:
    const CommandRegistry& registry_;
    KeymapStack             keymaps_;
    std::vector<KeyChord>   pending_;
};

} // namespace ned::editor

#endif // NED_EDITOR_DISPATCHER_H
