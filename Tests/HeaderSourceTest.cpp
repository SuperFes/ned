#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/HeaderSource.h"

using ned::editor::headersource::FindCounterpart;
using ned::editor::headersource::IsHeaderExtension;
using ned::editor::headersource::IsSourceExtension;

namespace {

void Touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "";
}

// RAII scratch directory, unique per test case (Catch2's own test name is
// not filesystem-safe, so this just uses a fixed subdirectory per TEST_CASE
// body and cleans it up both before and after -- mirrors LinkTest.cpp's own
// ResolveFileLink tests' ad hoc temp-directory pattern).
struct ScratchDir {
    std::filesystem::path root;

    explicit ScratchDir(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_headersource_test_" + name)) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~ScratchDir() {
        std::filesystem::remove_all(root);
    }
    ScratchDir(const ScratchDir&)            = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
};

} // namespace

TEST_CASE("IsHeaderExtension/IsSourceExtension classify known extensions case-insensitively", "[HeaderSource]") {
    CHECK(IsHeaderExtension(".h"));
    CHECK(IsHeaderExtension(".Hpp"));
    CHECK(IsHeaderExtension(".HXX"));
    CHECK_FALSE(IsHeaderExtension(".cpp"));

    CHECK(IsSourceExtension(".cpp"));
    CHECK(IsSourceExtension(".CC"));
    CHECK(IsSourceExtension(".mm"));
    CHECK_FALSE(IsSourceExtension(".h"));

    CHECK_FALSE(IsHeaderExtension(".txt"));
    CHECK_FALSE(IsSourceExtension(".txt"));
}

TEST_CASE("FindCounterpart finds a source file for a header in the same directory", "[HeaderSource]") {
    const ScratchDir dir("same_dir");
    Touch(dir.root / "widget.h");
    Touch(dir.root / "widget.cpp");

    const auto found = FindCounterpart(dir.root / "widget.h");
    REQUIRE(found.has_value());
    CHECK(*found == dir.root / "widget.cpp");
}

TEST_CASE("FindCounterpart finds a header for a source file in the same directory", "[HeaderSource]") {
    const ScratchDir dir("same_dir_reverse");
    Touch(dir.root / "widget.h");
    Touch(dir.root / "widget.cpp");

    const auto found = FindCounterpart(dir.root / "widget.cpp");
    REQUIRE(found.has_value());
    CHECK(*found == dir.root / "widget.h");
}

TEST_CASE("FindCounterpart finds a header across a src/include sibling-directory swap", "[HeaderSource]") {
    const ScratchDir dir("sibling_swap");
    Touch(dir.root / "src" / "widget.cpp");
    Touch(dir.root / "include" / "widget.h");

    const auto found = FindCounterpart(dir.root / "src" / "widget.cpp");
    REQUIRE(found.has_value());
    CHECK(*found == dir.root / "include" / "widget.h");
}

TEST_CASE("FindCounterpart finds a source file across an include/src sibling-directory swap", "[HeaderSource]") {
    const ScratchDir dir("sibling_swap_reverse");
    Touch(dir.root / "src" / "widget.cpp");
    Touch(dir.root / "include" / "widget.h");

    const auto found = FindCounterpart(dir.root / "include" / "widget.h");
    REQUIRE(found.has_value());
    CHECK(*found == dir.root / "src" / "widget.cpp");
}

TEST_CASE("FindCounterpart returns nullopt when no counterpart exists on disk", "[HeaderSource]") {
    const ScratchDir dir("no_counterpart");
    Touch(dir.root / "widget.h");

    CHECK_FALSE(FindCounterpart(dir.root / "widget.h").has_value());
}

TEST_CASE("FindCounterpart returns nullopt for a path with no header/source extension", "[HeaderSource]") {
    const ScratchDir dir("not_cpp");
    Touch(dir.root / "notes.txt");
    Touch(dir.root / "notes.h"); // even with a plausible counterpart sitting right there

    CHECK_FALSE(FindCounterpart(dir.root / "notes.txt").has_value());
}
