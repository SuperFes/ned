#include "RemoteFile.h"

#include <algorithm>

namespace ned::text {

std::string RemoteFile::ToString() const {
    return destination + ":" + path.string();
}

std::optional<RemoteFile> ParseRemoteFile(std::string_view text) {
    const std::size_t split = text.find(":/");
    if (split == std::string_view::npos || split == 0) {
        return std::nullopt;
    }
    std::filesystem::path path = std::filesystem::path(text.substr(split + 1)).lexically_normal();
    if (!path.has_filename() && path != path.root_path()) {
        path = path.parent_path();
    }
    return RemoteFile{.destination = std::string(text.substr(0, split)), .path = std::move(path)};
}

std::string ShortDestination(std::string_view destination) {
    if (const std::size_t at = destination.rfind('@'); at != std::string_view::npos) {
        destination.remove_prefix(at + 1);
    }
    const bool address = destination.starts_with('[') ||
                         std::ranges::all_of(destination, [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
    if (!address) {
        destination = destination.substr(0, destination.find('.'));
    }
    return std::string(destination);
}

std::string RemoteBufferName(const RemoteFile& file) {
    return ShortDestination(file.destination) + ":" + file.path.filename().string();
}

} // namespace ned::text
