#include "ImportResolutionConfig.h"

#include "Editor/BundledLanguages.h"
#include "Editor/Project/Settings.h"

namespace ned::editor {


ImportResolutionConfig DefaultImportResolutionConfig(const std::string& languageKey) {
    // From the language's own definition (language.janet's
    // :import-resolution); a language declaring none -- including every one
    // with no import query at all -- gets the default-constructed config.
    const LanguageDefinition* definition = BundledLanguage(languageKey);
    if (definition != nullptr && definition->importResolution.has_value()) {
        return *definition->importResolution;
    }
    return {};
}

ImportResolutionConfig ResolveImportResolutionConfig(const ProjectSettings& settings, const std::string& languageKey) {
    ImportResolutionConfig          config    = DefaultImportResolutionConfig(languageKey);
    const ImportResolutionOverride& override_ = ImportResolutionOverrideForLanguage(settings, languageKey);
    if (!override_.extensions.empty()) {
        config.extensions = override_.extensions;
    }
    if (!override_.indexBasenames.empty()) {
        config.indexBasenames = override_.indexBasenames;
    }
    if (override_.searchPackageDirs.has_value()) {
        config.searchPackageDirs = *override_.searchPackageDirs;
    }
    return config;
}

} // namespace ned::editor
