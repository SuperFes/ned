//
// rename-review follow-up: whether rename-symbol/lsp-rename hand their edits
// to an editable review multibuffer (Editor/RenameReview.h) instead of
// applying them straight away. Same mutex-guarded-static-state shape as
// MultibufferSearchSettings.h/TabWidth.h and the dozens of settings modules
// beside them.
//
// On by default: both rename tiers already land as one undo transaction, so
// the cost of reviewing first is a keystroke, and the review is the only
// place the comment/string occurrences a rename skipped are visible at all.
// Off restores the immediate apply exactly -- the same code path, just
// reached without the buffer in between.
//
// A rename whose edit carries filesystem resource operations (create/delete/
// rename a file) never goes through a review regardless of this setting: a
// multibuffer has no way to represent one, and half-reviewing an edit is
// worse than not reviewing it. BufferView says so when it happens.
//

#ifndef NED_EDITOR_RENAMEREVIEWSETTINGS_H
#define NED_EDITOR_RENAMEREVIEWSETTINGS_H

namespace ned::editor {

void               SetRenameThroughReview(bool enabled);
[[nodiscard]] bool RenameThroughReview(); // default true

} // namespace ned::editor

#endif // NED_EDITOR_RENAMEREVIEWSETTINGS_H
