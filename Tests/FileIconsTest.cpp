#include <catch2/catch_test_macros.hpp>

#include "Editor/FileIcons.h"
#include "Editor/ModeOverrides.h"
#include "FileIconsOff.h"
#include "Text/Buffer.h"

using ned::editor::FileIcon;
using ned::editor::FolderIcon;
using ned::editor::Glyph;
using ned::editor::IconForPath;

namespace {

// c/language.janet's.
const Glyph kC{.text = "\ue61e", .rgb = 0x599eff};

} // namespace

TEST_CASE("IconForPath names a file's language icon from its name alone", "[FileIcons]") {
    CHECK(IconForPath("src/main.c") == kC);
    CHECK(IconForPath("does/not/exist.c") == kC);
    CHECK_FALSE(IconForPath("Makefile").empty());
    CHECK(IconForPath("notes.nedunknown").empty());
}

TEST_CASE("IconForPath follows an extension override, past its own cache", "[FileIcons]") {
    CHECK(IconForPath("a.nedicontest").empty());
    ned::editor::SetModeForExtension("nedicontest", "c-mode");
    CHECK(IconForPath("a.nedicontest") == kC);
    // One naming no mode leaves the file to the bundled tables, as ModeForPath.
    ned::editor::SetModeForExtension("c", "no-such-mode");
    CHECK(IconForPath("b.c") == kC);
    ned::editor::SetModeForExtension("c", "c-mode");
}

TEST_CASE("FileIcon falls back to a kind glyph, then a plain document", "[FileIcons]") {
    CHECK(FileIcon(std::filesystem::path("x.c")) == kC);
    CHECK(FileIcon(std::filesystem::path("shot.PNG")) == Glyph{.text = "\ue60d", .rgb = 0xa074c4});
    CHECK(FileIcon(std::filesystem::path("release.tar")).text == "\uf410");
    CHECK(FileIcon(std::filesystem::path("LICENSE")).text == "\ue60a");
    CHECK(FileIcon(std::filesystem::path("README")) == Glyph{.text = "\uf016", .rgb = std::nullopt});
    CHECK(FolderIcon(false) == Glyph{.text = "\uf07b", .rgb = std::nullopt});
    CHECK(FolderIcon(true).text == "\uf07c");
}

TEST_CASE("FileIcon of a buffer reads its path, its host path, or neither", "[FileIcons]") {
    ned::text::Buffer local("main.c");
    local.SetPath("main.c");
    CHECK(FileIcon(local) == kC);

    ned::text::Buffer remote("main.c");
    remote.SetRemote(ned::text::RemoteFile{.destination = "box", .path = "/src/main.c"});
    CHECK(FileIcon(remote) == kC);

    const ned::text::Buffer scratch("*scratch*");
    CHECK(FileIcon(scratch).text == "\uf016");
}

TEST_CASE("File and folder icons are empty while file type icons are off", "[FileIcons]") {
    const FileIconsOff      noIcons;
    const ned::text::Buffer scratch("*scratch*");
    CHECK(FileIcon(std::filesystem::path("x.c")).empty());
    CHECK(FileIcon(scratch).empty());
    CHECK(FolderIcon(true).empty());
}
