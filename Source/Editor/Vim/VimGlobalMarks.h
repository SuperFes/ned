//
// vim-global-marks follow-up: real vim's A-Z marks are cross-file, unlike a-z (buffer-
// local) and '</'> (visual-selection) marks, which VimEngine.h's own marks_ map already
// covers correctly. A small, process-wide, mutex-guarded store -- this codebase's own
// dominant "process-wide setting" shape (see CLAUDE.md's own convention note) -- mapping
// a mark letter to a file path plus (line, column), not a byte offset: Bookmark.h's own
// reasoning applies identically here -- a file edited outside ned between a mark being
// set and jumped to would make a stored byte offset silently wrong, while line/column
// clamps sanely via Buffer::ByteOffsetForLineAndColumn.
//
// Setting/reading this store is plain process-wide state, no UI dependency, so
// VimEngine.cpp (Editor/, UI-free) can call it directly -- see SetMarkAt/GotoMark.
// Actually opening a different file and switching the active buffer, though, is
// BufferView's own job (the same "engine signals intent, host UI acts" split
// PendingIntent/pendingIntent_ already establishes) -- see
// VimEngine::TakePendingBufferJump().
//

#ifndef NED_EDITOR_VIM_VIMGLOBALMARKS_H
#define NED_EDITOR_VIM_VIMGLOBALMARKS_H

#include <cstddef>
#include <filesystem>
#include <optional>

namespace ned::editor::vim {

struct GlobalMark {
    std::filesystem::path path; // already normalized (weakly_canonical) by the setter
    std::size_t           line   = 0; // 0-based
    std::size_t           column = 0; // 0-based visual column

    bool operator==(const GlobalMark&) const = default;
};

// name must be 'A'-'Z' -- callers (VimEngine::SetMarkAt/GotoMark) have already checked
// this, matching every other process-wide setting module's own "no redundant
// re-validation" convention.
void SetGlobalMark(char32_t name, GlobalMark mark);
[[nodiscard]] std::optional<GlobalMark> GetGlobalMark(char32_t name);

// Test-only: clears every stored mark so one test's SetGlobalMark calls can't leak into
// another's -- this store is process-wide state with no other reset hook (Environment.h's
// own "construct at most one, ever" precedent means there's no natural per-test
// teardown otherwise).
void ClearGlobalMarksForTesting();

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMGLOBALMARKS_H
