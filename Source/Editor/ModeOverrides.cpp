#include "ModeOverrides.h"

#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "BundledLanguages.h"
#include "LanguageDefinition.h"
#include "LanguageFiles.h"
#include "Text/Buffer.h"
#include "TreeSitter/DynamicGrammar.h"

namespace ned::editor {

namespace {

    // dynamic-mode-thread-safety fix (found live via a SIGSEGV coredump): a
    // dynamically-registered mode used to be built *once* here and handed
    // out by ModeByName as copies of that single
    // Mode -- but Mode's closures capture their tree-sitter Parser/
    // IncrementalParseCache by shared_ptr (see TreeSitterModeFromLanguage's
    // own comment), so every copy aliased the exact same non-thread-safe
    // TSParser. ModePrewarmer's background thread and BufferView::Paint's
    // main-thread highlight both resolve the same dynamic mode name and
    // both call into it concurrently -- confirmed via gdb against a real
    // coredump: two threads inside ts_parser_parse on the identical Parser
    // object, corrupting tree-sitter's internal stack. Bundled modes never
    // had this bug because BundledModeByName() below builds its definition
    // fresh on every lookup, building a brand-new Parser each
    // time. Storing ingredients instead of a built Mode and rebuilding via
    // TreeSitterModeFromLanguage on every ModeByName call restores that
    // same "fresh Parser per call" contract for the dynamic case too.
    struct DynamicModeEntry {
        treesitter::Language language;
        QueryFiles           files; // absolute paths under the registered queries directory
    };
    std::mutex                                        g_mutex;
    std::unordered_map<std::string, DynamicModeEntry> g_dynamicModes;
    // RegisterMode's own registry -- a caller there hands over an already-
    // built, arbitrary Mode (not necessarily tree-sitter-backed at all;
    // Commands.cpp's "vcs-commit-message-mode" is its only caller today), so
    // there are no ingredients to rebuild from the way g_dynamicModes now
    // does. Kept as a separate map (rather than reusing g_dynamicModes'
    // now-different value type) since the two registration APIs have
    // genuinely different contracts.
    std::unordered_map<std::string, Mode>        g_registeredModes;
    std::unordered_map<std::string, std::string> g_extensionOverrides;
    std::unordered_map<std::string, std::string> g_filenameOverrides;
    // per-buffer-mode-cache follow-up: see CachedModeForBuffer's own doc
    // comment in the header. Keyed by raw Buffer* -- only ever compared for
    // identity, never dereferenced, so an entry outliving its buffer briefly
    // (between close and ClearModeCacheFor running) is harmless as long as
    // it's gone before any other buffer could reuse the same address; the
    // WindowManager close funnel guarantees that.
    std::unordered_map<const text::Buffer*, Mode> g_modeCache;

    // The bundled definitions' extensions -> mode name, keyed with the
    // leading dot (std::filesystem::path::extension()'s own form). Derived
    // from BundledLanguages() so a language claims its files in exactly one
    // place.
    const std::unordered_map<std::string, std::string>& BundledExtensionTable() {
        static const std::unordered_map<std::string, std::string> table = [] {
            std::unordered_map<std::string, std::string> built;
            for (const LanguageDefinition& definition : BundledLanguages()) {
                for (const std::string& extension : definition.extensions) {
                    built.emplace(extension, ModeNameFor(definition));
                }
            }
            return built;
        }();
        return table;
    }

    const std::unordered_map<std::string, std::string>& BundledFilenameTable() {
        static const std::unordered_map<std::string, std::string> table = [] {
            std::unordered_map<std::string, std::string> built;
            for (const LanguageDefinition& definition : BundledLanguages()) {
                for (const std::string& filename : definition.filenames) {
                    built.emplace(filename, ModeNameFor(definition));
                }
            }
            return built;
        }();
        return table;
    }

    // A bundled mode is its definition built fresh on every lookup -- a new
    // Parser each time, which is what keeps two threads (ModePrewarmer's
    // background build, BufferView::Paint's main-thread highlight) from ever
    // sharing one non-thread-safe TSParser; see DynamicModeEntry above for
    // the coredump that rule came from.
    std::optional<Mode> BundledModeByName(const std::string& modeName) {
        for (const LanguageDefinition& definition : BundledLanguages()) {
            if (ModeNameFor(definition) == modeName) {
                return ModeFromDefinition(definition);
            }
        }
        return std::nullopt;
    }

    std::string StripLeadingDot(std::string_view extension) {
        if (!extension.empty() && extension.front() == '.') {
            extension.remove_prefix(1);
        }
        return std::string(extension);
    }

