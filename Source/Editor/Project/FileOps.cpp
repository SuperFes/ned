#include "FileOps.h"

#include <stdexcept>
#include <system_error>

namespace ned::editor {

namespace {

    // A dangling symlink is still there to delete or rename.
    bool Present(const std::filesystem::path& path, std::error_code& ec) {
        return std::filesystem::exists(std::filesystem::symlink_status(path, ec));
    }

} // namespace

void CreateProjectDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec)) {
        throw std::runtime_error("ned: a file already exists at: " + path.string());
    }

    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("ned: cannot create directory: " + path.string() + ": " + ec.message());
    }
}

void DeleteProjectPath(const std::filesystem::path& path) {
    if (path.has_root_path() && path.lexically_normal() == path.root_path()) {
        throw std::runtime_error("ned: refusing to delete " + path.string());
    }
    std::error_code ec;
    if (!Present(path, ec)) {
        throw std::runtime_error("ned: does not exist: " + path.string());
    }

    std::filesystem::remove_all(path, ec);
    if (ec) {
        throw std::runtime_error("ned: cannot delete: " + path.string() + ": " + ec.message());
    }
}

void RenameProjectPath(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!Present(from, ec)) {
        throw std::runtime_error("ned: does not exist: " + from.string());
    }
    if (Present(to, ec)) {
        throw std::runtime_error("ned: already exists: " + to.string());
    }

    std::filesystem::rename(from, to, ec);
    if (ec) {
        throw std::runtime_error("ned: cannot rename " + from.string() + " to " + to.string() + ": " + ec.message());
    }
}

} // namespace ned::editor
