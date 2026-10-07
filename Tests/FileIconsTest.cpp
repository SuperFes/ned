#include <catch2/catch_test_macros.hpp>

#include "Editor/FileIcons.h"
#include "Editor/ModeOverrides.h"
#include "FileIconsOff.h"
#include "Text/Buffer.h"

using ned::editor::FileIcon;
using ned::editor::FolderIcon;
using ned::editor::IconForPath;

namespace {

constexpr const char* kCIcon = "\ue61e"; // c/language.janet's

} // namespace

TEST_CASE("IconForPath names a file's language icon from its name alone", "[FileIcons]") {
    CHECK(IconForPath("src/main.c") == kCIcon);
    CHECK(IconForPath("does/not/exist.c") == kCIcon);
    CHECK_FALSE(IconForPath("Makefile").empty());
    CHECK(IconForPath("notes.nedunknown").empty());
}

TEST_CASE("IconForPath follows an extension override, past its own cache", "[FileIcons]") {
    CHECK(IconForPath("a.nedicontest").empty());
    ned::editor::SetModeForExtension("nedicontest", "c-mode");
    CHECK(IconForPath("a.nedicontest") == kCIcon);
    // One naming no mode leaves the file to the bundled tables, as ModeForPath.
    ned::editor::SetModeForExtension("c", "no-such-mode");
    CHECK(IconForPath("b.c") == kCIcon);
    ned::editor::SetModeForExtension("c", "c-mode");
}

TEST_CASE("FileIcon falls back to a kind glyph, then a plain document", "[FileIcons]") {
    CHECK(FileIcon(std::filesystem::path("x.c")) == kCIcon);
    CHECK(FileIcon(std::filesystem::path("shot.PNG")) == "\ue60d");
    CHECK(FileIcon(std::filesystem::path("release.tar")) == "\uf410");
    CHECK(FileIcon(std::filesystem::path("LICENSE")) == "\ue60a");
    CHECK(FileIcon(std::filesystem::path("README")) == "\uf016");
    CHECK(FolderIcon(false) == "\uf07b");
    CHECK(FolderIcon(true) == "\uf07c");
}

TEST_CASE("FileIcon of a buffer reads its path, its host path, or neither", "[FileIcons]") {
    ned::text::Buffer local("main.c");
    local.SetPath("main.c");
    CHECK(FileIcon(local) == kCIcon);

    ned::text::Buffer remote("main.c");
    remote.SetRemote(ned::text::RemoteFile{.destination = "box", .path = "/src/main.c"});
    CHECK(FileIcon(remote) == kCIcon);

    const ned::text::Buffer scratch("*scratch*");
    CHECK(FileIcon(scratch) == "\uf016");
}

TEST_CASE("File and folder icons are empty while file type icons are off", "[FileIcons]") {
    const FileIconsOff      noIcons;
    const ned::text::Buffer scratch("*scratch*");
    CHECK(FileIcon(std::filesystem::path("x.c")).empty());
    CHECK(FileIcon(scratch).empty());
    CHECK(FolderIcon(true).empty());
}
