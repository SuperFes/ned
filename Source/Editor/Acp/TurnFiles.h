//
// The files an agent turn changed, as text: read the way the user sees them
// (an open buffer's live text before the disk), and written back the way
// an agent's own write lands (to disk, then the open buffer follows) --
// what rewind and the turn review restore through. A host's file is only
// tracked while it's open here: its disk can't be read or written without
// waiting on the host.
//

#ifndef NED_EDITOR_ACP_TURNFILES_H
#define NED_EDITOR_ACP_TURNFILES_H

#include <filesystem>
#include <optional>
#include <string>

#include "Text/RemoteFile.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor::acp {

// A file's text, or its absence.
struct FileState {
    bool        exists = false;
    std::string text;

    bool operator==(const FileState&) const = default;
};

// One file a turn changed: as it was before the turn first touched it, and
// as the turn left it (unset until the turn ends).
struct TurnFile {
    text::FileRef            file;
    FileState                before;
    std::optional<FileState> after;
};

// Files past this are left out of turn tracking.
inline constexpr std::size_t kMaxTurnFileBytes = 4 * 1024 * 1024;

// The file's text: an open buffer's, else the disk's. nullopt when it can't
// be tracked -- too large, binary, unreadable, still loading, or a host's
// file that isn't open.
[[nodiscard]] std::optional<FileState> ReadTurnFile(const text::BufferList& bufferList, const text::FileRef& file);

// Makes the file read `state`. An open buffer with unsaved changes is
// edited in place and left unsaved; otherwise the disk is written (after a
// backup version) and an open buffer reverts to it. A state that doesn't
// exist deletes the file. A host's file is edited in its open buffer and
// left for the user to save; one that isn't open, or a delete there, is
// refused. Throws std::runtime_error on failure.
void WriteTurnFile(text::BufferList& bufferList, const text::FileRef& file, const FileState& state);

// Sibling-temp-file + rename, preserving the file's attributes -- see
// Text/FilePreservation.h.
void WriteFileAtomically(const std::filesystem::path& path, const std::string& content);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_TURNFILES_H
