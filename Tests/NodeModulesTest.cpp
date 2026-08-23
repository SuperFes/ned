#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/NodeModules.h"

using ned::editor::NodeModulesSearchPaths;

namespace {

// One disposable temp tree per test, mirroring GitIgnoreTest.cpp's/
// ProjectSettingsTest.cpp's own "real filesystem, cleaned up in the
// destructor" convention rather than a fake filesystem abstraction.
struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name = "ned-node-modules-test")
        : root(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempTree() {
        std::filesystem::remove_all(root);
    }
};

} // namespace

TEST_CASE("NodeModulesSearchPaths finds node_modules at both the file's own directory and the project root", "[NodeModules]") {
    TempTree tree;
    std::filesystem::create_directories(tree.root / "node_modules");
    std::filesystem::create_directories(tree.root / "src" / "node_modules");
    std::filesystem::create_directories(tree.root / "src" / "a");

    const auto paths = NodeModulesSearchPaths(tree.root / "src" / "a", tree.root);

    REQUIRE(paths.size() == 2);
    CHECK(paths[0] == std::filesystem::weakly_canonical(tree.root / "src" / "node_modules"));
    CHECK(paths[1] == std::filesystem::weakly_canonical(tree.root / "node_modules"));
}

TEST_CASE("NodeModulesSearchPaths skips a level with no node_modules directory", "[NodeModules]") {
    TempTree tree;
    std::filesystem::create_directories(tree.root / "node_modules");
    std::filesystem::create_directories(tree.root / "src" / "a");

    const auto paths = NodeModulesSearchPaths(tree.root / "src" / "a", tree.root);

    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == std::filesystem::weakly_canonical(tree.root / "node_modules"));
}

TEST_CASE("NodeModulesSearchPaths returns nothing when no ancestor has a node_modules directory", "[NodeModules]") {
    TempTree tree;
    std::filesystem::create_directories(tree.root / "src" / "a");

    const auto paths = NodeModulesSearchPaths(tree.root / "src" / "a", tree.root);
    CHECK(paths.empty());
}

TEST_CASE("NodeModulesSearchPaths stops at the filesystem root when baseDirectory isn't under projectRoot", "[NodeModules]") {
    TempTree tree1("ned-node-modules-test-1");
    TempTree tree2("ned-node-modules-test-2");
    std::filesystem::create_directories(tree1.root / "a");

    // projectRoot (tree2) is unrelated to baseDirectory (under tree1) --
    // must terminate rather than loop, and must still search baseDirectory's
    // own real ancestors (the filesystem root itself has no node_modules,
    // so this should just come back empty without hanging).
    const auto paths = NodeModulesSearchPaths(tree1.root / "a", tree2.root);
    CHECK(paths.empty());
}
