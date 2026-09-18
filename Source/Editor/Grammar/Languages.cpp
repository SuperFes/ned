#include "Languages.h"

#include <filesystem>
#include <string>

#include "Editor/Grammar/LanguagePackage.h"
#include "Editor/LanguageFiles.h"

namespace ned::editor::grammar {

std::optional<Language> LanguageByName(std::string_view name) {
    const std::filesystem::path directory = BundledLanguagesRoot() / std::string(name);
    if (!std::filesystem::exists(directory / "tables") && !std::filesystem::exists(directory / "grammar.janet"))
        return std::nullopt;
    return LoadLanguagePackage(directory, PackageScanner{.name = std::string(name), .library = {}});
}

} // namespace ned::editor::grammar
