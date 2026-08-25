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
    // weakly_canonical, not plain absolute: absolute() only prepends the cwd
    // and leaves any "." / ".." components in place, so opening "../README.md"
    // left the detected root's own parent_path() literally ending in "..",
    // which then surfaced verbatim as the sidebar header's project name
    // (ProjectSidebar's own ProjectNameLabel takes root.filename(), and ".."
    // is its own last path component as far as std::filesystem is concerned,
    // not resolved away). weakly_canonical resolves the real, existing
    // portion of the path (symlinks included) and only falls back to lexical
    // normalization for a trailing portion that doesn't exist yet -- doesn't
    // throw for a not-yet-existing file the way canonical() would, which
    // matters since openedPath may be a new-file path (see
    // BufferList::OpenOrCreateFile) that doesn't exist on disk yet.
    // Absolutized *before* weakly_canonical, not after: for a relative path
    // where no leading portion exists at all (`./ned ROADMAP.md` from a
    // build directory that has no ROADMAP.md), libstdc++'s weakly_canonical
    // returns the input unchanged -- still relative, ec unset -- so
    // parent_path() below came out empty and the marker walk returned that
    // empty path verbatim as the "root", which then crashed the first LSP
    // handshake (absolute("") throws) on the first paint. Found from a real
    // core dump, not review.
    std::error_code       ec;
    std::filesystem::path start = std::filesystem::absolute(openedPath, ec);
    if (ec) {
        start = openedPath;
    }
    if (const std::filesystem::path canonical = std::filesystem::weakly_canonical(start, ec); !ec) {
        start = canonical;
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
