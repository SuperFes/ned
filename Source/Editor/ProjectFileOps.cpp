#include "ProjectFileOps.h"

#include <stdexcept>
#include <system_error>

namespace ned::editor {

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
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        throw std::runtime_error("ned: does not exist: " + path.string());
    }

    std::filesystem::remove_all(path, ec);
    if (ec) {
        throw std::runtime_error("ned: cannot delete: " + path.string() + ": " + ec.message());
    }
}

void RenameProjectPath(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!std::filesystem::exists(from, ec)) {
        throw std::runtime_error("ned: does not exist: " + from.string());
    }
    if (std::filesystem::exists(to, ec)) {
        throw std::runtime_error("ned: already exists: " + to.string());
    }

    std::filesystem::rename(from, to, ec);
    if (ec) {
        throw std::runtime_error("ned: cannot rename " + from.string() + " to " + to.string() + ": " + ec.message());
    }
}

} // namespace ned::editor
