#include "ModeOverrides.h"

#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "TreeSitter/DynamicGrammar.h"

namespace ned::editor {

namespace {

    std::mutex                                   g_mutex;
    std::unordered_map<std::string, Mode>        g_dynamicModes;
    std::unordered_map<std::string, std::string> g_extensionOverrides;
    std::unordered_map<std::string, std::string> g_filenameOverrides;

    // The bundled *Mode() functions' own names (Mode.cpp), so ModeByName can
    // resolve one the same way it resolves a dynamically-registered name --
    // a plain factory-function table, not anything fancier, since the set
    // is small and fixed at compile time.
    const std::unordered_map<std::string, std::function<Mode()>>& BundledModeFactories() {
        static const std::unordered_map<std::string, std::function<Mode()>> table = {
            {"fundamental-mode", FundamentalMode},
            {"janet-mode", JanetMode},
            {"json-mode", JsonMode},
            {"c-mode", CMode},
            {"cpp-mode", CppMode},
            {"php-mode", PhpMode},
            {"javascript-mode", JavaScriptMode},
            {"typescript-mode", TypeScriptMode},
            {"tsx-mode", TsxMode},
            {"html-mode", HtmlMode},
            {"css-mode", CssMode},
            {"python-mode", PythonMode},
            {"bash-mode", BashMode},
            {"markdown-mode", MarkdownMode},
        };
        return table;
    }

    std::string StripLeadingDot(std::string_view extension) {
        if (!extension.empty() && extension.front() == '.') {
            extension.remove_prefix(1);
        }
        return std::string(extension);
    }

    std::string ReadFileOrThrow(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("ned: failed to open tree-sitter query file '" + path.string() + "'");
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

} // namespace

void RegisterDynamicMode(const std::string& name, const std::filesystem::path& libraryPath,
                         const std::filesystem::path& queryPath) {
    const treesitter::Language language    = treesitter::LoadDynamicLanguage(libraryPath, name);
    const std::string          querySource = ReadFileOrThrow(queryPath);
    Mode                       mode        = TreeSitterModeFromLanguage(name, language, querySource);

    const std::lock_guard lock(g_mutex);
    g_dynamicModes.insert_or_assign(name, std::move(mode));
}

std::optional<Mode> ModeByName(const std::string& name) {
    {
        const std::lock_guard lock(g_mutex);
        if (const auto it = g_dynamicModes.find(name); it != g_dynamicModes.end()) {
            return it->second;
        }
    }
    const auto& factories = BundledModeFactories();
    if (const auto it = factories.find(name); it != factories.end()) {
        return it->second();
    }
    return std::nullopt;
}

void SetModeForExtension(const std::string& extension, const std::string& modeName) {
    const std::lock_guard lock(g_mutex);
    g_extensionOverrides.insert_or_assign(StripLeadingDot(extension), modeName);
}

void SetModeForFilename(const std::string& filename, const std::string& modeName) {
    const std::lock_guard lock(g_mutex);
    g_filenameOverrides.insert_or_assign(filename, modeName);
}

std::optional<Mode> ModeForFileOverride(const std::filesystem::path& path) {
    std::optional<std::string> modeName;
    {
        const std::lock_guard lock(g_mutex);
        if (const auto it = g_filenameOverrides.find(path.filename().string()); it != g_filenameOverrides.end()) {
            modeName = it->second;
        }
        else if (const auto extIt = g_extensionOverrides.find(StripLeadingDot(path.extension().string()));
                 extIt != g_extensionOverrides.end()) {
            modeName = extIt->second;
        }
    }
    if (!modeName) {
        return std::nullopt;
    }
    return ModeByName(*modeName);
}

} // namespace ned::editor
