#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "Editor/LanguageRegistry.h"
#include "Editor/ModeOverrides.h"
#include "Text/Buffer.h"

using ned::editor::CachedModeForBuffer;
using ned::editor::ClearModeCacheFor;
using ned::editor::ClearRegisteredLanguages;
using ned::editor::InsertPrewarmedMode;
using ned::editor::LoadLanguageDirectory;
using ned::editor::ModeByName;
using ned::editor::ModeForBuffer;
using ned::editor::ModeForFileOverride;
using ned::editor::ModeForPath;
using ned::editor::SetModeForExtension;
using ned::editor::SetModeForFilename;

namespace {

// See DynamicGrammarTest.cpp's own header comment: real, non-bundled,
// system-installed grammar + query, not FetchContent'd -- tests
// exercising the real load path SKIP rather than fail if absent.
// kLuaQueriesDir is exactly the shape a real system tree-sitter install
// uses (/usr/share/tree-sitter/queries/<lang>/highlights.scm, ...) -- the
// :queries-dir key exists for it.
const std::filesystem::path kLuaLibrary    = "/usr/lib64/libtree-sitter-lua.so";
const std::filesystem::path kLuaQueriesDir = "/usr/share/tree-sitter/queries/lua";
const std::filesystem::path kLuaQuery      = kLuaQueriesDir / "highlights.scm";

bool HasRealLuaFixture() {
    return std::filesystem::exists(kLuaLibrary) && std::filesystem::exists(kLuaQuery);
}

// A scratch language directory in the bundled layout: <parent>/lua/
// language.janet pointing at the system grammar. `extra` appends more
// definition keys.
std::filesystem::path WriteLuaLanguageDir(const std::string& extra = {}) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned-test-languages" / "lua";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "language.janet", std::ios::trunc);
    out << "{:name \"lua\"\n :extensions [\".lua\"]\n :line-comment \"--\"\n"
        << " :grammar-library \"" << kLuaLibrary.string() << "\"\n"
        << extra << "}\n";
    out.close();
    return dir;
}

// Registration is process-wide; every test that registers clears after
// itself, the same discipline the override-table tests follow.
struct RegistryGuard {
    ~RegistryGuard() {
        ClearRegisteredLanguages();
    }
};

} // namespace

TEST_CASE("ModeByName resolves a bundled mode name", "[ModeOverrides]") {
    const std::optional<ned::editor::Mode> mode = ModeByName("json-mode");
    REQUIRE(mode.has_value());
    REQUIRE(mode->name == "json-mode");
    REQUIRE(static_cast<bool>(mode->highlight));
}

TEST_CASE("ModeByName returns nullopt for a name that is neither bundled nor registered",
          "[ModeOverrides]") {
    REQUIRE_FALSE(ModeByName("never-registered-language-xyz").has_value());
}

TEST_CASE("LoadLanguageDirectory throws for a nonexistent grammar library", "[ModeOverrides]") {
    const RegistryGuard         guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned-test-languages" / "bogus";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "language.janet", std::ios::trunc)
        << "{:name \"bogus\" :grammar-library \"/not/a/real/libtree-sitter-bogus.so\"}\n";
    REQUIRE_THROWS_AS(LoadLanguageDirectory(dir), std::runtime_error);
    REQUIRE_FALSE(ModeByName("bogus-mode").has_value()); // nothing half-registered
}

TEST_CASE("A grammar-library definition with no query files still registers, parser only", "[ModeOverrides]") {
    if (!std::filesystem::exists(kLuaLibrary)) {
        SKIP("system-wide libtree-sitter-lua.so not found on this machine");
    }
    const RegistryGuard guard;
    LoadLanguageDirectory(WriteLuaLanguageDir());

    const std::optional<ned::editor::Mode> mode = ModeByName("lua-mode");
    REQUIRE(mode.has_value());
    REQUIRE_FALSE(static_cast<bool>(mode->highlight));
}

TEST_CASE("A :queries-dir pointing at a system tree-sitter install round-trips", "[ModeOverrides]") {
    if (!HasRealLuaFixture()) {
        SKIP("system-wide lua grammar/query not found on this machine");
    }
    const RegistryGuard guard;
    LoadLanguageDirectory(WriteLuaLanguageDir(" :queries-dir \"" + kLuaQueriesDir.string() + "\"\n"));

    const std::optional<ned::editor::Mode> mode = ModeByName("lua-mode");
    REQUIRE(mode.has_value());
    REQUIRE(static_cast<bool>(mode->highlight));

    const auto spans = mode->highlight("-- a comment\nlocal x = 1", ned::editor::HighlightWindow{});
    REQUIRE_FALSE(spans.empty());
    REQUIRE(mode->lineCommentPrefix == "--");
}