    // register-language-grammar-directory-scan follow-up: the conventional
    // basename for a query kind, if the directory has it -- ned's own Janet
    // spelling first, then tree-sitter's (what a system install under
    // /usr/share/tree-sitter/queries/<lang>/ ships); both read through
    // Editor/QueryData.h. An absent file just means that capability is
    // unavailable for this grammar; a directory that doesn't exist at all
    // scans every kind as absent rather than throwing.
    std::vector<std::string> QueryFileIfPresent(const std::filesystem::path& queriesDir, const char* kind) {
        if (queriesDir.empty()) {
            return {};
        }
        for (const char* extension : {".janet", ".scm"}) {
            const std::filesystem::path path = queriesDir / (std::string(kind) + extension);
            if (std::filesystem::exists(path)) {
                return {std::filesystem::absolute(path).string()};
            }
        }
        return {};
    }

} // namespace

void RegisterDynamicMode(const std::string& name, const std::filesystem::path& libraryPath,
                         const std::filesystem::path& queriesDir) {
    const treesitter::Language language = treesitter::LoadDynamicLanguage(libraryPath, name);
    QueryFiles                 files{.highlights = QueryFileIfPresent(queriesDir, "highlights"),
                                     .folds      = QueryFileIfPresent(queriesDir, "folds"),
                                     .imports    = QueryFileIfPresent(queriesDir, "imports"),
                                     .tags       = QueryFileIfPresent(queriesDir, "tags"),
                                     .tests      = QueryFileIfPresent(queriesDir, "tests"),
                                     .indents    = QueryFileIfPresent(queriesDir, "indents"),
                                     .locals     = QueryFileIfPresent(queriesDir, "locals"),
                                     .injections = QueryFileIfPresent(queriesDir, "injections")};

    const std::lock_guard lock(g_mutex);
    g_dynamicModes.insert_or_assign(name, DynamicModeEntry{.language = language, .files = std::move(files)});
    // A re-registration under a name some already-cached buffer resolved to
    // would otherwise never take effect for it -- see g_modeCache's own
    // comment. Registration is rare (init.janet load time, or an
    // interactive re-eval), so a wholesale flush here is simpler and cheap
    // enough versus tracking which cached buffers actually used this name.
    g_modeCache.clear();
}

void RegisterMode(const std::string& name, Mode mode) {
    const std::lock_guard lock(g_mutex);
    g_registeredModes.insert_or_assign(name, std::move(mode));
    g_modeCache.clear();
}

std::optional<Mode> ModeByName(const std::string& name) {
    // Copied out under g_mutex, then built/returned with it released --
    // TreeSitterModeFromLanguage compiles tree-sitter queries, real work
    // that shouldn't happen while holding a mutex every other mode
    // lookup/registration also takes (the same reason the bundled-factory
    // branch below already runs outside the lock).
    std::optional<DynamicModeEntry> dynamicEntry;
    {
        const std::lock_guard lock(g_mutex);
        // Rebuilt fresh on every lookup -- see DynamicModeEntry's own
        // comment on why a cached, pre-built Mode isn't safe to hand out as
        // copies here (a real, coredump-confirmed SIGSEGV: two threads
        // sharing one non-thread-safe tree-sitter Parser).
        if (const auto it = g_dynamicModes.find(name); it != g_dynamicModes.end()) {
            dynamicEntry = it->second;
        }
        else if (const auto regIt = g_registeredModes.find(name); regIt != g_registeredModes.end()) {
            return regIt->second;
        }
    }
    if (dynamicEntry) {
        // Kept on the registered name as-is (not "<name>-mode"), which is
        // what ned/set-mode-for-extension callers already hand back.
        const QueryFiles& f          = dynamicEntry->files;
        const QueryText   highlights = CompileQueryFiles(f.highlights), folds = CompileQueryFiles(f.folds),
                          imports = CompileQueryFiles(f.imports), tags = CompileQueryFiles(f.tags),
                          tests = CompileQueryFiles(f.tests), indents = CompileQueryFiles(f.indents),
                          locals = CompileQueryFiles(f.locals), injections = CompileQueryFiles(f.injections);
        return TreeSitterModeFromLanguage(name, dynamicEntry->language,
                                          {.highlights = highlights.text,
                                           .folds      = folds.text,
                                           .imports    = imports.text,
                                           .tags       = tags.text,
                                           .tests      = tests.text,
                                           .indents    = indents.text,
                                           .locals     = locals.text,
                                           .injections = injections.text});
    }
    return BundledModeByName(name);
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
    const auto& filenames = BundledFilenameTable();
    if (const auto it = filenames.find(path.filename().string()); it != filenames.end()) {
        if (auto mode = ModeByName(it->second); mode) {
            return std::move(*mode);
        }
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
    Mode                  mode = ModeForBuffer(buffer);
    const std::lock_guard lock(g_mutex);
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
