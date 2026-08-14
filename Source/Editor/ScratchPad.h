//
// Named, persistent scratch notes (auto-saved-scratch-pads follow-up):
// disk-backed text files under one fixed XDG data directory, deliberately
// not associated with any project or ProjectRoot() -- a scratch is global
// to the user, not per-project (see ROADMAP.md for why per-project
// "contextual" scratches were explicitly deferred rather than designed in
// here). Not shown anywhere (tab list, sidebar) until a buffer for one is
// actually opened via find-scratch -- there is deliberately no startup-time
// scan that loads every existing scratch as a buffer.
//

#ifndef NED_EDITOR_SCRATCHPAD_H
#define NED_EDITOR_SCRATCHPAD_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

// $XDG_DATA_HOME/ned/scratches, falling back to $HOME/.local/share/ned/scratches
// if XDG_DATA_HOME is unset or empty. Throws std::runtime_error if neither is
// usable. Mirrors Janet/InitFile.h's resolution pattern, just against
// $XDG_DATA_HOME instead of $XDG_CONFIG_HOME -- scratch notes are
// user-authored content, not editor configuration. A pure path calculation,
// same as InitFilePath/ThemeFilePath -- does not create the directory itself;
// see AutoSaveScratchBuffers below for where that happens.
[[nodiscard]] std::filesystem::path ScratchDirectory();

// True if name is safe to use as a scratch's filename stem: non-empty, and
// contains no path separator. Scratches are deliberately a flat namespace (no
// per-project/nested scratches yet), so a name that could escape
// ScratchDirectory() via a relative path component is rejected outright
// rather than silently sanitized.
[[nodiscard]] bool IsValidScratchName(std::string_view name);

// ScratchDirectory() / (name + ".txt"). Throws std::invalid_argument if name
// fails IsValidScratchName.
[[nodiscard]] std::filesystem::path ScratchPathForName(std::string_view name);

// Sorted list of existing scratch names (file basenames under
// ScratchDirectory(), with the fixed .txt extension stripped). Returns an
// empty list if the directory doesn't exist yet (no scratches created, not an
// error) or otherwise can't be listed, the same "UI completion, not a hard
// file operation" convention text::CompleteFilePath already established.
[[nodiscard]] std::vector<std::string> ListScratchNames();

// Prefix-matched, sorted candidates for find-scratch's prompt. Mirrors
// text::CompleteBufferNames' shape.
[[nodiscard]] std::vector<std::string> CompleteScratchNames(std::string_view prefix);

// Process-wide auto-save toggle (mutex-guarded static state, mirroring
// TabWidth.h/ProjectRoot.h's exact pattern), default on. Configured from
// Janet via ned/set-scratch-auto-save.
void               SetScratchAutoSaveEnabled(bool enabled);
[[nodiscard]] bool ScratchAutoSaveEnabled();

// Saves every open, Modified() buffer in bufferList whose own path sits
// directly inside ScratchDirectory() (a flat namespace, so "directly inside"
// rather than "anywhere underneath" is deliberate and exact -- matches
// IsValidScratchName's own flat-namespace rule). A no-op if
// ScratchAutoSaveEnabled() is false. Creates ScratchDirectory() on disk if it
// doesn't exist yet (mirrors ThemeFile.cpp's SaveThemeFile, which does the
// same at its own write site rather than baking directory creation into a
// pure path getter). A per-buffer save failure (disk full, permissions, ...)
// is swallowed rather than propagated -- this runs unattended on a timer
// (see BufferView::StartAutoSaveTimer), there's no user waiting to see an
// exception, and the next tick just retries.
void AutoSaveScratchBuffers(text::BufferList& bufferList);

} // namespace ned::editor

#endif // NED_EDITOR_SCRATCHPAD_H
