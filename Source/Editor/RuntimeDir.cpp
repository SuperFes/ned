#include "RuntimeDir.h"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace ned::editor {

namespace {

    void EnsureOwnerOnlyDirectory(const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            throw std::runtime_error("ned: failed to create runtime directory " + dir.string() + ": " + ec.message());
        }
        std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
        if (ec) {
            throw std::runtime_error("ned: failed to restrict permissions on runtime directory " + dir.string() + ": " + ec.message());
        }
    }

} // namespace

std::filesystem::path RuntimeDirectory() {
    if (const char* xdgRuntimeHome = std::getenv("XDG_RUNTIME_DIR"); xdgRuntimeHome && *xdgRuntimeHome) {
        return std::filesystem::path(xdgRuntimeHome) / "ned";
    }
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "run";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "run";
    }
    throw std::runtime_error("ned: cannot determine a runtime directory (none of XDG_RUNTIME_DIR, XDG_STATE_HOME, HOME is set)");
}

std::filesystem::path EnsureRuntimeDirectory(std::string_view subdirectory) {
    std::filesystem::path dir = RuntimeDirectory();
    EnsureOwnerOnlyDirectory(dir);
    if (!subdirectory.empty()) {
        dir /= subdirectory;
        EnsureOwnerOnlyDirectory(dir);
    }
    return dir;
}

} // namespace ned::editor
