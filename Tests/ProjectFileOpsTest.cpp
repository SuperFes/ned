#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/ProjectFileOps.h"

using ned::editor::CreateProjectDirectory;
using ned::editor::DeleteProjectPath;
using ned::editor::RenameProjectPath;

TEST_CASE("CreateProjectDirectory creates a new directory", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_create";
    std::filesystem::remove_all(dir);

    CreateProjectDirectory(dir);

    REQUIRE(std::filesystem::is_directory(dir));

    std::filesystem::remove_all(dir);
}

TEST_CASE("CreateProjectDirectory creates missing parent directories too", "[ProjectFileOps]") {
    const std::filesystem::path base   = std::filesystem::temp_directory_path() / "ned_project_fileops_test_nested";
    const std::filesystem::path target = base / "a" / "b" / "c";
    std::filesystem::remove_all(base);

    CreateProjectDirectory(target);

    REQUIRE(std::filesystem::is_directory(target));

    std::filesystem::remove_all(base);
}

TEST_CASE("CreateProjectDirectory is a no-op-ish success when the directory already exists", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_existing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    CreateProjectDirectory(dir); // must not throw -- create_directories tolerates an existing directory

    REQUIRE(std::filesystem::is_directory(dir));

    std::filesystem::remove_all(dir);
}

TEST_CASE("CreateProjectDirectory throws if a regular file already exists at that path", "[ProjectFileOps]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_project_fileops_test_conflict";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    REQUIRE_THROWS_AS(CreateProjectDirectory(path), std::runtime_error);

    std::filesystem::remove_all(path);
}

TEST_CASE("DeleteProjectPath deletes a single file", "[ProjectFileOps]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_project_fileops_test_delfile.txt";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    DeleteProjectPath(path);

    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("DeleteProjectPath deletes a directory and everything inside it, recursively", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_deldir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "file.txt") << "x";
    }

    DeleteProjectPath(dir);

    REQUIRE_FALSE(std::filesystem::exists(dir));
}

TEST_CASE("DeleteProjectPath throws for a path that doesn't exist", "[ProjectFileOps]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_project_fileops_test_missing";
    std::filesystem::remove_all(path);

    REQUIRE_THROWS_AS(DeleteProjectPath(path), std::runtime_error);
}

TEST_CASE("RenameProjectPath renames a file", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_rename";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "old.txt";
    const std::filesystem::path to   = dir / "new.txt";
    {
        std::ofstream(from) << "content";
    }

    RenameProjectPath(from, to);

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to));
    {
        std::ifstream in(to);
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "content");
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("RenameProjectPath renames a directory", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_renamedir";
    std::filesystem::remove_all(dir);
    const std::filesystem::path from = dir / "old";
    const std::filesystem::path to   = dir / "new";
    std::filesystem::create_directories(from);

    RenameProjectPath(from, to);

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::is_directory(to));

    std::filesystem::remove_all(dir);
}

TEST_CASE("RenameProjectPath throws when the source doesn't exist", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_rename_nosrc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE_THROWS_AS(RenameProjectPath(dir / "nope.txt", dir / "new.txt"), std::runtime_error);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RenameProjectPath throws when the destination already exists", "[ProjectFileOps]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_fileops_test_rename_dstexists";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "from.txt") << "x";
    }
    {
        std::ofstream(dir / "to.txt") << "x";
    }

    REQUIRE_THROWS_AS(RenameProjectPath(dir / "from.txt", dir / "to.txt"), std::runtime_error);

    std::filesystem::remove_all(dir);
}
