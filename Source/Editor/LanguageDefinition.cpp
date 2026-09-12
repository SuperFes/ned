#include "LanguageDefinition.h"

#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "AutoPair.h"
#include "Key.h"
#include "LanguageFiles.h"
#include "TreeSitter/Languages.h"
#include "TreeSitter/Query.h"

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

namespace {

    // The eight kinds, compiled; kept alive for the duration of the build
    // (TreeSitterModeFromLanguage retains none of the text).
    struct CompiledQueries {
        QueryText highlights, folds, imports, tags, tests, indents, locals, injections;

        [[nodiscard]] TreeSitterQuerySources Views() const {
            return {.highlights = highlights.text,
                    .folds      = folds.text,
                    .imports    = imports.text,
                    .tags       = tags.text,
                    .tests      = tests.text,
                    .indents    = indents.text,
                    .locals     = locals.text,
                    .injections = injections.text};
        }
    };

    CompiledQueries Compile(const QueryFiles& files) {
        return {.highlights = CompileQueryFiles(files.highlights),
                .folds      = CompileQueryFiles(files.folds),
                .imports    = CompileQueryFiles(files.imports),
                .tags       = CompileQueryFiles(files.tags),
                .tests      = CompileQueryFiles(files.tests),
                .indents    = CompileQueryFiles(files.indents),
                .locals     = CompileQueryFiles(files.locals),
                .injections = CompileQueryFiles(files.injections)};
    }

    // Which kind tree-sitter rejected, and where in which file: the generic
    // build compiles every kind in one go and its exception carries only a
    // byte offset, so on failure each kind is compiled again alone -- an
    // error path only, never paid on success.
    [[noreturn]] void RethrowLocated(const LanguageDefinition& definition, const treesitter::Language& language,
                                     const CompiledQueries& compiled, const treesitter::QueryCompileError& error) {
        for (const QueryText* text : {&compiled.highlights, &compiled.folds, &compiled.imports, &compiled.tags, &compiled.tests,
                                      &compiled.indents, &compiled.locals, &compiled.injections}) {
            if (text->text.empty()) {
                continue;
            }
            try {
                treesitter::Query probe(language, text->text);
            }
            catch (const treesitter::QueryCompileError& kindError) {
                throw std::runtime_error("language '" + definition.name + "': " + text->Locate(kindError.Offset()) +
                                         ": tree-sitter query error (" + std::string(kindError.Kind()) + ")");
            }
        }
        throw std::runtime_error("language '" + definition.name + "': " + error.what());
    }

} // namespace

Mode ModeFromDefinition(const LanguageDefinition& definition, const treesitter::Language& language) {
    const CompiledQueries compiled = Compile(definition.queries);
    ModeBuildContext      context;
    try {
        Mode mode = TreeSitterModeFromLanguage(ModeNameFor(definition), language, compiled.Views(), &context);
        return Finish(std::move(mode), definition, context);
    }
    catch (const treesitter::QueryCompileError& error) {
        RethrowLocated(definition, language, compiled, error);
    }
}

} // namespace ned::editor
