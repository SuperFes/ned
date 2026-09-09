//
// Which InputModes are plain text-entry prompts, and what Tab offers in each --
// see Docs/BufferViewDecomposition.md.
//
// Twenty-seven prompts share one key handler: type a line, Enter commits, Escape
// cancels. What Tab offers in each, and what each is called when the user
// abandons it, used to be two more lists that had to agree with the key
// dispatch -- and they drifted twice, both times a new prompt reaching one list
// but not another, so typing into it silently fell through to
// self-insert-command. The git history carries both as "dispatch gap" fixes.
//
// This table is now the single answer to both of those questions. Dispatch
// itself -- which modes reach the handler at all -- is OnKeyEvent's switch,
// which has no default label, so the compiler reports a mode nobody dispatches.
// Between the two, the drift that caused those bugs is no longer expressible:
// a mode missing from the switch will not build, and a mode missing here gets a
// generic cancel message and an inert Tab rather than silently doing nothing.
//

#ifndef NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H
#define NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H

#include <string>

namespace ned::ui::bufferview {

enum class PromptCompletion {
    // Free text with no candidates worth offering: a search regex, a date, a
    // debuggee expression, a message to an agent, a line number, a branch name
    // being deliberately invented.
    None,
    // Buffer and file names, offered as a common prefix plus a list in the echo
    // area.
    Names,
    // Filesystem paths, offered as an anchored dropdown that Tab accepts from.
    PathDropdown,
};

// Whether committing finished the prompt, or handed off to a second one that now
// owns the session -- find-file discovering a binary file and asking whether to
// open it anyway, or open-project asking for a name once it has a path. A
// hand-off must not end the session or record history: the prompt that took over
// is still running.
enum class PromptCommit {
    Finished,
    Transitioned,
};

// What one text-entry prompt needs beyond its commit action: what Tab offers,
// and the name it goes by when the user abandons it ("<label> cancelled.").
struct TextEntryPrompt {
    PromptCompletion completion = PromptCompletion::None;
    std::string      cancelLabel;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H
