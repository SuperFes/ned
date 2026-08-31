#include "DiskSpace.h"

#include <limits>

namespace ned::text {

DiskSpaceCheck CheckFreeSpaceForSave(const std::filesystem::path& path, std::uintmax_t contentBytes, double multiplier,
                                     std::optional<std::uintmax_t> availableBytesOverride) {
    // Clamped before the cast: a double-to-uintmax_t conversion where the
    // value exceeds the target's range is undefined behavior, not a clean
    // saturate/wrap -- reachable only with a pathological multiplier (real
    // configured values are ~1.5-3.0), but cheap to make well-defined
    // regardless of what a caller (or a test forcing "insufficient"
    // unconditionally) passes.
    const double requiredDouble = static_cast<double>(contentBytes) * multiplier;
    const std::uintmax_t requiredBytes =
        requiredDouble >= static_cast<double>(std::numeric_limits<std::uintmax_t>::max())
            ? std::numeric_limits<std::uintmax_t>::max()
            : static_cast<std::uintmax_t>(requiredDouble);

    if (availableBytesOverride) {
        return DiskSpaceCheck{*availableBytesOverride >= requiredBytes, *availableBytesOverride, requiredBytes};
    }

    std::error_code ec;
    // space() wants an existing path -- a not-yet-saved file's own path may
    // not exist yet (e.g. never reached for FromHugeFile, always reached
    // for the first save of what would be a brand-new huge buffer, not a
    // real scenario today since FromHugeFile requires an existing file,
    // but SaveToFile could in principle be called with a fresh path) --
    // fall back to the parent directory in that case.
    std::filesystem::space_info info = std::filesystem::space(path, ec);
    if (ec) {
        info = std::filesystem::space(path.parent_path(), ec);
    }
    if (ec) {
        // Fail safe: an unstatable filesystem is reported as insufficient,
        // not silently assumed fine.
        return DiskSpaceCheck{false, 0, requiredBytes};
    }

    const std::uintmax_t availableBytes = info.available;
    return DiskSpaceCheck{availableBytes >= requiredBytes, availableBytes, requiredBytes};
}

} // namespace ned::text
