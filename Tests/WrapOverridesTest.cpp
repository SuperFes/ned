#include <catch2/catch_test_macros.hpp>

#include "Editor/Mode.h"
#include "Editor/WrapOverrides.h"

using ned::editor::EffectiveWrapLines;
using ned::editor::FundamentalMode;
using ned::editor::SetWrapForExtension;
using ned::editor::SetWrapForFilename;
using ned::editor::WrapLinesForFileOverride;

TEST_CASE("WrapLinesForFileOverride returns nullopt for a file with no override at all", "[WrapOverrides]") {
    REQUIRE_FALSE(WrapLinesForFileOverride("/some/path/totally-unmapped-file.nobody-registered-this").has_value());
}

TEST_CASE("SetWrapForExtension + WrapLinesForFileOverride resolves through the extension table", "[WrapOverrides]") {
    SetWrapForExtension("wraptest1", true);

    const std::optional<bool> result = WrapLinesForFileOverride("/some/path/file.wraptest1");
    REQUIRE(result.has_value());
    REQUIRE(*result);
}

TEST_CASE("SetWrapForExtension accepts a leading dot too, resolving the same way", "[WrapOverrides]") {
    SetWrapForExtension(".wraptest2", false);

    const std::optional<bool> result = WrapLinesForFileOverride("/some/path/file.wraptest2");
    REQUIRE(result.has_value());
    REQUIRE_FALSE(*result);
}

TEST_CASE("SetWrapForFilename resolves an exact filename with no distinguishing extension", "[WrapOverrides]") {
    SetWrapForFilename("WrapTestFilename.txt", true);

    const std::optional<bool> result = WrapLinesForFileOverride("/some/project/WrapTestFilename.txt");
    REQUIRE(result.has_value());
    REQUIRE(*result);

    // A different file with the same extension isn't affected.
    REQUIRE_FALSE(WrapLinesForFileOverride("/some/project/other-file-wraptest.txt").has_value());
}

TEST_CASE("WrapLinesForFileOverride prefers a filename match over an extension match", "[WrapOverrides]") {
    SetWrapForExtension("wraptest3", true);
    SetWrapForFilename("SpecialFile.wraptest3", false);

    const std::optional<bool> result = WrapLinesForFileOverride("/some/path/SpecialFile.wraptest3");
    REQUIRE(result.has_value());
    REQUIRE_FALSE(*result); // filename table wins, not the extension table
}

TEST_CASE("EffectiveWrapLines falls back to the Mode's own default when no override is configured", "[WrapOverrides]") {
    ned::editor::Mode wrapOnMode  = FundamentalMode();
    wrapOnMode.wrapLines          = true;
    ned::editor::Mode wrapOffMode = FundamentalMode();
    wrapOffMode.wrapLines         = false;

    REQUIRE(EffectiveWrapLines(std::filesystem::path("/some/path/unmapped-wraptest.nobody-registered"), wrapOnMode));
    REQUIRE_FALSE(EffectiveWrapLines(std::filesystem::path("/some/path/unmapped-wraptest.nobody-registered"), wrapOffMode));
}

TEST_CASE("EffectiveWrapLines falls back to the Mode's own default for a path-less buffer", "[WrapOverrides]") {
    ned::editor::Mode wrapOnMode = FundamentalMode();
    wrapOnMode.wrapLines         = true;

    REQUIRE(EffectiveWrapLines(std::nullopt, wrapOnMode));
}

TEST_CASE("EffectiveWrapLines prefers a configured override over the Mode's own default", "[WrapOverrides]") {
    SetWrapForExtension("wraptest4", false);

    ned::editor::Mode wrapOnMode = FundamentalMode();
    wrapOnMode.wrapLines         = true;

    REQUIRE_FALSE(EffectiveWrapLines(std::filesystem::path("/some/path/file.wraptest4"), wrapOnMode));
}
