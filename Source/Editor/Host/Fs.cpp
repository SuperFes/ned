#include "Fs.h"

#include <ostream>
#include <system_error>

#include "Text/FilePreservation.h"
#include "Text/SavePlan.h"

namespace ned::editor::host {

FileStat StatPath(const std::filesystem::path& path) {
    std::error_code                    ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    FileStat                           stat;
    switch (status.type()) {
        case std::filesystem::file_type::not_found:
        case std::filesystem::file_type::none:
            return stat;
        case std::filesystem::file_type::regular:
            stat.kind = FileKind::File;
            stat.size = std::filesystem::file_size(path, ec);
            if (ec) {
                stat.size = 0;
            }
            break;
        case std::filesystem::file_type::directory:
            stat.kind = FileKind::Directory;
            break;
        default:
            stat.kind = FileKind::Other;
            break;
    }
    stat.mode            = static_cast<std::uint32_t>(status.permissions() & std::filesystem::perms::mask);
    const auto timestamp = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        stat.timestamp = timestamp;
    }
    return stat;
}

text::FileContents ReadPath(const std::filesystem::path& path) {
    const FileStat stat = StatPath(path);
    if (stat.kind == FileKind::Missing) {
        throw FsError(fs_error::kNotFound, "no such file: " + path.string());
    }
    if (stat.kind == FileKind::Directory) {
        throw FsError(fs_error::kFailed, path.string() + " is a directory");
    }
    try {
        return text::ReadFileContents(path);
    }
    catch (const std::runtime_error& e) {
        throw FsError(fs_error::kFailed, e.what());
    }
}

FileStat WritePath(const FsWriteRequest& request) {
    if (request.expect) {
        const FileStat now = StatPath(request.path);
        if (!*request.expect && now.kind != FileKind::Missing) {
            throw FsError(fs_error::kFileChanged, request.path.string() + " was created since it was read");
        }
        if (*request.expect && (now.kind == FileKind::Missing || now.timestamp != *request.expect)) {
            throw FsError(fs_error::kFileChanged, request.path.string() + (now.kind == FileKind::Missing ? " was removed" : " changed") + " since it was read");
        }
    }
    try {
        const std::filesystem::path target = text::ResolveSaveTarget(request.path);
        text::ReplaceFileContent(target, text::CaptureFileAttributes(target), [&request](std::ostream& out) {
            out.write(request.data.data(), static_cast<std::streamsize>(request.data.size()));
        });
    }
    catch (const std::exception& e) {
        throw FsError(fs_error::kFailed, e.what());
    }
    return StatPath(request.path);
}

} // namespace ned::editor::host
