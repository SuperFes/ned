#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/ProjectPlugins.h"

using ned::editor::ProjectPluginFiles;

namespace {

std::filesystem::path FreshTestDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& content = "") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}

} // namespace

TEST_CASE("ProjectPluginFiles is empty when .ned/plugins doesn't exist", "[ProjectPlugins]") {
    const std::filesystem::path root = FreshTestDir("ned_project_plugins_test_missing");
    REQUIRE(ProjectPluginFiles(root).empty());
}

TEST_CASE("ProjectPluginFiles is empty when .ned exists but plugins doesn't", "[ProjectPlugins]") {
    const std::filesystem::path root = FreshTestDir("ned_project_plugins_test_no_plugins_dir");
    std::filesystem::create_directories(root / ".ned");
    REQUIRE(ProjectPluginFiles(root).empty());
}

TEST_CASE("ProjectPluginFiles lists only .janet files, sorted", "[ProjectPlugins]") {
    const std::filesystem::path root = FreshTestDir("ned_project_plugins_test_list");
    WriteFile(root / ".ned" / "plugins" / "zeta.janet");
    WriteFile(root / ".ned" / "plugins" / "alpha.janet");
    WriteFile(root / ".ned" / "plugins" / "README.md");
    WriteFile(root / ".ned" / "plugins" / "notes.txt");

    const auto files = ProjectPluginFiles(root);
    REQUIRE(files.size() == 2);
    REQUIRE(files[0].filename() == "alpha.janet");
    REQUIRE(files[1].filename() == "zeta.janet");
}

TEST_CASE("ProjectPluginFiles doesn't recurse into subdirectories", "[ProjectPlugins]") {
    const std::filesystem::path root = FreshTestDir("ned_project_plugins_test_norecurse");
    WriteFile(root / ".ned" / "plugins" / "top.janet");
    WriteFile(root / ".ned" / "plugins" / "nested" / "inner.janet");

    const auto files = ProjectPluginFiles(root);
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].filename() == "top.janet");
}
