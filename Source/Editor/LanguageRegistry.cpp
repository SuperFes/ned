#include "LanguageRegistry.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include "BundledLanguages.h"
#include "LanguageParse.h"
#include "ModeOverrides.h"
#include "TreeSitter/DynamicGrammar.h"

namespace ned::editor {

namespace {

    std::mutex                                g_mutex;
    std::map<std::string, RegisteredLanguage> g_languages;

    std::string ReadFileOrThrow(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("cannot read " + path.string());
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // A foreign queries directory (:queries-dir) fills kinds discovery left
    // empty -- <kind>.janet preferred over <kind>.scm, both readable by
    // Editor/QueryData.h.
    void DiscoverForeignQueries(LanguageDefinition& definition, const std::filesystem::path& queriesDir) {
        const auto fill = [&queriesDir](std::vector<std::string>& list, const char* kind) {
            if (!list.empty()) {
                return;
            }
            for (const char* extension : {".janet", ".scm"}) {
                const std::filesystem::path candidate = queriesDir / (std::string(kind) + extension);
                if (std::filesystem::exists(candidate)) {
                    list.push_back(std::filesystem::absolute(candidate).string());
                    return;
                }
            }
        };
        fill(definition.queries.highlights, "highlights");
        fill(definition.queries.folds, "folds");
        fill(definition.queries.imports, "imports");
        fill(definition.queries.tags, "tags");
        fill(definition.queries.tests, "tests");
        fill(definition.queries.indents, "indents");
        fill(definition.queries.locals, "locals");
        fill(definition.queries.injections, "injections");
    }

} // namespace

void LoadLanguageDirectory(const std::filesystem::path& directory) {
    const std::string name = directory.filename().string();
    if (name.empty()) {
        throw std::runtime_error("language directory has no name: " + directory.string());
    }
    const std::filesystem::path definitionPath = directory / "language.janet";
    LanguageDefinition          definition     = ParseLanguageDefinition(name, ReadFileOrThrow(definitionPath));

    // Discovery against the directory's own files -- absolute paths, so
    // CompileQueryFiles reads them from disk rather than the embedded table.
    const std::filesystem::path parent = std::filesystem::absolute(directory).parent_path();
    DiscoverQueryFiles(
        definition, [](std::string_view candidate) { return std::filesystem::exists(std::filesystem::path(candidate)); },
        parent.string() + "/");
    if (!definition.queriesDir.empty()) {
        DiscoverForeignQueries(definition, definition.queriesDir);
    }

    RegisteredLanguage registered{.definition = std::move(definition)};
    if (!registered.definition.grammarLibrary.empty()) {
        const std::string symbolName =
            registered.definition.grammar.empty() ? registered.definition.name : registered.definition.grammar;
        // Throws with a path-qualified message on a missing library/symbol;
        // the handle stays resident for the process lifetime
        // (TreeSitter/DynamicGrammar.h's own scope cut).
        registered.language = treesitter::LoadDynamicLanguage(registered.definition.grammarLibrary, symbolName);
    }
    RegisterLanguage(std::move(registered));
}

void RegisterLanguage(RegisteredLanguage language) {
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_languages.insert_or_assign(language.definition.name, std::move(language));
    }
    // A re-registration under a name some cached buffer's mode resolved to
    // must take effect for it -- same rule every mode-affecting override
    // already follows (see ModeOverrides.cpp's g_modeCache comment).
    ClearAllModeCaches();
}

std::vector<std::filesystem::path> LanguageDirectories(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    std::error_code                    ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, ec)) {
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc &&
            std::filesystem::is_regular_file(entry.path() / "language.janet", entryEc) && !entryEc) {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<RegisteredLanguage> FindRegisteredLanguage(std::string_view name) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_languages.find(std::string(name));
    if (it == g_languages.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<LanguageDefinition> FindLanguageDefinition(std::string_view name) {
    if (std::optional<RegisteredLanguage> registered = FindRegisteredLanguage(name)) {
        return std::move(registered->definition);
    }
    if (const LanguageDefinition* bundled = BundledLanguage(name)) {
        return *bundled;
    }
    return std::nullopt;
}

std::vector<RegisteredLanguage> RegisteredLanguages() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<RegisteredLanguage>   out;
    out.reserve(g_languages.size());
    for (const auto& [name, language] : g_languages) {
        out.push_back(language);
    }
    return out;
}

void ClearRegisteredLanguages() {
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_languages.clear();
    }
    ClearAllModeCaches();
}

} // namespace ned::editor
