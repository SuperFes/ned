//
// Tracker panels that appear with no configuration: every provider is
// shown the project's git remotes and names the connections and panels it
// recognizes there (the GitHub plugin, a github.com remote).
//

#ifndef NED_EDITOR_TRACKER_DETECT_H
#define NED_EDITOR_TRACKER_DETECT_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::tracker {

// The URLs in `git remote -v` output, each once, in order.
[[nodiscard]] std::vector<std::string> ParseRemoteUrls(std::string_view remoteOutput);

// Runs git synchronously; empty outside a repository or without git.
[[nodiscard]] std::vector<std::string> GitRemoteUrls(const std::filesystem::path& root);

// Registers what every provider detects in remoteUrls. Configuration wins:
// a detected connection whose name is already declared is skipped with its
// panels, and so is a detected panel whose name is. A provider that throws
// is logged and skipped. Returns the names of the panels added.
std::vector<std::string> AddDetectedPanels(const std::vector<std::string>& remoteUrls);

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_DETECT_H
