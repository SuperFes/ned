//
// Filesystem create/delete/rename operations for the project tree (project-
// file-ops follow-up) -- thin, UI-agnostic wrappers over std::filesystem
// that throw std::runtime_error with a clear message on failure, the same
// convention ProjectReplace/Buffer::SaveToFile already established, so
// BufferView's own try/catch-and-report-via-statusMessage_ pattern works
// here unchanged.
//
// File *creation* is deliberately not here: find-file (C-x C-f) on a
// not-yet-existing path already creates a real, path-associated buffer
// (Buffer::NewFile) with no disk I/O until it's saved -- duplicating that
// as a separate command wouldn't add anything.
//

#ifndef NED_EDITOR_PROJECTFILEOPS_H
#define NED_EDITOR_PROJECTFILEOPS_H

#include <filesystem>

namespace ned::editor {

// Creates a directory at path, including any missing parent directories
// (mkdir -p semantics). Throws std::runtime_error if a regular file already
// exists at that exact path, or creation otherwise fails.
void CreateProjectDirectory(const std::filesystem::path& path);

// Deletes path -- a single file, or a directory and everything inside it,
// recursively. Throws std::runtime_error if path doesn't exist or deletion
// fails (permissions, etc.). Irreversible; callers are expected to confirm
// with the user first (see BufferView::HandleDeleteFileKey).
void DeleteProjectPath(const std::filesystem::path& path);

// Renames/moves from to to. Throws std::runtime_error if from doesn't
// exist, to already exists, or the rename fails -- including across
// filesystems/mount points, which std::filesystem::rename (POSIX rename(2)
// under the hood) does not support and this does not attempt to work
// around with a copy+delete fallback: atomic when it works, an honest
// failure when it can't, matching Buffer::SaveToFile's own
// <path>.ned-tmp-then-rename safety pattern.
void RenameProjectPath(const std::filesystem::path& from, const std::filesystem::path& to);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTFILEOPS_H
