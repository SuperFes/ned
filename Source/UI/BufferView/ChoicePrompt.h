//
// A numbered list you pick one entry from -- see
// Docs/BufferViewDecomposition.md.
//
// Five of these exist (LSP code actions, go-to-definition when several match,
// the peek-definition popup, DAP thread selection, and an agent's permission
// request). All five behaved the same: 1-9 pick an entry directly, Up/Down move the
// highlight with wrap-around, Enter takes the highlighted one, the quit chord
// cancels, and anything else is ignored so a stray keystroke cannot answer.
//
// They were written out five times, and had drifted: pressing a digit with no
// entry behind it left two of them highlighting what they already were and then
// *committing it anyway*, while the other three stayed put. Committing an entry
// the user did not choose because they typed 7 in a list of three is a bug, not
// a variant, so the shared driver keeps the safe behaviour.
//
// Same ordering rule as ConfirmPrompt and FuzzyPrompt: `commit` runs after the
// session has ended, so it may open a buffer, apply an edit, or start another
// prompt. It is handed the chosen index, and whatever it needs from the list
// must be captured when the prompt is built -- ending the session clears it.
//
// Not stretched to cover two lists that only look similar: the DAP exception
// filters, where Enter *toggles* an entry and keeps the list up, and the mouse
// context menu, whose dividers make both its navigation and its digit lookup
// skip entries.
//

#ifndef NED_UI_BUFFERVIEW_CHOICEPROMPT_H
#define NED_UI_BUFFERVIEW_CHOICEPROMPT_H

#include <cstddef>
#include <functional>
#include <string>

namespace ned::ui::bufferview {

struct ChoicePrompt {
    // How many entries are on offer, and the caller's own highlight index into
    // them -- held by pointer because the caller keeps it across keystrokes.
    std::size_t  count     = 0;
    std::size_t* selection = nullptr;

    std::string cancelMessage;

    // Redraw the list after the highlight moves.
    std::function<void()> refresh;

    // Runs after the session has ended, with the index that was chosen.
    std::function<void(std::size_t index)> commit;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_CHOICEPROMPT_H
