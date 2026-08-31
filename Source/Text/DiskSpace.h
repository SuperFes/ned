//
// Free-disk-space safety check for the huge-file-editing storage engine
// (Text/PieceTable.h) -- the atomic save pattern used throughout this
// codebase (write a full sibling .ned-tmp, then rename over the target)
// needs the new content's full size in free space before the rename can
// even happen, and a copy-on-write filesystem (Btrfs, ZFS) can transiently
// need close to double that -- COW never overwrites blocks in place, so
// the old file's blocks may not be reclaimed immediately (especially under
// a snapshot). This module answers one question -- "is there comfortably
// enough free space to save this much content" -- filesystem-type-agnostic
// for now; see Buffer::FromHugeFile/SaveToFile for how the answer is used
// (a soft, overridable read-only downgrade at open time, a hard backstop
// with no override at save time).
//

#ifndef NED_TEXT_DISKSPACE_H
#define NED_TEXT_DISKSPACE_H

#include <cstdint>
#include <filesystem>
#include <optional>

namespace ned::text {

struct DiskSpaceCheck {
    bool           sufficient = true;
    std::uintmax_t availableBytes = 0;
    std::uintmax_t requiredBytes  = 0;
};

// requiredBytes is contentBytes * multiplier (the caller resolves the
// multiplier setting -- this function has no config dependency of its
// own). availableBytes comes from std::filesystem::space(path)'s "free
// space available to a non-privileged process" figure, unless
// availableBytesOverride is set (test-only -- real disk space can't be
// deterministically controlled in a unit test, same reasoning
// Editor/Backup.cpp's nowSeconds injection already uses for time). A
// filesystem query failure (path's filesystem can't be statted) is
// reported as *insufficient* -- failing safe, not silently assumed fine.
[[nodiscard]] DiskSpaceCheck CheckFreeSpaceForSave(const std::filesystem::path& path, std::uintmax_t contentBytes, double multiplier,
                                                   std::optional<std::uintmax_t> availableBytesOverride = std::nullopt);

} // namespace ned::text

#endif // NED_TEXT_DISKSPACE_H
