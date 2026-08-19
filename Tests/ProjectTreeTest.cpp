#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/ProjectTree.h"

using ned::editor::BuildProjectTree;
using ned::editor::ProjectTreeEntry;

TEST_CASE("BuildProjectTree lists directories before files, each group sorted", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_ordering";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "zdir");
    std::filesystem::create_directories(dir / "adir");
    {
        std::ofstream(dir / "bfile.txt") << "x";
    }
    {
        std::ofstream(dir / "afile.txt") << "x";
    }

    const std::vector<ProjectTreeEntry> entries = BuildProjectTree(dir);

    REQUIRE(entries.size() == 4);
    REQUIRE(entries[0].path.filename() == "adir");
    REQUIRE(entries[0].isDirectory);
    REQUIRE(entries[1].path.filename() == "zdir");
    REQUIRE(entries[1].isDirectory);
    REQUIRE(entries[2].path.filename() == "afile.txt");
    REQUIRE_FALSE(entries[2].isDirectory);
    REQUIRE(entries[3].path.filename() == "bfile.txt");
    REQUIRE_FALSE(entries[3].isDirectory);

    std::filesystem::remove_all(dir);
}

TEST_CASE("BuildProjectTree is depth-first: a directory's children immediately follow it", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_depthfirst";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "adir");
    {
        std::ofstream(dir / "adir" / "nested.txt") << "x";
    }
    {
        std::ofstream(dir / "zfile.txt") << "x";
    }

    const std::vector<ProjectTreeEntry> entries = BuildProjectTree(dir);

    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].path.filename() == "adir");
    REQUIRE(entries[0].depth == 0);
    REQUIRE(entries[1].path.filename() == "nested.txt");
    REQUIRE(entries[1].depth == 1); // immediately after its parent, not deferred to the end
    REQUIRE(entries[2].path.filename() == "zfile.txt");
    REQUIRE(entries[2].depth == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("BuildProjectTree skips dot-directories entirely", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_dotdir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / ".git");
    {
        std::ofstream(dir / ".git" / "config") << "x";
    }
    {
        std::ofstream(dir / "visible.txt") << "x";
    }

    const std::vector<ProjectTreeEntry> entries = BuildProjectTree(dir);

    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].path.filename() == "visible.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("BuildProjectTree returns absolute paths regardless of how root was given", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_absolute";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "x";
    }

    const std::vector<ProjectTreeEntry> entries = BuildProjectTree(dir);

    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].path.is_absolute());

    std::filesystem::remove_all(dir);
}

TEST_CASE("BuildProjectTree returns an empty list for a nonexistent root, without throwing", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_missing";
    std::filesystem::remove_all(dir);

    REQUIRE(BuildProjectTree(dir).empty());
}

TEST_CASE("BuildProjectTree returns an empty list for an empty directory", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_empty";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE(BuildProjectTree(dir).empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("BuildProjectTree skips a directory excluded by .gitignore, without descending into it", "[ProjectTree]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_tree_test_gitignore";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "build" / "nested");
    {
        std::ofstream(dir / ".gitignore") << "build/\n";
    }
    {
        std::ofstream(dir / "build" / "nested" / "hidden.txt") << "x";
    }
    {
        std::ofstream(dir / "visible.txt") << "x";
    }

    const std::vector<ProjectTreeEntry> entries = BuildProjectTree(dir);

    // .gitignore itself is a real, listed file (not a dot-directory -- only
    // directories are excluded by IsDotDirectory), so "visible.txt" and
    // ".gitignore" are the only two entries -- "build/" and everything
    // under it must not appear at all.
    REQUIRE(entries.size() == 2);
    for (const ProjectTreeEntry& entry : entries) {
        REQUIRE(entry.path.filename() != "build");
        REQUIRE(entry.path.filename() != "hidden.txt");
    }

    std::filesystem::remove_all(dir);
}
