//
// The branch last seen checked out in each project root, for whatever
// shows or uses it without running the VCS itself (the mode line's issue
// key). Recorded by Vcs/Runner whenever it lists, switches or creates
// branches; a switch made outside ned shows up on the next of those.
//

#ifndef NED_EDITOR_VCS_CURRENTBRANCH_H
#define NED_EDITOR_VCS_CURRENTBRANCH_H

#include <filesystem>
#include <optional>
#include <string>

namespace ned::editor::vcs {

// nullopt forgets it: a detached HEAD, or a root that isn't a repository.
void                                     SetCurrentBranch(const std::filesystem::path& root, std::optional<std::string> branch);
[[nodiscard]] std::optional<std::string> CurrentBranch(const std::filesystem::path& root);

// Test-only.
void ClearCurrentBranches();

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_CURRENTBRANCH_H
