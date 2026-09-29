#include "Save.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace ned::editor::image {

namespace {

    std::optional<std::filesystem::path> EnvPath(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return std::nullopt;
        }
        return std::filesystem::path(value);
    }

    bool WriteAll(int fd, std::string_view bytes) {
        while (!bytes.empty()) {
            const ssize_t written = ::write(fd, bytes.data(), bytes.size());
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
        return true;
    }

} // namespace

std::optional<std::filesystem::path> UserDirFromConfig(std::string_view config, std::string_view key, const std::filesystem::path& home) {
    while (!config.empty()) {
        const std::size_t newline = config.find('\n');
        std::string_view  line    = config.substr(0, newline);
        config                    = newline == std::string_view::npos ? std::string_view() : config.substr(newline + 1);
        line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
        if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != '=') {
            continue;
        }
        std::string_view value = line.substr(key.size() + 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
            value.remove_suffix(1);
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (value == "$HOME") {
            return home;
        }
        if (value.starts_with("$HOME/")) {
            return home / value.substr(6);
        }
        if (value.starts_with('/')) {
            return std::filesystem::path(value);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> DownloadDirectory() {
    if (std::optional<std::filesystem::path> dir = EnvPath("XDG_DOWNLOAD_DIR")) {
        return dir;
    }
    const std::optional<std::filesystem::path> home = EnvPath("HOME");
    if (!home) {
        return std::nullopt;
    }
    const std::filesystem::path configHome = EnvPath("XDG_CONFIG_HOME").value_or(*home / ".config");
    if (std::ifstream file(configHome / "user-dirs.dirs"); file) {
        std::ostringstream text;
        text << file.rdbuf();
        if (std::optional<std::filesystem::path> dir = UserDirFromConfig(text.str(), "XDG_DOWNLOAD_DIR", *home)) {
            return dir;
        }
    }
    return *home / "Downloads";
}

std::string ImageFileExtension(std::string_view mimeType, std::string_view bytes) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kRenamed{{
        {"image/jpeg", ".jpg"},
        {"image/jpg", ".jpg"},
        {"image/svg+xml", ".svg"},
        {"image/x-icon", ".ico"},
        {"image/vnd.microsoft.icon", ".ico"},
    }};
    for (const auto& [type, extension] : kRenamed) {
        if (mimeType == type) {
            return std::string(extension);
        }
    }
    if (mimeType.starts_with("image/")) {
        const std::string_view subtype = mimeType.substr(6);
        const bool             plain   = !subtype.empty() && subtype.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") == std::string_view::npos;
        if (plain) {
            return "." + std::string(subtype);
        }
    }
    if (bytes.starts_with("\x89PNG")) {
        return ".png";
    }
    if (bytes.starts_with("\xFF\xD8\xFF")) {
        return ".jpg";
    }
    if (bytes.starts_with("GIF8")) {
        return ".gif";
    }
    if (bytes.size() >= 12 && bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP") {
        return ".webp";
    }
    return ".bin";
}

WrittenFile WriteNewFile(const std::filesystem::path& directory, std::string_view stem, std::string_view extension, std::string_view bytes) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {.error = "can't create " + directory.string() + ": " + error.message()};
    }
    constexpr int kMaxAttempts = 1000;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        const std::string           name = std::string(stem) + (attempt == 1 ? std::string() : "-" + std::to_string(attempt)) + std::string(extension);
        const std::filesystem::path path = directory / name;
        // O_EXCL: two saves in the same second, or a file already there,
        // take the next name rather than clobbering.
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return {.error = "can't write " + path.string() + ": " + std::strerror(errno)};
        }
        const bool written = WriteAll(fd, bytes);
        const int  saved   = errno;
        if (::close(fd) != 0 || !written) {
            std::filesystem::remove(path, error);
            return {.error = "can't write " + path.string() + ": " + std::strerror(written ? errno : saved)};
        }
        return {.path = path};
    }
    return {.error = "no free file name for " + std::string(stem) + std::string(extension) + " in " + directory.string()};
}

} // namespace ned::editor::image
