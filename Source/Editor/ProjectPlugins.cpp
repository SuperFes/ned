#include "ProjectPlugins.h"

#include <algorithm>
#include <system_error>

namespace ned::editor {

std::vector<std::filesystem::path> ProjectPluginFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;

    const std::filesystem::path pluginsDir = root / ".ned" / "plugins";
    std::error_code              ec;
    if (!std::filesystem::is_directory(pluginsDir, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(pluginsDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".janet") {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        return {};
    }

    std::sort(files.begin(), files.end());
    return files;
}

} // namespace ned::editor
