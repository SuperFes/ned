//
// What the fs namespace does on the host (Host/FsCodec.h): the same reads
// and writes a local load and save run (Text/FileContents.h,
// Text/SavePlan.h), so a remote file is read and replaced exactly as a
// local one is.
//

#ifndef NED_EDITOR_HOST_FS_H
#define NED_EDITOR_HOST_FS_H

#include <filesystem>
#include <stdexcept>
#include <string>

#include "Editor/Host/FsCodec.h"
#include "Text/FileContents.h"

namespace ned::editor::host {

// Carries the fs_error code a request answers with.
class FsError : public std::runtime_error {
  public:
    FsError(int code, const std::string& message) : std::runtime_error(message), code_(code) {
    }
    [[nodiscard]] int Code() const noexcept {
        return code_;
    }

  private:
    int code_;
};

// Follows symlinks; a path that doesn't resolve is Missing.
[[nodiscard]] FileStat StatPath(const std::filesystem::path& path);

// Throws FsError: kNotFound, or kFailed for anything but a regular file
// and for a failed read.
[[nodiscard]] text::FileContents ReadPath(const std::filesystem::path& path);

// Replaces the file (through a symlink, its target) as a local save does,
// and returns its stat afterwards. Throws FsError: kFileChanged when
// request.expect no longer holds, kFailed when the write fails.
FileStat WritePath(const FsWriteRequest& request);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_FS_H
