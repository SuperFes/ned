#include "RemoteFile.h"

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

} // namespace ned::text
