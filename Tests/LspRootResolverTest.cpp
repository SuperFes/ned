#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Editor/Lsp/RootResolver.h"
#include "Editor/Project/Root.h"

using ned::editor::AutoDetectProjectRoot;
using ned::editor::ProjectRoot;
using ned::editor::SetAutoDetectProjectRoot;
using ned::editor::SetProjectRoot;
using ned::editor::lsp::RootMarkers;
using ned::editor::lsp::ResolveLspRoot;
using ned::editor::lsp::SetLspRootMarkers;

namespace {

// ProjectRoot()/AutoDetectProjectRoot() are process-wide state -- mirrors
// ProjectRootTest.cpp's own ProjectRootGuard/AutoDetectGuard exactly, so a
// test that changes either restores it even if a REQUIRE fails partway
// through.
struct RootStateGuard {
    std::filesystem::path previousRoot       = ProjectRoot();
    bool                  previousAutoDetect = AutoDetectProjectRoot();
    ~RootStateGuard() {
        SetProjectRoot(previousRoot);
        SetAutoDetectProjectRoot(previousAutoDetect);
    }
};

} // namespace

TEST_CASE("RootMarkers is empty for a language with no default and no override", "[Lsp]") {
    REQUIRE(RootMarkers("a-language-nobody-configured").empty());
}

TEST_CASE("RootMarkers returns compiled-in defaults for bundled languages", "[Lsp]") {
    const auto cpp = RootMarkers("cpp");
    REQUIRE_FALSE(cpp.empty());
    REQUIRE(std::find(cpp.begin(), cpp.end(), "CMakeLists.txt") != cpp.end());

    const auto python = RootMarkers("python");
    REQUIRE_FALSE(python.empty());
    REQUIRE(std::find(python.begin(), python.end(), "pyproject.toml") != python.end());

    const auto csharp = RootMarkers("csharp");
    REQUIRE_FALSE(csharp.empty());
    REQUIRE(std::find(csharp.begin(), csharp.end(), "*.csproj") != csharp.end());

    const auto java = RootMarkers("java");
    REQUIRE_FALSE(java.empty());
    REQUIRE(std::find(java.begin(), java.end(), "pom.xml") != java.end());
    REQUIRE(std::find(java.begin(), java.end(), "build.gradle") != java.end());

    const auto kotlin = RootMarkers("kotlin");
    REQUIRE_FALSE(kotlin.empty());
    REQUIRE(std::find(kotlin.begin(), kotlin.end(), "build.gradle.kts") != kotlin.end());
}

TEST_CASE("SetLspRootMarkers overrides the default, and an empty list reverts to it", "[Lsp]") {
    SetLspRootMarkers("cpp", {"WORKSPACE"});
    REQUIRE(RootMarkers("cpp") == std::vector<std::string>{"WORKSPACE"});

    SetLspRootMarkers("cpp", {}); // clears the override -- reverts to the compiled-in default
    REQUIRE_FALSE(RootMarkers("cpp").empty());
    REQUIRE(std::find(RootMarkers("cpp").begin(), RootMarkers("cpp").end(), "CMakeLists.txt") != RootMarkers("cpp").end());
}

TEST_CASE("ResolveLspRoot falls back to ProjectRoot() when nothing is configured for the language", "[Lsp]") {
    RootStateGuard guard;
    SetProjectRoot(std::filesystem::temp_directory_path());

    const std::filesystem::path buffer = std::filesystem::temp_directory_path() / "ned-lsp-root-resolver-test" / "file.txt";
    REQUIRE(ResolveLspRoot(buffer, "a-language-nobody-configured") == std::filesystem::temp_directory_path());
}

TEST_CASE("ResolveLspRoot walks upward to the nearest directory containing a configured marker", "[Lsp]") {
    RootStateGuard guard;
    SetProjectRoot(std::filesystem::temp_directory_path()); // the outer "monorepo root" -- deliberately NOT the expected answer here

    const std::filesystem::path monorepoRoot = std::filesystem::temp_directory_path() / "ned-lsp-root-resolver-monorepo";
    const std::filesystem::path packageRoot  = monorepoRoot / "packages" / "widget";
    const std::filesystem::path sourceDir    = packageRoot / "src";
    std::filesystem::create_directories(sourceDir);
    {
        std::ofstream(packageRoot / "package.json") << "{}";
    }

    SetLspRootMarkers("ned-lsp-root-resolver-test-lang", {"package.json"});
    REQUIRE(ResolveLspRoot(sourceDir / "index.js", "ned-lsp-root-resolver-test-lang") == packageRoot);
    SetLspRootMarkers("ned-lsp-root-resolver-test-lang", {}); // cleanup -- process-wide state

    std::filesystem::remove_all(monorepoRoot);
}

TEST_CASE("ResolveLspRoot never walks -- and falls straight to ProjectRoot() -- when auto-detect is off", "[Lsp]") {
    RootStateGuard guard;
    SetProjectRoot(std::filesystem::temp_directory_path());
    SetAutoDetectProjectRoot(false);

    const std::filesystem::path monorepoRoot = std::filesystem::temp_directory_path() / "ned-lsp-root-resolver-autodetect-off";
    const std::filesystem::path packageRoot  = monorepoRoot / "packages" / "widget";
    std::filesystem::create_directories(packageRoot);
    {
        std::ofstream(packageRoot / "package.json") << "{}";
    }

    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-2", {"package.json"});
    REQUIRE(ResolveLspRoot(packageRoot / "index.js", "ned-lsp-root-resolver-test-lang-2") == std::filesystem::temp_directory_path());
    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-2", {}); // cleanup

    std::filesystem::remove_all(monorepoRoot);
}

TEST_CASE("ResolveLspRoot matches a \"*.<ext>\" marker against any file with that extension, csharp bundling "
          "follow-up",
          "[Lsp]") {
    RootStateGuard guard;
    SetProjectRoot(std::filesystem::temp_directory_path()); // deliberately NOT the expected answer here

    const std::filesystem::path monorepoRoot = std::filesystem::temp_directory_path() / "ned-lsp-root-resolver-csproj-glob";
    const std::filesystem::path projectRoot  = monorepoRoot / "src" / "Widget";
    std::filesystem::create_directories(projectRoot);
    {
        // .NET's own convention: the file is named after the project, never
        // a fixed name -- MarkerExistsInDirectory's whole reason to exist.
        std::ofstream(projectRoot / "Widget.csproj") << "<Project />";
    }

    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-4", {"*.csproj"});
    REQUIRE(ResolveLspRoot(projectRoot / "Program.cs", "ned-lsp-root-resolver-test-lang-4") == projectRoot);
    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-4", {}); // cleanup

    std::filesystem::remove_all(monorepoRoot);
}

TEST_CASE("ResolveLspRoot falls back to ProjectRoot() when no ancestor has the marker", "[Lsp]") {
    RootStateGuard guard;
    SetProjectRoot(std::filesystem::temp_directory_path());

    const std::filesystem::path buffer = std::filesystem::temp_directory_path() / "ned-lsp-root-resolver-no-marker" / "file.py";
    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-3", {"a-marker-file-that-will-never-exist.toml"});
    REQUIRE(ResolveLspRoot(buffer, "ned-lsp-root-resolver-test-lang-3") == std::filesystem::temp_directory_path());
    SetLspRootMarkers("ned-lsp-root-resolver-test-lang-3", {}); // cleanup
}
