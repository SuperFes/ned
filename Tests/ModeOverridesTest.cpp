#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "Editor/ModeOverrides.h"
#include "Text/Buffer.h"

using ned::editor::ModeByName;
using ned::editor::ModeForBuffer;
using ned::editor::ModeForFileOverride;
using ned::editor::ModeForPath;
using ned::editor::RegisterDynamicMode;
using ned::editor::SetModeForExtension;
using ned::editor::SetModeForFilename;

namespace {

// See DynamicGrammarTest.cpp's own header comment: real, non-bundled,
// system-installed grammar + query, not FetchContent'd -- tests
// exercising the real load path SKIP rather than fail if absent.
const std::filesystem::path kLuaLibrary = "/usr/lib64/libtree-sitter-lua.so";
const std::filesystem::path kLuaQuery   = "/usr/share/tree-sitter/queries/lua/highlights.scm";

bool HasRealLuaFixture() {
    return std::filesystem::exists(kLuaLibrary) && std::filesystem::exists(kLuaQuery);
}

} // namespace

TEST_CASE("ModeByName resolves a bundled mode name", "[ModeOverrides]") {
    const std::optional<ned::editor::Mode> mode = ModeByName("json-mode");
    REQUIRE(mode.has_value());
    REQUIRE(mode->name == "json-mode");
    REQUIRE(static_cast<bool>(mode->highlight));
}

TEST_CASE("ModeByName returns nullopt for a name that is neither bundled nor dynamically registered",
          "[ModeOverrides]") {
    REQUIRE_FALSE(ModeByName("never-registered-language-xyz").has_value());
}

TEST_CASE("RegisterDynamicMode throws for a nonexistent library path", "[ModeOverrides]") {
    REQUIRE_THROWS_AS(
        RegisterDynamicMode("bogus-lang", "/not/a/real/path/libtree-sitter-bogus.so", "/not/a/real/query.scm"),
        std::runtime_error);
}

TEST_CASE("RegisterDynamicMode throws for a missing query file, even with a real library", "[ModeOverrides]") {
    if (!std::filesystem::exists(kLuaLibrary)) {
        SKIP("system-wide libtree-sitter-lua.so not found on this machine");
    }
    REQUIRE_THROWS_AS(RegisterDynamicMode("lua-missing-query", kLuaLibrary, "/not/a/real/query.scm"),
                      std::runtime_error);
}

TEST_CASE("RegisterDynamicMode + ModeByName round-trip with a real system grammar", "[ModeOverrides]") {
    if (!HasRealLuaFixture()) {
        SKIP("system-wide lua grammar/query not found on this machine");
    }

    RegisterDynamicMode("lua", kLuaLibrary, kLuaQuery);

    const std::optional<ned::editor::Mode> mode = ModeByName("lua");
    REQUIRE(mode.has_value());
    REQUIRE(static_cast<bool>(mode->highlight));

    const auto spans = mode->highlight("-- a comment\nlocal x = 1");
    REQUIRE_FALSE(spans.empty());
}

TEST_CASE("SetModeForExtension + ModeForFileOverride resolves through the extension table", "[ModeOverrides]") {
    if (!HasRealLuaFixture()) {
        SKIP("system-wide lua grammar/query not found on this machine");
    }

    RegisterDynamicMode("lua", kLuaLibrary, kLuaQuery);
    SetModeForExtension("lua", "lua"); // extension "lua" (no dot) -> mode name "lua"

    const std::optional<ned::editor::Mode> viaDot = ModeForFileOverride("/some/path/script.lua");
    REQUIRE(viaDot.has_value());
    REQUIRE(static_cast<bool>(viaDot->highlight));
}

TEST_CASE("SetModeForExtension accepts a leading dot too, resolving the same way", "[ModeOverrides]") {
    if (!HasRealLuaFixture()) {
        SKIP("system-wide lua grammar/query not found on this machine");
    }

    RegisterDynamicMode("lua", kLuaLibrary, kLuaQuery);
    SetModeForExtension(".lua", "lua"); // extension WITH a leading dot this time

    REQUIRE(ModeForFileOverride("/some/path/other.lua").has_value());
}

