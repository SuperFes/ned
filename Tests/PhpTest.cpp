#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Php.h"

using ned::editor::php::ResolvePsr4Namespace;

namespace {

// mirrors NodeModulesTest.cpp's own "real filesystem, cleaned up in the
// destructor" TempTree convention.
struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name = "ned-php-test") : root(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempTree() {
        std::filesystem::remove_all(root);
    }
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content;
}

} // namespace

TEST_CASE("ResolvePsr4Namespace resolves a simple prefix mapping", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload": {"psr-4": {"App\\": "src/"}}})");
    WriteFile(tree.root / "src" / "Models" / "User.php", "<?php\n");

    const auto resolved = ResolvePsr4Namespace("App\\Models\\User", tree.root);
    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::weakly_canonical(tree.root / "src" / "Models" / "User.php"));
}

TEST_CASE("ResolvePsr4Namespace strips a leading fully-qualified backslash", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload": {"psr-4": {"App\\": "src/"}}})");
    WriteFile(tree.root / "src" / "Foo.php", "<?php\n");

    const auto resolved = ResolvePsr4Namespace("\\App\\Foo", tree.root);
    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::weakly_canonical(tree.root / "src" / "Foo.php"));
}

TEST_CASE("ResolvePsr4Namespace prefers the longest matching prefix", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json",
              R"({"autoload": {"psr-4": {"App\\": "src/", "App\\Tests\\": "tests/"}}})");
    WriteFile(tree.root / "src" / "Tests" / "FooTest.php", "<?php\n");
    WriteFile(tree.root / "tests" / "FooTest.php", "<?php\n");

    const auto resolved = ResolvePsr4Namespace("App\\Tests\\FooTest", tree.root);
    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::weakly_canonical(tree.root / "tests" / "FooTest.php"));
}

TEST_CASE("ResolvePsr4Namespace tries each directory in a multi-value psr-4 entry", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload": {"psr-4": {"App\\": ["src/", "lib/"]}}})");
    WriteFile(tree.root / "lib" / "Foo.php", "<?php\n");

    const auto resolved = ResolvePsr4Namespace("App\\Foo", tree.root);
    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::weakly_canonical(tree.root / "lib" / "Foo.php"));
}

TEST_CASE("ResolvePsr4Namespace checks autoload-dev too", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload-dev": {"psr-4": {"App\\Tests\\": "tests/"}}})");
    WriteFile(tree.root / "tests" / "FooTest.php", "<?php\n");

    const auto resolved = ResolvePsr4Namespace("App\\Tests\\FooTest", tree.root);
    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::weakly_canonical(tree.root / "tests" / "FooTest.php"));
}

TEST_CASE("ResolvePsr4Namespace returns nullopt when no prefix matches", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload": {"psr-4": {"App\\": "src/"}}})");

    CHECK_FALSE(ResolvePsr4Namespace("Other\\Foo", tree.root).has_value());
}

TEST_CASE("ResolvePsr4Namespace returns nullopt when the mapped file doesn't exist", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", R"({"autoload": {"psr-4": {"App\\": "src/"}}})");

    CHECK_FALSE(ResolvePsr4Namespace("App\\Missing", tree.root).has_value());
}

TEST_CASE("ResolvePsr4Namespace returns nullopt when there is no composer.json", "[Php]") {
    TempTree tree;
    CHECK_FALSE(ResolvePsr4Namespace("App\\Foo", tree.root).has_value());
}

TEST_CASE("ResolvePsr4Namespace returns nullopt on malformed JSON", "[Php]") {
    TempTree tree;
    WriteFile(tree.root / "composer.json", "{not json");
    CHECK_FALSE(ResolvePsr4Namespace("App\\Foo", tree.root).has_value());
}
