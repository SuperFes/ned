//
// A repository VCS operations work in: one on this machine, or one on a
// remote host, where its commands run and its paths are the host's.
//

#ifndef NED_EDITOR_VCS_REPO_H
#define NED_EDITOR_VCS_REPO_H

#include <compare>
#include <filesystem>
#include <optional>
#include <string>

#include "Text/RemoteFile.h"

namespace ned::editor::vcs {

struct Repo {
    std::optional<std::string> destination; // unset for this machine
    std::filesystem::path      root;

    [[nodiscard]] bool IsRemote() const {
        return destination.has_value();
    }
    // Where a process for it runs (Remote/Placement.h).
    [[nodiscard]] std::optional<text::RemoteFile> Beside() const;
    // The root as the mode line writes a file: destination:/root remotely.
    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] auto operator<=>(const Repo&) const = default;
};

// The project root's, on this machine.
[[nodiscard]] Repo LocalRepo();

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_REPO_H
