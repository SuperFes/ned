//
// One description of a "type to narrow, arrows to pick, Enter to commit" prompt
// -- see Docs/BufferViewDecomposition.md.
//
// Eight of these exist (M-x, project-find-file, recent files, switch-project,
// switch-to-buffer, ACP agent, bookmarks, themes, VCS branches) and they behaved
// identically apart from five things: where the choices come from, what to say
// when nothing matches, what to say on cancel, whether the typed text is worth
// remembering, and what committing actually does. Everything around that -- the
// history browsing, the wrap-around navigation, re-ranking as characters arrive,
// re-snapping to the top match on an edit, pushing the popup model, ending the
// session -- was written out eight times.
//
// So it is described rather than reimplemented: this struct is the five things
// that differ, and BufferView::HandleFuzzyPromptKey is the behaviour they share.
//
// Two ordering rules the shared driver guarantees, because getting either wrong
// is a real bug rather than a style question:
//
//   - `commit` runs *after* the session has already ended, so it is free to open
//     a buffer, switch panes, or start another prompt without fighting the one
//     that is still notionally up. The selected string is captured before the
//     session ends, since ending it clears the list.
//   - `onCancel` runs *before* the status message is replaced, so a prompt that
//     previews live (the theme picker) restores what it was showing first.
//

#ifndef NED_UI_BUFFERVIEW_FUZZYPROMPT_H
#define NED_UI_BUFFERVIEW_FUZZYPROMPT_H

#include <functional>
#include <string>
#include <vector>

#include "UI/BufferView/CandidateList.h"

namespace ned::ui::bufferview {

struct FuzzyPrompt {
    // The ranked choices and the selection in them. Never null.
    CandidateList* list = nullptr;

    // Recorded on Enter and browsed with the history keys. Empty means this
    // prompt keeps no history -- the theme picker, whose entries are a fixed
    // list nobody gains from retyping.
    std::string historyKey;

    // Shown when the prompt is abandoned.
    std::string cancelMessage;

    // Shown when Enter lands on an empty result. Takes what was typed, since
    // every one of these quotes it back.
    std::function<std::string(const std::string& query)> emptyMessage;

    // Recomputed per keystroke where the pool is live -- the command registry,
    // the open buffer list, the configured agents. Null where the pool was
    // captured once at session start and the list already owns it.
    std::function<std::vector<std::string>()> pool;

    // What picking this entry does. Runs after the session has ended.
    std::function<void(const std::string& selected)> commit;

    // Live preview as the selection moves, and its undo on cancel. Both null
    // for a prompt that only commits.
    std::function<void()> onSelectionChanged;
    std::function<void()> onCancel;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_FUZZYPROMPT_H
