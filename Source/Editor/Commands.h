//
// Built-in commands and a minimal real default keymap, built on top of the
// Phase 1 Buffer/KillRing APIs. This is the seam Phase 3 mirrors into Janet
// (exposing the same commands, letting Janet register more of its own) and
// Phase 5 extends with major/minor-mode-specific commands and keymaps.
//

#ifndef NED_EDITOR_COMMANDS_H
#define NED_EDITOR_COMMANDS_H

#include "Command.h"
#include "Keymap.h"

namespace ned::editor {

// Registers: forward-char, backward-char, next-line, previous-line,
// forward-word, backward-word, scroll-page-down, scroll-page-up, delete-char,
// backward-delete-char, beginning-of-line, end-of-line, kill-line, yank,
// undo, redo, newline, self-insert-command, save-buffer, quit,
// isearch-forward, isearch-backward, query-replace-regexp, find-file,
// switch-to-buffer, project-search, project-search-visit-result,
// project-replace, toggle-project-sidebar, create-directory, delete-file,
// rename-file, find-scratch. All but the first block only set
// CommandContext::interactiveRequest -- running the actual interactive
// session is a UI concern (see BufferView). scroll-page-down/-up read
// CommandContext::viewportHeight, set by the host UI. project-replace
// (project-replace follow-up) confirms once for the whole batch of matched
// files, not per-match -- see ProjectReplace.h for why. toggle-project-sidebar
// (project-sidebar follow-up) flips ProjectSidebar's own ox::Widget::active
// flag directly, not a prompt/session -- see BufferView::StartInteractiveSession.
// create-directory/delete-file/rename-file (project-file-ops follow-up) all
// prompt for one or two paths via the minibuffer, same as find-file -- see
// BufferView::HandlePromptKey/HandleDeleteFileKey/HandleRenameFileKey.
// rename-file is bound to C-c C-n, not the more obvious C-c C-m -- see the
// comment at its keymap.Bind call in Commands.cpp for why C-m is unusable.
// File *creation* has no separate command: find-file (C-x C-f) on a
// not-yet-existing path already creates one -- see ProjectFileOps.h.
// find-scratch (auto-saved-scratch-pads follow-up) prompts for a name and
// opens/creates a scratch note under Editor/ScratchPad.h's fixed data
// directory -- global, never tied to a project, folds into the same
// HandlePromptKey prompt machinery as find-file.
void RegisterBuiltinCommands(CommandRegistry& registry);

// C-f/C-b/C-n/C-p/C-d/DEL/C-a/C-e/C-k/C-y/C-//RET/C-x C-s/C-x C-c/C-x C-f/
// C-x b/C-s/C-r/ESC %/ESC f/ESC b/C-c C-s/C-c C-v/C-c C-r/C-c C-p/C-c C-d/
// C-c C-k/C-c C-n/C-c C-o, arrow-key movement (left/right/up/down), Home/End,
// Page Up/Page Down, and every printable ASCII character bound to
// self-insert-command.
[[nodiscard]] Keymap BuildDefaultGlobalKeymap();

} // namespace ned::editor

#endif // NED_EDITOR_COMMANDS_H