TEST_CASE("A registered definition's own :extensions claim files with no override call", "[ModeOverrides]") {
    if (!HasRealLuaFixture()) {
        SKIP("system-wide lua grammar/query not found on this machine");
    }
    const RegistryGuard guard;
    LoadLanguageDirectory(WriteLuaLanguageDir(" :queries-dir \"" + kLuaQueriesDir.string() + "\"\n"));

    REQUIRE(ModeForPath("/some/path/script.lua").name == "lua-mode");

    // The override tables still work, and still win over the claim.
    SetModeForExtension(".lua", "python-mode");
    REQUIRE(ModeForPath("/some/path/script.lua").name == "python-mode");
    SetModeForExtension(".lua", "lua-mode");
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
    REQUIRE(ModeForPath("/some/path/core.clj").name == "clojure-mode");
    REQUIRE(ModeForPath("/some/path/app.cljs").name == "clojure-mode");
    REQUIRE(ModeForPath("/some/path/shared.cljc").name == "clojure-mode");
    REQUIRE(ModeForPath("/some/path/deps.edn").name == "clojure-mode");
    REQUIRE(ModeForPath("/some/path/task.bb").name == "clojure-mode");
    REQUIRE(ModeForPath("/some/path/main.jank").name == "jank-mode");
    REQUIRE(ModeForPath("/some/path/Widget.java").name == "java-mode");
    REQUIRE(ModeForPath("/some/path/Widget.kt").name == "kotlin-mode");
    // A Gradle build script is Kotlin source, not a dialect of its own.
    REQUIRE(ModeForPath("/some/path/build.gradle.kts").name == "kotlin-mode");
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

// per-buffer-mode-cache follow-up. Every test here calls ClearModeCacheFor
// at the end to restore process-wide cache state for whichever test runs
// next -- same discipline "ModeForPath prefers a configured override..."
// already follows for g_extensionOverrides above.

TEST_CASE("CachedModeForBuffer resolves via the buffer's own path, same as ModeForBuffer, when nothing is cached yet",
          "[ModeOverrides]") {
    ned::text::Buffer buffer = ned::text::Buffer::NewFile("/some/path/cached-resolve-test.cpp");
    REQUIRE(CachedModeForBuffer(buffer).name == "cpp-mode");
    ClearModeCacheFor(buffer);
}

TEST_CASE("CachedModeForBuffer falls back to FundamentalMode for a path-less buffer, same as ModeForBuffer",
          "[ModeOverrides]") {
    ned::text::Buffer scratch("scratch");
    REQUIRE(CachedModeForBuffer(scratch).name == "fundamental-mode");
    ClearModeCacheFor(scratch);
}

TEST_CASE("CachedModeForBuffer returns a manually pre-inserted (prewarmed) Mode instead of resolving fresh",
          "[ModeOverrides]") {
    // No override registered for this extension at all -- a real
    // (uncached) resolution would fall back to FundamentalMode, so seeing
    // anything else back proves the cache was actually consulted first.
    ned::text::Buffer buffer = ned::text::Buffer::NewFile("/some/path/prewarm-identity-test.prewarm-test-ext");

    ned::editor::Mode fake;
    fake.name = "fake-prewarmed-mode";
    InsertPrewarmedMode(buffer, fake);

    REQUIRE(CachedModeForBuffer(buffer).name == "fake-prewarmed-mode");
    ClearModeCacheFor(buffer);
}

TEST_CASE("InsertPrewarmedMode never clobbers a Mode some other path already cached", "[ModeOverrides]") {
    ned::text::Buffer buffer = ned::text::Buffer::NewFile("/some/path/no-clobber-test.no-clobber-test-ext");

    ned::editor::Mode first;
    first.name = "first-cached-mode";
    InsertPrewarmedMode(buffer, first);
    REQUIRE(CachedModeForBuffer(buffer).name == "first-cached-mode");

    ned::editor::Mode second;
    second.name = "second-cached-mode";
    InsertPrewarmedMode(buffer, second); // no-op: buffer is already cached

    REQUIRE(CachedModeForBuffer(buffer).name == "first-cached-mode");
    ClearModeCacheFor(buffer);
}

TEST_CASE("ClearModeCacheFor removes a buffer's cached Mode, so the next call resolves fresh", "[ModeOverrides]") {
    ned::text::Buffer buffer = ned::text::Buffer::NewFile("/some/path/clear-cache-test.cpp");

    ned::editor::Mode fake;
    fake.name = "fake-mode";
    InsertPrewarmedMode(buffer, fake);
    REQUIRE(CachedModeForBuffer(buffer).name == "fake-mode");

    ClearModeCacheFor(buffer);
    REQUIRE(CachedModeForBuffer(buffer).name == "cpp-mode"); // real resolution, not the stale fake entry
}
