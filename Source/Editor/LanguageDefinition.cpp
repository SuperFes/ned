#include "LanguageDefinition.h"

#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "AutoPair.h"
#include "Key.h"
#include "TreeSitter/Languages.h"

namespace ned::editor {

namespace {

    std::mutex                                  g_escapeMutex;
    std::unordered_map<std::string, ModeEscape> g_escapes;

    ModeEscape FindEscape(std::string_view name) {
        const std::lock_guard<std::mutex> lock(g_escapeMutex);
        const auto                        it = g_escapes.find(std::string(name));
        if (it == g_escapes.end()) {
            throw std::runtime_error("language definition names an unregistered escape: " + std::string(name));
        }
        return it->second;
    }

    void ApplyDefinition(Mode& mode, const LanguageDefinition& definition) {
        mode.lineCommentPrefix = definition.lineCommentPrefix;
        mode.wrapLines         = definition.wrapLines;
        mode.autoPairs         = definition.autoPairs == AutoPairSet::Lisp ? LispAutoPairs() : DefaultAutoPairs();
        for (const auto& [sequence, command] : definition.keymap) {
            mode.keymap.Bind(ParseKeySequence(sequence), command);
        }
        if (!definition.embeddedDocuments) {
            mode.embeddedRegions = EmbeddedRegionFunction();
        }
    }

    Mode Finish(Mode mode, const LanguageDefinition& definition, const ModeBuildContext& context) {
        ApplyDefinition(mode, definition);
        for (const std::string& name : definition.escapes) {
            FindEscape(name)(mode, definition, context);
        }
        return mode;
    }

} // namespace

std::string ModeNameFor(const LanguageDefinition& definition) {
    return definition.name + "-mode";
}

void RegisterModeEscape(std::string name, ModeEscape escape) {
    const std::lock_guard<std::mutex> lock(g_escapeMutex);
    g_escapes[std::move(name)] = std::move(escape);
}

bool HasModeEscape(std::string_view name) {
    const std::lock_guard<std::mutex> lock(g_escapeMutex);
    return g_escapes.contains(std::string(name));
}

Mode ModeFromDefinition(const LanguageDefinition& definition) {
    if (definition.grammarless) {
        Mode mode{.name = ModeNameFor(definition), .keymap = Keymap(), .highlight = HighlightFunction()};
        return Finish(std::move(mode), definition, ModeBuildContext{.languageKey = definition.name});
    }
    const std::string_view grammar  = definition.grammar.empty() ? std::string_view(definition.name) : definition.grammar;
    const auto             language = treesitter::LanguageByName(grammar);
    if (!language) {
        throw std::runtime_error("language definition '" + definition.name + "' names a grammar that is not bundled: " +
                                 std::string(grammar));
    }
    return ModeFromDefinition(definition, *language);
}

Mode ModeFromDefinition(const LanguageDefinition& definition, const treesitter::Language& language) {
    ModeBuildContext context;
    Mode             mode = TreeSitterModeFromLanguage(ModeNameFor(definition), language, definition.queries, &context);
    return Finish(std::move(mode), definition, context);
}

} // namespace ned::editor
