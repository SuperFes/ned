//
// A yes/no confirmation in the echo area -- see Docs/BufferViewDecomposition.md.
//
// Six of these exist (quit with unsaved buffers, close a modified buffer,
// overwrite a file changed on disk, save over unresolved conflict markers, open
// a file that looks binary, discard a hunk) and they differ in exactly two
// things: what confirming does, and what to say when it is declined. The key
// handling around that -- y/Y confirms, n/N or the quit chord declines, anything
// else is ignored so a stray keystroke cannot answer for the user -- was written
// out six times.
//
// Same ordering rule as FuzzyPrompt: `onConfirm` runs *after* the session has
// ended, so it is free to close the buffer, open another, or start a fresh
// prompt. Anything it needs from the session -- the buffer awaiting closure, the
// path awaiting an open -- must therefore be captured when the prompt is built,
// because ending the session clears it.
//
// Deliberately not stretched to cover the project-trust prompt, which is a
// three-way choice (once / always / decline) rather than a confirmation.
//

#ifndef NED_UI_BUFFERVIEW_CONFIRMPROMPT_H
#define NED_UI_BUFFERVIEW_CONFIRMPROMPT_H

#include <functional>
#include <string>

namespace ned::ui::bufferview {

struct ConfirmPrompt {
    // Shown when the user declines. Confirming reports its own outcome instead,
    // since what it did is more informative than the fact it happened.
    std::string cancelMessage;

    // Runs after the session has ended.
    std::function<void()> onConfirm;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_CONFIRMPROMPT_H
