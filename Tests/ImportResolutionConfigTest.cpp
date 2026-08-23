#include <catch2/catch_test_macros.hpp>

#include "Editor/ImportResolutionConfig.h"
#include "Editor/ProjectSettings.h"

using ned::editor::DefaultImportResolutionConfig;
using ned::editor::ImportResolutionOverride;
using ned::editor::ProjectSettings;
using ned::editor::ResolveImportResolutionConfig;

TEST_CASE("DefaultImportResolutionConfig has bundled defaults for javascript/typescript/python", "[ImportResolutionConfig]") {
    const auto js = DefaultImportResolutionConfig("javascript");
    CHECK(js.searchPackageDirs);
    CHECK_FALSE(js.extensions.empty());
    CHECK_FALSE(js.indexBasenames.empty());

    const auto python = DefaultImportResolutionConfig("python");
    CHECK_FALSE(python.searchPackageDirs);
    REQUIRE(python.extensions.size() == 1);
    CHECK(python.extensions[0] == "py");
    REQUIRE(python.indexBasenames.size() == 1);
    CHECK(python.indexBasenames[0] == "__init__");
}

TEST_CASE("DefaultImportResolutionConfig returns an empty config for an unknown language", "[ImportResolutionConfig]") {
    const auto config = DefaultImportResolutionConfig("some-unregistered-language");
    CHECK(config.extensions.empty());
    CHECK(config.indexBasenames.empty());
    CHECK_FALSE(config.searchPackageDirs);
}

TEST_CASE("ResolveImportResolutionConfig returns the bundled default when no project override exists",
          "[ImportResolutionConfig]") {
    const ProjectSettings settings;
    const auto             config = ResolveImportResolutionConfig(settings, "python");
    CHECK(config.extensions == DefaultImportResolutionConfig("python").extensions);
}

TEST_CASE("ResolveImportResolutionConfig lets a project override replace the bundled extensions outright",
          "[ImportResolutionConfig]") {
    ProjectSettings settings;
    settings.importResolutionByLanguage["python"] = ImportResolutionOverride{.extensions = {"pyx"}};

    const auto config = ResolveImportResolutionConfig(settings, "python");
    REQUIRE(config.extensions.size() == 1);
    CHECK(config.extensions[0] == "pyx");
    // indexBasenames wasn't overridden -- still inherits the bundled default.
    REQUIRE(config.indexBasenames.size() == 1);
    CHECK(config.indexBasenames[0] == "__init__");
}

TEST_CASE("ResolveImportResolutionConfig lets a project override searchPackageDirs independently",
          "[ImportResolutionConfig]") {
    ProjectSettings settings;
    settings.importResolutionByLanguage["python"] = ImportResolutionOverride{.searchPackageDirs = true};

    const auto config = ResolveImportResolutionConfig(settings, "python");
    CHECK(config.searchPackageDirs);
    // extensions untouched.
    REQUIRE(config.extensions.size() == 1);
    CHECK(config.extensions[0] == "py");
}

TEST_CASE("ResolveImportResolutionConfig configures a language with no bundled default at all", "[ImportResolutionConfig]") {
    ProjectSettings settings;
    settings.importResolutionByLanguage["mylang"] =
        ImportResolutionOverride{.extensions = {"my"}, .searchPackageDirs = false};

    const auto config = ResolveImportResolutionConfig(settings, "mylang");
    REQUIRE(config.extensions.size() == 1);
    CHECK(config.extensions[0] == "my");
}
