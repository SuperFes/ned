//
// line-wrap follow-up. A user-configurable override table pointing a
// filename or file extension at a wrap-lines bool, taking precedence over
// whichever Mode::wrapLines default would otherwise apply -- e.g. a user
// who wants org-mode's own wrapLines=true default overridden off for one
// extension, or a plain-text extension opted into wrapping without needing
// a whole new Mode.
//
// Deliberately a small, standalone table, not folded into ModeOverrides.h's
// existing SetModeForExtension/SetModeForFilename mechanism -- that
// resolves an entire named Mode via ModeByName, with no concept of
// overriding a single field of whichever Mode would otherwise apply; a
// second, differently-shaped table is the closer fit than inventing
// per-field Mode overrides just for this one bool.
//
// Mutex-guarded static state, mirroring ModeOverrides.h's exact pattern
// (itself mirroring TabWidth.h/ProjectRoot.h) -- a few maps instead of one
// scalar.
//

#ifndef NED_EDITOR_WRAPOVERRIDES_H
#define NED_EDITOR_WRAPOVERRIDES_H

#include <filesystem>
#include <optional>
#include <string>

#include "Mode.h"

namespace ned::editor {

// Points extension at wrap -- resolved whenever WrapLinesForFileOverride is
// called below. extension may be given with or without a leading '.' (both
// "md" and ".md" resolve the same way), matching SetModeForExtension's own
// convention.
void SetWrapForExtension(const std::string& extension, bool wrap);

// Points filename (the full, exact base name of a file, e.g.
// "CMakeLists.txt" -- no wildcards/globbing) at wrap, matching
// SetModeForFilename's own convention. Checked before SetWrapForExtension's
// table by WrapLinesForFileOverride, same filename-before-extension
// precedence ModeForFileOverride already established.
void SetWrapForFilename(const std::string& filename, bool wrap);

// std::nullopt if neither table has an entry for path -- not an error;
// the caller falls back to whichever Mode::wrapLines default would
// otherwise apply, the same "caller falls back gracefully" convention
// ModeForFileOverride already established.
[[nodiscard]] std::optional<bool> WrapLinesForFileOverride(const std::filesystem::path& path);

// path's own override if one is configured, else mode.wrapLines. path is
// std::nullopt for a buffer with no associated path (e.g. a scratch
// buffer), which can never have a file-based override -- mode.wrapLines
// alone decides.
[[nodiscard]] bool EffectiveWrapLines(const std::optional<std::filesystem::path>& path, const Mode& mode);

// acp-panel-wrapping follow-up: a second, buffer-Name()-keyed override
// table, for a path-less synthetic buffer (e.g. "*acp: <agent>*") that
// wants word-wrap without the VCS commit-message buffer's own workaround
// of inventing a fake on-disk path just to make the file-based tables
// above resolvable (see Editor/Vcs/VcsRunner.h's kVcsCommitMessageFilename
// comment) -- a real path drags in dedupe-by-path/save/AutoRevert/
// AutoMerge/FileWatch semantics a pure in-memory log buffer has no business
// picking up. Exact-name match only, no extension-style matching (a
// synthetic buffer name isn't a filename). Additive to the two tables
// above, not a replacement -- SetWrapForFilename/SetWrapForExtension still
// apply to any buffer that does have a real path.
void SetWrapForBufferName(const std::string& name, bool wrap);

[[nodiscard]] std::optional<bool> WrapLinesForBufferNameOverride(const std::string& name);

// path's own file-based override if path has a value and one is
// configured; else, for a path-less buffer, bufferName's own override if
// one is configured; else mode.wrapLines.
[[nodiscard]] bool EffectiveWrapLines(const std::optional<std::filesystem::path>& path, const std::string& bufferName,
                                      const Mode& mode);

} // namespace ned::editor

#endif // NED_EDITOR_WRAPOVERRIDES_H
