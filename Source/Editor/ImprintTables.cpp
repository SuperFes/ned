#include "Editor/ImprintTables.h"

#include <mutex>

#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/GrammarImprint.h"
#include "Editor/LanguageFiles.h"
#include "Editor/LanguageRegistry.h"

namespace ned::editor::imprint {

namespace {

    // The grammar a definition parses with; empty for a grammarless one.
    std::string GrammarNameFor(std::string_view language) {
        if (const auto definition = FindLanguageDefinition(language)) {
            if (definition->grammarless || !definition->imprint)
                return {};
            if (!definition->grammar.empty())
                return definition->grammar;
        }
        return std::string(language);
    }

    std::map<std::string, DelimitedBody> Infer(std::string_view language) {
        const std::string grammarName = GrammarNameFor(language);
        if (grammarName.empty())
            return {};
        const std::string path = grammarName + "/grammar.janet";
        if (!BundledLanguageFileExists(path))
            return {};
        try {
            return grammar::InferDelimitedBodies(grammar::compile::ParseGrammarJanet(ReadLanguageFile(path)));
        }
        catch (const grammar::compile::GrammarFileError&) {
            return {};
        }
    }

    std::mutex                                                  g_mutex;
    std::map<std::string, std::map<std::string, DelimitedBody>> g_tables;

} // namespace

const std::map<std::string, DelimitedBody>& TableFor(std::string_view language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (const auto it = g_tables.find(std::string(language)); it != g_tables.end())
        return it->second;
    return g_tables.emplace(std::string(language), Infer(language)).first->second;
}

} // namespace ned::editor::imprint
