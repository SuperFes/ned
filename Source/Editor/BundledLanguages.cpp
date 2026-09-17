#include "BundledLanguages.h"

#include <algorithm>

#include "LanguageFiles.h"
#include "LanguageParse.h"
#include "LanguageRegistry.h"
#include "Languages/Escapes.h"

namespace ned::editor {

namespace {

    // Every <name>/language.janet under the bundled root, parsed and
    // query-discovered with paths relative to that root. Built once; a
    // parse error here is a build regression in a bundled definition, and
    // throwing (which aborts at first use) beats shipping a silently
    // missing language.
    std::vector<LanguageDefinition> Build() {
        languages::RegisterBundledEscapes();
        std::vector<LanguageDefinition> out;
        for (const std::filesystem::path& directory : LanguageDirectories(BundledLanguagesRoot())) {
            const std::string  name       = directory.filename().string();
            LanguageDefinition definition = ParseLanguageDefinition(name, ReadLanguageFile(name + "/language.janet"));
            DiscoverQueryFiles(definition, BundledLanguageFileExists);
            out.push_back(std::move(definition));
        }
        std::sort(out.begin(), out.end(),
                  [](const LanguageDefinition& a, const LanguageDefinition& b) { return a.name < b.name; });
        return out;
    }

} // namespace

const std::vector<LanguageDefinition>& BundledLanguages() {
    static const std::vector<LanguageDefinition> kLanguages = Build();
    return kLanguages;
}

const LanguageDefinition* BundledLanguage(std::string_view name) {
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.name == name) {
            return &definition;
        }
    }
    return nullptr;
}

} // namespace ned::editor
