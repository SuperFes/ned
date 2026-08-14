//
// The project root that project-wide features (ProjectSidebar, project-
// search, project-replace) operate against -- one process-wide path
// (mutex-guarded static state, mirroring TabWidth.h/FormatOnSave.h's exact
// pattern), replacing each of those three's own previously-independent
// std::filesystem::current_path() calls with a single, coherent concept.
//
// Detected once at startup (see DetectProjectRoot) from whatever path was
// opened on the command line, not re-derived automatically afterward --
// e.g. switching to a file in an unrelated directory via find-file does not
// change it. An explicit v1 scope cut, not an oversight; there is no
// interactive "change project root" command yet, though SetProjectRoot is
// public and Janet-reachable the same way every other process-wide setting
// in this codebase is, so scripting one is already possible.
//

#ifndef NED_EDITOR_PROJECTROOT_H
#define NED_EDITOR_PROJECTROOT_H

#include <filesystem>

namespace ned::editor {

void                                SetProjectRoot(std::filesystem::path root);
[[nodiscard]] std::filesystem::path ProjectRoot();

// Whether DetectProjectRoot walks upward looking for a VCS marker directory
// at all -- on by default. The user's own request: root detection should be
// configurable, not forced. Configured from Janet
// (ned/set-auto-detect-project-root).
void               SetAutoDetectProjectRoot(bool enabled);
[[nodiscard]] bool AutoDetectProjectRoot();

// Determines the project root for openedPath (a file or a directory, as
// passed on the command line):
//  - If openedPath is a directory, it IS the root, unconditionally -- an
//    explicitly opened directory always wins over VCS detection ("if we
//    just open a directory, that can be the project root regardless").
//  - If openedPath is a file and AutoDetectProjectRoot() is on, walks
//    upward from its containing directory looking for a VCS marker
//    directory (.git, .hg, .svn, .bzr); the nearest ancestor containing one
//    becomes the root, otherwise falls back to the file's own containing
//    directory.
//  - If openedPath is a file and AutoDetectProjectRoot() is off, always
//    just the file's own containing directory -- no VCS walk.
// Returns an absolute path regardless of whether openedPath itself was
// relative; a nonexistent openedPath (e.g. `ned newfile.txt`) is treated as
// a file (its containing directory is used), not a directory.
[[nodiscard]] std::filesystem::path DetectProjectRoot(const std::filesystem::path& openedPath);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTROOT_H
