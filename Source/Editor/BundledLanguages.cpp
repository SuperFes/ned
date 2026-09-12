#include "BundledLanguages.h"

#include <algorithm>
#include <stdexcept>

#include "LanguageFiles.h"
#include "LanguageParse.h"
#include "Languages/Escapes.h"

namespace ned::editor {

namespace {

    // Every embedded <name>/language.janet, parsed and query-discovered.
    // Built once; a parse error here is a build regression in a bundled
    // definition, and throwing (which aborts at first use) beats shipping a
    // silently missing language.
    std::vector<LanguageDefinition> Build() {
        languages::RegisterBundledEscapes();
        std::vector<LanguageDefinition> out;
        for (const EmbeddedLanguageFile& file : EmbeddedLanguageFiles()) {
            const std::string_view     path    = file.path;
            constexpr std::string_view kSuffix = "/language.janet";
            if (!path.ends_with(kSuffix)) {
                continue;
            }
            const std::string_view directory = path.substr(0, path.size() - kSuffix.size());
            if (directory.find('/') != std::string_view::npos) {
                throw std::runtime_error("language.janet nested too deep: " + std::string(path));
            }
            LanguageDefinition definition = ParseLanguageDefinition(directory, file.content);
            DiscoverQueryFiles(definition,
                               [](std::string_view candidate) { return FindEmbeddedLanguageFile(candidate).has_value(); });
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
