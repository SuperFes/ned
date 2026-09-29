#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <unistd.h>

#include "Editor/Image/Save.h"

using namespace ned::editor::image;

namespace {

// A fresh, empty directory, gone again afterwards.
struct ScratchDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("ned-image-save-" + std::to_string(::getpid()));
    ScratchDirectory() {
        std::filesystem::remove_all(path);
    }
    ~ScratchDirectory() {
        std::filesystem::remove_all(path);
    }
};

std::string FileBytes(const std::filesystem::path& path) {
    std::ifstream      file(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << file.rdbuf();
    return bytes.str();
}

} // namespace

TEST_CASE("UserDirFromConfig reads a user-dirs.dirs entry", "[Image]") {
    const std::filesystem::path home = "/home/someone";
    const std::string           config =
        "# written by xdg-user-dirs-update\n"
        "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
        "  XDG_DOWNLOAD_DIR=\"$HOME/Stuff/Downloads\"\r\n"
        "XDG_MUSIC_DIR=\"/srv/music\"\n"
        "XDG_TEMPLATES_DIR=\"$HOME/\"\n"
        "XDG_VIDEOS_DIR=\"relative/videos\"\n";
    REQUIRE(UserDirFromConfig(config, "XDG_DOWNLOAD_DIR", home) == home / "Stuff/Downloads");
    REQUIRE(UserDirFromConfig(config, "XDG_MUSIC_DIR", home) == std::filesystem::path("/srv/music"));
    REQUIRE(UserDirFromConfig(config, "XDG_TEMPLATES_DIR", home) == home / "");
    REQUIRE_FALSE(UserDirFromConfig(config, "XDG_VIDEOS_DIR", home).has_value());
    REQUIRE_FALSE(UserDirFromConfig(config, "XDG_PICTURES_DIR", home).has_value());
    REQUIRE_FALSE(UserDirFromConfig("XDG_DOWNLOAD_DIR_X=\"/x\"\nXDG_DOWNLOAD_DIR", "XDG_DOWNLOAD_DIR", home).has_value());
}

TEST_CASE("ImageFileExtension goes by MIME type, then leading bytes", "[Image]") {
    REQUIRE(ImageFileExtension("image/png", "") == ".png");
    REQUIRE(ImageFileExtension("image/jpeg", "") == ".jpg");
    REQUIRE(ImageFileExtension("image/svg+xml", "") == ".svg");
    REQUIRE(ImageFileExtension("image/gif", "") == ".gif");
    REQUIRE(ImageFileExtension("", "\x89PNG\r\n") == ".png");
    REQUIRE(ImageFileExtension("application/octet-stream", "\xFF\xD8\xFF\xE0") == ".jpg");
    REQUIRE(ImageFileExtension("", std::string_view("RIFF\x10\0\0\0WEBPVP8 ", 16)) == ".webp");
    REQUIRE(ImageFileExtension("image/../x", "GIF89a") == ".gif");
    REQUIRE(ImageFileExtension("", "plain text") == ".bin");
}

TEST_CASE("WriteNewFile creates the directory and never replaces a file", "[Image]") {
    ScratchDirectory  scratch;
    const auto        directory = scratch.path / "nested";
    const WrittenFile first     = WriteNewFile(directory, "picture", ".png", "one");
    REQUIRE(first.error.empty());
    REQUIRE(first.path == directory / "picture.png");
    const WrittenFile second = WriteNewFile(directory, "picture", ".png", "two");
    REQUIRE(second.path == directory / "picture-2.png");
    REQUIRE(FileBytes(first.path) == "one");
    REQUIRE(FileBytes(second.path) == "two");
}

TEST_CASE("WriteNewFile says why it couldn't write", "[Image]") {
    ScratchDirectory scratch;
    std::filesystem::create_directories(scratch.path);
    std::ofstream(scratch.path / "file") << "not a directory";
    const WrittenFile written = WriteNewFile(scratch.path / "file", "picture", ".png", "bytes");
    REQUIRE(written.path.empty());
    REQUIRE(written.error.starts_with("can't create "));
}
