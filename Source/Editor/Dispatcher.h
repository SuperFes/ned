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

    // execute-extended-command follow-up: read-only access to the same
    // registry Feed already invokes commands through, so BufferView's M-x
    // session can invoke a fuzzy-matched command by name directly (skipping
    // the KeymapStack::Resolve step Feed itself does) without needing a
    // second CommandRegistry& threaded into BufferView's own constructor.
    [[nodiscard]] const CommandRegistry& Registry() const;

    // Keyboard macros (kmacro-start-macro/kmacro-end-or-call-macro
    // follow-up). StartRecording begins/restarts capturing every
    // subsequently *resolved* chord sequence Feed invokes a command through
    // (an unbound/still-pending sequence is never recorded -- only a
    // sequence that actually completes a bound command becomes part of the
    // macro). StopRecording finalizes the in-progress capture into
    // LastMacro() and turns recording off; a safe no-op if not currently
    // recording. Starting a new recording does not clear the previous
    // LastMacro() until the new one actually finishes via StopRecording --
    // matches Emacs' own "last completed macro stays callable while a new
    // one is mid-recording" behavior.
    void                                       StartRecording();
    void                                       StopRecording();
    [[nodiscard]] bool                         IsRecording() const;
    [[nodiscard]] const std::vector<KeyChord>& LastMacro() const;

    // The command that stops recording (kmacro-end-or-call-macro) is itself
    // invoked through the exact same Match path every other command is --
    // its own triggering chord(s) get appended to the in-progress macro by
    // Feed, same as any other resolved sequence, since Dispatcher can't tell
    // in advance which command a caller is about to treat as "the one that
    // ends recording." Real recording start/stop doesn't happen inside a
    // command's own CommandFunction though (CommandContext carries no
    // Dispatcher& -- by design, see Command.h) -- it happens one level up,
    // in BufferView::StartInteractiveSession, *after* Feed has already
    // returned for that keypress. So the caller (BufferView), which is the
    // only place that actually knows "this keypress is the one ending
    // recording," must call this *before* StopRecording() to strip that
    // keypress's own chord(s) back out -- removes exactly however many
    // chords the most recent recording Match-append added, a safe no-op if
    // nothing's been recorded yet.
    void DiscardMostRecentlyRecordedChords();

    // The name of the most recent command Feed invoked -- Emacs'
    // `last-command`, tracked so a command can behave differently when it
    // directly follows a specific other one (yank-pop after yank is the
    // canonical case). Feed copies the *previous* value into
    // context.lastCommand before invoking, so the running command sees what
    // ran before it, not itself. Commands invoked outside Feed (M-x's own
    // Registry().Invoke path) deliberately don't update this -- a documented
    // v1 simplification, same class of cut as KeymapStack's non-Emacs
    // layering policy.
    [[nodiscard]] const std::string& LastInvokedCommand() const;

  private:
    const CommandRegistry& registry_;
    KeymapStack            keymaps_;
    std::vector<KeyChord>  pending_;
    std::string            lastInvokedCommand_;

    bool                  recording_ = false;
    std::vector<KeyChord> currentMacro_;
    std::vector<KeyChord> lastMacro_;
    std::size_t           lastRecordedChordCount_ = 0; // see DiscardMostRecentlyRecordedChords
};

} // namespace ned::editor

#endif // NED_EDITOR_DISPATCHER_H