TEST_CASE("SetModeForExtension can point an extension at a bundled mode by name", "[ModeOverrides]") {
    SetModeForExtension("phtml-test-ext", "php-mode");

    const std::optional<ned::editor::Mode> mode = ModeForFileOverride("/some/path/file.phtml-test-ext");
    REQUIRE(mode.has_value());
    REQUIRE(mode->name == "php-mode");
}

TEST_CASE("SetModeForFilename resolves an exact filename with no distinguishing extension", "[ModeOverrides]") {
    SetModeForFilename("CMakeListsTest.txt", "c-mode");

    const std::optional<ned::editor::Mode> mode = ModeForFileOverride("/some/project/CMakeListsTest.txt");
    REQUIRE(mode.has_value());
    REQUIRE(mode->name == "c-mode");

    // A different file with the same extension isn't affected.
    REQUIRE_FALSE(ModeForFileOverride("/some/project/other-file-xyz.txt").has_value());
}

TEST_CASE("ModeForFileOverride prefers a filename match over an extension match", "[ModeOverrides]") {
    SetModeForExtension("special-test-ext", "python-mode");
    SetModeForFilename("SpecialFile.special-test-ext", "c-mode");

    const std::optional<ned::editor::Mode> mode = ModeForFileOverride("/some/path/SpecialFile.special-test-ext");
    REQUIRE(mode.has_value());
    REQUIRE(mode->name == "c-mode"); // filename table wins, not the extension table
}

TEST_CASE("ModeForFileOverride returns nullopt for a file with no override at all", "[ModeOverrides]") {
    REQUIRE_FALSE(ModeForFileOverride("/some/path/totally-unmapped-file.nobody-registered-this").has_value());
}

TEST_CASE("ModeForFileOverride returns nullopt if the mapped mode name resolves to nothing", "[ModeOverrides]") {
    SetModeForExtension("orphan-ext", "mode-name-nobody-registered");
    REQUIRE_FALSE(ModeForFileOverride("/some/path/file.orphan-ext").has_value());
}

TEST_CASE("ModeForPath resolves a representative sample of bundled extensions", "[ModeOverrides]") {
    REQUIRE(ModeForPath("/some/path/main.cpp").name == "cpp-mode");
    REQUIRE(ModeForPath("/some/path/script.py").name == "python-mode");
    REQUIRE(ModeForPath("/some/path/notes.md").name == "markdown-mode");
    REQUIRE(ModeForPath("/some/path/data.json").name == "json-mode");
    REQUIRE(ModeForPath("/some/path/outline.org").name == "org-mode");
    REQUIRE(ModeForPath("/some/path/config.yaml").name == "yaml-mode");
    REQUIRE(ModeForPath("/some/path/config.yml").name == "yaml-mode");
    REQUIRE(ModeForPath("/some/path/Cargo.toml").name == "toml-mode");
}

TEST_CASE("ModeForPath falls back to FundamentalMode for an unrecognized extension", "[ModeOverrides]") {
    REQUIRE(ModeForPath("/some/path/file.totally-unrecognized-xyz").name == "fundamental-mode");
}

TEST_CASE("ModeForPath prefers a configured override over the bundled extension table", "[ModeOverrides]") {
    SetModeForExtension("cpp", "python-mode"); // deliberately perverse, just to prove precedence
    REQUIRE(ModeForPath("/some/path/main.cpp").name == "python-mode");
    SetModeForExtension("cpp", "cpp-mode"); // restore, since g_extensionOverrides is process-wide state
}

TEST_CASE("ModeForBuffer falls back to FundamentalMode for a path-less buffer", "[ModeOverrides]") {
    ned::text::Buffer scratch("scratch");
    REQUIRE_FALSE(scratch.Path().has_value());
    REQUIRE(ModeForBuffer(scratch).name == "fundamental-mode");
}

TEST_CASE("ModeForBuffer resolves via the buffer's own path", "[ModeOverrides]") {
    ned::text::Buffer buffer = ned::text::Buffer::NewFile("/some/path/main.cpp");
    REQUIRE(ModeForBuffer(buffer).name == "cpp-mode");
}
