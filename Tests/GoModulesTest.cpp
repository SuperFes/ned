#include <catch2/catch_test_macros.hpp>

#include "Editor/GoModules.h"

using ned::editor::EscapeGoModulePath;
using ned::editor::ParseGoMod;

TEST_CASE("go.mod's module, requirements and replacements are read in both forms", "[GoModules]") {
    const auto file = ParseGoMod("module example.com/m // the app\n"
                                 "\n"
                                 "go 1.22\n"
                                 "\n"
                                 "require github.com/a/b v1.2.3\n"
                                 "require (\n"
                                 "\tgolang.org/x/text v0.14.0 // indirect\n"
                                 "\t\"github.com/c/d\" v0.1.0\n"
                                 ")\n"
                                 "replace github.com/a/b => ../b\n"
                                 "replace (\n"
                                 "\tgithub.com/c/d v0.1.0 => github.com/e/d v0.2.0\n"
                                 ")\n"
                                 "exclude github.com/x/y v1.0.0\n");
    CHECK(file.module == "example.com/m");
    REQUIRE(file.requirements.size() == 3);
    CHECK(file.requirements[0].path == "github.com/a/b");
    CHECK(file.requirements[0].version == "v1.2.3");
    CHECK(file.requirements[1].path == "golang.org/x/text");
    CHECK(file.requirements[2].path == "github.com/c/d");
    REQUIRE(file.replaces.size() == 2);
    CHECK(file.replaces[0].path == "github.com/a/b");
    CHECK(file.replaces[0].version.empty());
    CHECK(file.replaces[0].newPath == "../b");
    CHECK(file.replaces[0].newVersion.empty());
    CHECK(file.replaces[1].version == "v0.1.0");
    CHECK(file.replaces[1].newPath == "github.com/e/d");
    CHECK(file.replaces[1].newVersion == "v0.2.0");
}

TEST_CASE("A module path is case-escaped the way the module cache stores it", "[GoModules]") {
    CHECK(EscapeGoModulePath("github.com/BurntSushi/toml") == "github.com/!burnt!sushi/toml");
    CHECK(EscapeGoModulePath("golang.org/x/text") == "golang.org/x/text");
}
