#include "Repo.h"

#include "Editor/Project/Root.h"

namespace ned::editor::vcs {

std::optional<text::RemoteFile> Repo::Beside() const {
    if (!destination) {
        return std::nullopt;
    }
    return text::RemoteFile{.destination = *destination, .path = root};
}

std::string Repo::ToString() const {
    return destination ? text::RemoteFile{.destination = *destination, .path = root}.ToString() : root.string();
}

std::filesystem::path Repo::Name(const std::filesystem::path& relative) const {
    const std::filesystem::path path = root / relative;
    return destination ? std::filesystem::path(text::RemoteFile{.destination = *destination, .path = path}.ToString()) : path;
}

std::filesystem::path Repo::PathOf(const std::filesystem::path& name) const {
    if (destination) {
        if (const std::optional<text::RemoteFile> file = text::ParseRemoteFile(name.string()); file && file->destination == *destination) {
            return file->path;
        }
    }
    return name;
}

Repo LocalRepo() {
    return Repo{.destination = std::nullopt, .root = ProjectRoot()};
}

} // namespace ned::editor::vcs
