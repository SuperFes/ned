//
// Which InputModes are plain text-entry prompts, and what Tab offers in each --
// see Docs/BufferViewDecomposition.md.
//
// Twenty-seven prompts share one key handler: type a line, Enter commits, Escape
// cancels. Which modes those are was written out twice, as two lists that had to
// agree -- an opt-in list in the key dispatch deciding what reaches the handler
// at all, and an opt-out list inside it deciding what Tab does. They drifted
// twice, both times the same way: a new prompt was added to the second list but
// not the first, so typing into it silently fell through to ordinary
// self-insert-command instead of the prompt. That is recorded in the git history
// as two separate "dispatch gap" fixes.
//
// One table replaces both. A mode that is not listed is not a text-entry prompt;
// a mode that is says what Tab means there, so adding a prompt is one entry
// rather than two edits in agreement.
//

#ifndef NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H
#define NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H

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

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_TEXTENTRYPROMPT_H
