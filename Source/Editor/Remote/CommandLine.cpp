#include "CommandLine.h"

#include <filesystem>
#include <system_error>

namespace ned::editor::remote {

std::optional<text::RemoteFile> RemoteArgument(std::string_view argument) {
    std::optional<text::RemoteFile> file = text::ParseRemoteFile(argument);
    if (!file || file->destination.find('/') != std::string::npos) {
        return std::nullopt;
    }
    std::error_code ec;
    if (std::filesystem::symlink_status(std::filesystem::path(argument), ec).type() != std::filesystem::file_type::not_found && !ec) {
        return std::nullopt;
    }
    return file;
}

} // namespace ned::editor::remote
