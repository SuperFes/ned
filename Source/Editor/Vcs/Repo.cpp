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

Repo LocalRepo() {
    return Repo{.destination = std::nullopt, .root = ProjectRoot()};
}

} // namespace ned::editor::vcs
