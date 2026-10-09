//
// A file tree's VCS colouring, shared by the project sidebar and the Remote
// panel: each changed file's status, every directory between it and the
// repository root the most severe status beneath it (the propagation VS
// Code's own file-tree decorations do), and the colour a row takes for it.
//

#ifndef NED_UI_FILETREEVCS_H
#define NED_UI_FILETREEVCS_H

#include <filesystem>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "Editor/Vcs/Provider.h"
#include "Editor/Vcs/RowStatus.h"
#include "Theme.h"

namespace ned::ui {

using VcsStatusIndex = std::unordered_map<std::filesystem::path, editor::vcs::RowStatus>;

// conflicted names files the caller found still holding conflict markers,
// which read as Conflicted whatever their status code says.
[[nodiscard]] VcsStatusIndex BuildVcsStatusIndex(const std::vector<editor::vcs::StatusEntry>& entries, const std::filesystem::path& root,
                                                 const std::set<std::filesystem::path>& conflicted = {});

[[nodiscard]] editor::vcs::RowStatus LookupVcsStatus(const VcsStatusIndex& index, const std::filesystem::path& path);

// Reuses the diff gutter's own colours rather than adding Theme fields --
// foreground-only, the choice Theme.h's diffAddedBackground comment
// documents (a background wash fought syntax-highlight contrast).
[[nodiscard]] std::optional<Color> VcsStatusColor(editor::vcs::RowStatus status, const Theme& theme);

} // namespace ned::ui

#endif // NED_UI_FILETREEVCS_H
