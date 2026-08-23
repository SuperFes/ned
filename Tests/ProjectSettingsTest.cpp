#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/ProjectSettings.h"

using ned::editor::ImportResolutionOverrideForLanguage;
using ned::editor::IncludePathsForMode;
using ned::editor::LoadProjectSettings;
using ned::editor::LspInitializationOptionsForLanguage;
using ned::editor::ProjectSettings;

namespace {

std::filesystem::path MakeTempRoot(const std::string& name) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".ned");
    return root;
}

} // namespace

TEST_CASE("LoadProjectSettings returns empty when .ned/settings.json is absent", "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_absent");

    const ProjectSettings settings = LoadProjectSettings(root);
    CHECK(settings.includePathsByMode.empty());
    CHECK(IncludePathsForMode(settings, "cpp-mode").empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("LoadProjectSettings parses includePaths per mode, resolving relative entries against root",
          "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_parse");
    {
        std::ofstream file(root / ".ned" / "settings.json");
        file << R"({"includePaths": {
                        "cpp-mode": ["build/_deps/foo-src/include", "/usr/include"],
                        "php-mode": ["/usr/share/php"]
                    }})";
    }

    const ProjectSettings settings = LoadProjectSettings(root);

    const auto& cppPaths = IncludePathsForMode(settings, "cpp-mode");
    REQUIRE(cppPaths.size() == 2);
    CHECK(cppPaths[0] == root / "build/_deps/foo-src/include");
    CHECK(cppPaths[1] == "/usr/include");

    const auto& phpPaths = IncludePathsForMode(settings, "php-mode");
    REQUIRE(phpPaths.size() == 1);
    CHECK(phpPaths[0] == "/usr/share/php");

    // A mode with no configured entry never sees another language's paths.
    CHECK(IncludePathsForMode(settings, "python-mode").empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("LoadProjectSettings parses lspInitializationOptions per language, passed through verbatim",
          "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_lsp_init_options");
    {
        std::ofstream file(root / ".ned" / "settings.json");
        file << R"({"lspInitializationOptions": {
                        "php": {"bootstrapFiles": ["bootstrap.php"]}
                    }})";
    }

    const ProjectSettings settings = LoadProjectSettings(root);

    const nlohmann::json& phpOptions = LspInitializationOptionsForLanguage(settings, "php");
    REQUIRE(phpOptions.contains("bootstrapFiles"));
    CHECK(phpOptions["bootstrapFiles"] == nlohmann::json::array({"bootstrap.php"}));

    // A language with no configured entry gets an empty object, not an error.
    CHECK(LspInitializationOptionsForLanguage(settings, "cpp").empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("LoadProjectSettings parses lspWorkspaceConfiguration as a flat, language-agnostic tree",
          "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_lsp_workspace_config");
    {
        std::ofstream file(root / ".ned" / "settings.json");
        file << R"({"lspWorkspaceConfiguration": {
                        "phpactor": {"file_extensions": ["php"]},
                        "intelephense": {"environment": {"includePaths": ["/stubs"]}}
                    }})";
    }

    const ProjectSettings settings = LoadProjectSettings(root);

    REQUIRE(settings.lspWorkspaceConfiguration.contains("phpactor"));
    CHECK(settings.lspWorkspaceConfiguration["phpactor"]["file_extensions"] == nlohmann::json::array({"php"}));
    CHECK(settings.lspWorkspaceConfiguration["intelephense"]["environment"]["includePaths"] ==
          nlohmann::json::array({"/stubs"}));

    std::filesystem::remove_all(root);
}

TEST_CASE("LoadProjectSettings parses importResolution overrides per language", "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_import_resolution");
    {
        std::ofstream file(root / ".ned" / "settings.json");
        file << R"({"importResolution": {
                        "python": {"extensions": ["pyx"], "indexBasenames": ["__init__"], "searchPackageDirs": false},
                        "javascript": {"searchPackageDirs": true}
                    }})";
    }

    const ProjectSettings settings = LoadProjectSettings(root);

    const auto& python = ImportResolutionOverrideForLanguage(settings, "python");
    REQUIRE(python.extensions.size() == 1);
    CHECK(python.extensions[0] == "pyx");
    REQUIRE(python.indexBasenames.size() == 1);
    CHECK(python.indexBasenames[0] == "__init__");
    REQUIRE(python.searchPackageDirs.has_value());
    CHECK_FALSE(*python.searchPackageDirs);

    const auto& javascript = ImportResolutionOverrideForLanguage(settings, "javascript");
    CHECK(javascript.extensions.empty());
    REQUIRE(javascript.searchPackageDirs.has_value());
    CHECK(*javascript.searchPackageDirs);

    const auto& unconfigured = ImportResolutionOverrideForLanguage(settings, "rust");
    CHECK(unconfigured.extensions.empty());
    CHECK_FALSE(unconfigured.searchPackageDirs.has_value());

    std::filesystem::remove_all(root);
}

TEST_CASE("LoadProjectSettings returns empty on malformed JSON", "[ProjectSettings]") {
    const std::filesystem::path root = MakeTempRoot("ned_project_settings_test_malformed");
    {
        std::ofstream file(root / ".ned" / "settings.json");
        file << "not valid json {{{";
    }

    const ProjectSettings settings = LoadProjectSettings(root);
    CHECK(settings.includePathsByMode.empty());

    std::filesystem::remove_all(root);
}
