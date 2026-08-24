#include "ModeOverrides.h"

#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "Text/Buffer.h"
#include "TreeSitter/DynamicGrammar.h"

namespace ned::editor {

namespace {

    std::mutex                                        g_mutex;
    std::unordered_map<std::string, Mode>             g_dynamicModes;
    std::unordered_map<std::string, std::string>      g_extensionOverrides;
    std::unordered_map<std::string, std::string>      g_filenameOverrides;
    // per-buffer-mode-cache follow-up: see CachedModeForBuffer's own doc
    // comment in the header. Keyed by raw Buffer* -- only ever compared for
    // identity, never dereferenced, so an entry outliving its buffer briefly
    // (between close and ClearModeCacheFor running) is harmless as long as
    // it's gone before any other buffer could reuse the same address; the
    // WindowManager close funnel guarantees that.
    std::unordered_map<const text::Buffer*, Mode> g_modeCache;

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
            {"fish-mode", FishMode},
            {"yaml-mode", YamlMode},
            {"toml-mode", TomlMode},
            {"clojure-mode", ClojureMode},
            {"jank-mode", JankMode},
            {"markdown-mode", MarkdownMode},
            {"org-mode", OrgMode},
        };
        return table;
    }

    std::string StripLeadingDot(std::string_view extension) {
        if (!extension.empty() && extension.front() == '.') {
            extension.remove_prefix(1);
        }
        return std::string(extension);
    }

    // per-buffer-mode follow-up. The bundled extension -> mode-name table,
    // moved here from main.cpp's own local ModeForPath so both startup and
    // BufferView's per-buffer resync path share one copy. Keyed by
    // extension with its leading dot (matches std::filesystem::path::
    // extension()'s own form directly, no StripLeadingDot needed here).
    const std::unordered_map<std::string, std::string>& BundledExtensionTable() {
        static const std::unordered_map<std::string, std::string> table = {
            {".janet", "janet-mode"},         {".json", "json-mode"},
            {".c", "c-mode"},                 {".h", "c-mode"},
            {".cpp", "cpp-mode"},             {".cc", "cpp-mode"},
            {".cxx", "cpp-mode"},             {".hpp", "cpp-mode"},
            {".hh", "cpp-mode"},              {".php", "php-mode"},
            {".phtml", "php-mode"},           {".js", "javascript-mode"},
            {".mjs", "javascript-mode"},      {".cjs", "javascript-mode"},
            {".ts", "typescript-mode"},       {".mts", "typescript-mode"},
            {".cts", "typescript-mode"},      {".tsx", "tsx-mode"},
            {".html", "html-mode"},           {".htm", "html-mode"},
            {".css", "css-mode"},             {".py", "python-mode"},
            {".pyw", "python-mode"},          {".sh", "bash-mode"},
            {".bash", "bash-mode"},           {".yaml", "yaml-mode"},
            {".yml", "yaml-mode"},            {".toml", "toml-mode"},
            {".fish", "fish-mode"},
            {".md", "markdown-mode"},
            {".markdown", "markdown-mode"},   {".org", "org-mode"},
            // .edn is data, not code, but it's read with Clojure's own reader
            // syntax -- same reasoning as .json -> json-mode. .bb is babashka,
            // a Clojure dialect like jank but with no extra syntax of its own.
            {".clj", "clojure-mode"},         {".cljs", "clojure-mode"},
            {".cljc", "clojure-mode"},        {".edn", "clojure-mode"},
            {".bb", "clojure-mode"},          {".jank", "jank-mode"},
        };
        return table;
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
                         const std::filesystem::path& queryPath, const std::filesystem::path& foldQueryPath,
                         const std::filesystem::path& importQueryPath) {
    const treesitter::Language language          = treesitter::LoadDynamicLanguage(libraryPath, name);
    const std::string          querySource       = queryPath.empty() ? std::string() : ReadFileOrThrow(queryPath);
    const std::string          foldQuerySource   = foldQueryPath.empty() ? std::string() : ReadFileOrThrow(foldQueryPath);
    const std::string          importQuerySource = importQueryPath.empty() ? std::string() : ReadFileOrThrow(importQueryPath);
    Mode mode = TreeSitterModeFromLanguage(name, language, querySource, foldQuerySource, importQuerySource);

    const std::lock_guard lock(g_mutex);
    g_dynamicModes.insert_or_assign(name, std::move(mode));
    // A re-registration under a name some already-cached buffer resolved to
    // would otherwise never take effect for it -- see g_modeCache's own
    // comment. Registration is rare (init.janet load time, or an
    // interactive re-eval), so a wholesale flush here is simpler and cheap
    // enough versus tracking which cached buffers actually used this name.
    g_modeCache.clear();
}

void RegisterMode(const std::string& name, Mode mode) {
    const std::lock_guard lock(g_mutex);
    g_dynamicModes.insert_or_assign(name, std::move(mode));
    g_modeCache.clear();
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
    g_modeCache.clear(); // see RegisterMode's own comment on why
}

void SetModeForFilename(const std::string& filename, const std::string& modeName) {
    const std::lock_guard lock(g_mutex);
    g_filenameOverrides.insert_or_assign(filename, modeName);
    g_modeCache.clear(); // see RegisterMode's own comment on why
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

Mode ModeForPath(const std::filesystem::path& path) {
    if (auto overrideMode = ModeForFileOverride(path); overrideMode) {
        return std::move(*overrideMode);
    }
    const auto& table = BundledExtensionTable();
    if (const auto it = table.find(path.extension().string()); it != table.end()) {
        if (auto mode = ModeByName(it->second); mode) {
            return std::move(*mode);
        }
    }
    return FundamentalMode();
}

Mode ModeForBuffer(const text::Buffer& buffer) {
    if (buffer.Path()) {
        return ModeForPath(*buffer.Path());
    }
    return FundamentalMode();
}

Mode CachedModeForBuffer(const text::Buffer& buffer) {
    {
        const std::lock_guard lock(g_mutex);
        if (const auto it = g_modeCache.find(&buffer); it != g_modeCache.end()) {
            return it->second;
        }
    }
    // Built with g_mutex released -- ModeForBuffer (via ModeForPath/
    // ModeForFileOverride/ModeByName) takes it itself, internally, and
    // g_mutex isn't recursive. Main-thread-only per this function's own
    // header comment, so there's no real race to build the same buffer's
    // Mode twice; insert_or_assign rather than emplace just in case, so a
    // hypothetical double-build overwrites rather than leaving two entries.
    Mode                   mode = ModeForBuffer(buffer);
    const std::lock_guard  lock(g_mutex);
    return g_modeCache.insert_or_assign(&buffer, std::move(mode)).first->second;
}

void ClearModeCacheFor(const text::Buffer& buffer) {
    const std::lock_guard lock(g_mutex);
    g_modeCache.erase(&buffer);
}

void InsertPrewarmedMode(const text::Buffer& buffer, Mode mode) {
    const std::lock_guard lock(g_mutex);
    g_modeCache.try_emplace(&buffer, std::move(mode)); // no-op if already cached
}

} // namespace ned::editor
