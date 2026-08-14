#include "ProjectRoot.h"

#include <array>
#include <mutex>
#include <system_error>

namespace ned::editor {

namespace {

    std::mutex& RootMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::filesystem::path& RootStorage() {
        static std::filesystem::path root = std::filesystem::current_path();
        return root;
    }

    std::mutex& AutoDetectMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoDetectStorage() {
        static bool enabled = true;
        return enabled;
    }

    // Directory names that mark a VCS repository root -- checked as an
    // immediate child of each ancestor while walking upward.
    constexpr std::array<const char*, 4> kVcsMarkers = {".git", ".hg", ".svn", ".bzr"};

    bool HasVcsMarker(const std::filesystem::path& directory) {
        for (const char* marker : kVcsMarkers) {
            std::error_code ec;
            if (std::filesystem::exists(directory / marker, ec)) {
                return true;
            }
        }
        return false;
    }

} // namespace

void SetProjectRoot(std::filesystem::path root) {
    const std::lock_guard<std::mutex> lock(RootMutex());
    RootStorage() = std::move(root);
}

std::filesystem::path ProjectRoot() {
    const std::lock_guard<std::mutex> lock(RootMutex());
    return RootStorage();
}

void SetAutoDetectProjectRoot(bool enabled) {
    const std::lock_guard<std::mutex> lock(AutoDetectMutex());
    AutoDetectStorage() = enabled;
}

bool AutoDetectProjectRoot() {
    const std::lock_guard<std::mutex> lock(AutoDetectMutex());
    return AutoDetectStorage();
}

std::filesystem::path DetectProjectRoot(const std::filesystem::path& openedPath) {
    std::error_code       ec;
    std::filesystem::path start = std::filesystem::absolute(openedPath, ec);
    if (ec) {
        start = openedPath;
    }

    if (std::filesystem::is_directory(start, ec)) {
        return start;
    }

    const std::filesystem::path containing = start.parent_path();

    if (!AutoDetectProjectRoot()) {
        return containing;
    }

    for (std::filesystem::path dir = containing;; dir = dir.parent_path()) {
        if (HasVcsMarker(dir)) {
            return dir;
        }
        if (dir == dir.parent_path()) { // reached the filesystem root
            break;
        }
    }

    return containing;
}

} // namespace ned::editor
