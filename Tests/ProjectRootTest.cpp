#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/ProjectRoot.h"

using ned::editor::AutoDetectProjectRoot;
using ned::editor::DetectProjectRoot;
using ned::editor::ProjectRoot;
using ned::editor::SetAutoDetectProjectRoot;
using ned::editor::SetProjectRoot;

namespace {

// AutoDetectProjectRoot is process-wide state; every test that changes it
// must restore the default for the next test, guaranteed via RAII rather
// than a manual reset at the end (which a failed REQUIRE partway through
// would skip). Mirrors TabWidthTest.cpp's own TabWidthGuard exactly.
struct AutoDetectGuard {
    ~AutoDetectGuard() {
        SetAutoDetectProjectRoot(true);
    }
};

// The process's cwd is likewise shared, global state across the whole test
// binary (every test case runs in the same process; see
// BufferViewTest.cpp/ProjectSidebarTest.cpp's own identical CurrentPathGuard
// for why that matters) -- restored on scope exit even if a REQUIRE fails
// partway through.
class CurrentPathGuard {
  public:
    explicit CurrentPathGuard(const std::filesystem::path& newPath) : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(newPath);
    }
    ~CurrentPathGuard() {
        std::filesystem::current_path(previous_);
    }
    CurrentPathGuard(const CurrentPathGuard&)            = delete;
    CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;

  private:
    std::filesystem::path previous_;
};

} // namespace

TEST_CASE("ProjectRoot defaults to the process's current path", "[ProjectRoot]") {
    REQUIRE(ProjectRoot() == std::filesystem::current_path());
}

TEST_CASE("SetProjectRoot/ProjectRoot round-trip", "[ProjectRoot]") {
    SetProjectRoot("/some/path");
    REQUIRE(ProjectRoot() == std::filesystem::path("/some/path"));
}

TEST_CASE("AutoDetectProjectRoot defaults to true", "[ProjectRoot]") {
    REQUIRE(AutoDetectProjectRoot());
}

TEST_CASE("SetAutoDetectProjectRoot/AutoDetectProjectRoot round-trip", "[ProjectRoot]") {
    const AutoDetectGuard guard;

    SetAutoDetectProjectRoot(false);
    REQUIRE_FALSE(AutoDetectProjectRoot());
    SetAutoDetectProjectRoot(true);
    REQUIRE(AutoDetectProjectRoot());
}

TEST_CASE("DetectProjectRoot returns an opened directory itself, absolute", "[ProjectRoot]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE(DetectProjectRoot(dir) == std::filesystem::absolute(dir));

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot returns an opened directory itself even if a VCS marker exists further up",
          "[ProjectRoot]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_dirwins";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / ".git");
    std::filesystem::create_directories(dir / "subdir");

    // Opening "subdir" directly should win over the ".git" marker one level
    // up -- an explicitly opened directory is always the root, regardless.
    REQUIRE(DetectProjectRoot(dir / "subdir") == std::filesystem::absolute(dir / "subdir"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot walks upward from a file to find a VCS marker directory", "[ProjectRoot]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_walk";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / ".git");
    std::filesystem::create_directories(dir / "src" / "nested");
    {
        std::ofstream(dir / "src" / "nested" / "file.txt") << "x";
    }

    REQUIRE(DetectProjectRoot(dir / "src" / "nested" / "file.txt") == std::filesystem::absolute(dir));

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot falls back to the containing directory when no VCS marker exists", "[ProjectRoot]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_nomarker";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "x";
    }

    // No .git/.hg/.svn/.bzr anywhere above a plain temp directory in any
    // normal environment -- falls all the way back to file.txt's own
    // containing directory.
    REQUIRE(DetectProjectRoot(dir / "file.txt") == std::filesystem::absolute(dir));

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot never walks upward when AutoDetectProjectRoot() is off", "[ProjectRoot]") {
    const AutoDetectGuard guard;
    SetAutoDetectProjectRoot(false);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_disabled";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / ".git");
    std::filesystem::create_directories(dir / "src");
    {
        std::ofstream(dir / "src" / "file.txt") << "x";
    }

    // Would normally find dir's own ".git" -- disabled, so it just uses the
    // file's containing directory ("src"), same as a directory with no
    // marker at all would.
    REQUIRE(DetectProjectRoot(dir / "src" / "file.txt") == std::filesystem::absolute(dir / "src"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot resolves a relative \"..\" path to the real parent directory, not literal \"..\"",
          "[ProjectRoot]") {
    // A real reported bug: `cd build && ./ned ../README.md` made the
    // sidebar header (which reads ProjectRoot().filename()) show ".."
    // verbatim -- plain std::filesystem::absolute() prepends the cwd but
    // never resolves "." / ".." components, so the detected root's own
    // parent_path() ended in a literal "..", whose own filename() is ".."
    // itself, not the real containing directory's name.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_dotdot";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "file.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir / "sub");

    const std::filesystem::path detected = DetectProjectRoot("../file.txt");

    REQUIRE(detected == std::filesystem::canonical(dir));
    REQUIRE(detected.filename() != "..");

    std::filesystem::remove_all(dir);
}

TEST_CASE("DetectProjectRoot treats a nonexistent path as a file, not a directory", "[ProjectRoot]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_root_test_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    // "newfile.txt" doesn't exist yet (the `ned newfile.txt` case) -- still
    // resolves to its would-be containing directory, not treated as if the
    // nonexistent path itself were a directory.
    REQUIRE(DetectProjectRoot(dir / "newfile.txt") == std::filesystem::absolute(dir));

    std::filesystem::remove_all(dir);
}
