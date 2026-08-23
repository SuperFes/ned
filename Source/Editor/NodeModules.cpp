#include "NodeModules.h"

#include <system_error>

namespace ned::editor {

std::vector<std::filesystem::path> NodeModulesSearchPaths(const std::filesystem::path& baseDirectory,
                                                           const std::filesystem::path& projectRoot) {
    std::error_code errorCode;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, errorCode);

    std::vector<std::filesystem::path> searchPaths;
    std::filesystem::path              dir = std::filesystem::weakly_canonical(baseDirectory, errorCode);
    if (dir.empty()) {
        dir = baseDirectory;
    }

    while (true) {
        std::error_code existsError;
        if (const std::filesystem::path candidate = dir / "node_modules";
            std::filesystem::is_directory(candidate, existsError)) {
            searchPaths.push_back(candidate);
        }

        if (!errorCode && !canonicalRoot.empty() && dir == canonicalRoot) {
            break;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break; // reached the filesystem root
        }
        dir = parent;
    }

    return searchPaths;
}

} // namespace ned::editor
