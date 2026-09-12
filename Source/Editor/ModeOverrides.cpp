#include "ModeOverrides.h"

#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "BundledLanguages.h"
#include "LanguageDefinition.h"
#include "LanguageRegistry.h"
#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    std::mutex g_mutex;
    // RegisterMode's registry -- a caller hands over an already-built,
    // arbitrary Mode (not necessarily tree-sitter-backed at all;
    // Commands.cpp's "vcs-commit-message-mode" is its only caller today).
    // Everything grammar-shaped goes through LanguageRegistry.h instead,
    // where it stays a DEFINITION and is rebuilt fresh per lookup -- a
    // cached, pre-built Mode handed out as copies aliased one
    // non-thread-safe TSParser between ModePrewarmer's background build and
    // Paint's main-thread highlight, a real coredump-confirmed SIGSEGV.
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

    // A definition-backed mode is built fresh on every lookup -- a new
    // Parser each time, which is what keeps two threads (ModePrewarmer's
    // background build, BufferView::Paint's main-thread highlight) from ever
    // sharing one non-thread-safe TSParser (the coredump documented at
    // g_registeredModes above). Registered languages shadow bundled ones.
    std::optional<Mode> DefinitionModeByName(const std::string& modeName) {
        constexpr std::string_view kSuffix = "-mode";
        if (modeName.size() <= kSuffix.size() || !modeName.ends_with(kSuffix)) {
            return std::nullopt;
        }
        const std::string key = modeName.substr(0, modeName.size() - kSuffix.size());
        if (std::optional<RegisteredLanguage> registered = FindRegisteredLanguage(key)) {
            if (registered->language.has_value()) {
                return ModeFromDefinition(registered->definition, *registered->language);
            }
            return ModeFromDefinition(registered->definition);
        }
        if (const LanguageDefinition* bundled = BundledLanguage(key)) {
            return ModeFromDefinition(*bundled);
        }
        return std::nullopt;
    }

    // The registered set's own file claims, checked before the bundled
    // tables so a user language can take an extension over a bundled one.
    std::optional<std::string> RegisteredModeNameForPath(const std::filesystem::path& path) {
        const std::string          filename  = path.filename().string();
        const std::string          extension = path.extension().string();
        std::optional<std::string> byExtension;
        for (const RegisteredLanguage& registered : RegisteredLanguages()) {
            for (const std::string& candidate : registered.definition.filenames) {
                if (candidate == filename) {
                    return ModeNameFor(registered.definition);
                }
            }
            if (!byExtension) {
                for (const std::string& candidate : registered.definition.extensions) {
                    if (candidate == extension) {
                        byExtension = ModeNameFor(registered.definition);
                    }
                }
            }
        }
        return byExtension;
    }

    std::string StripLeadingDot(std::string_view extension) {
        if (!extension.empty() && extension.front() == '.') {
            extension.remove_prefix(1);
        }
        return std::string(extension);
    }

} // namespace

void ClearAllModeCaches() {
    const std::lock_guard lock(g_mutex);
    g_modeCache.clear();
}

void RegisterMode(const std::string& name, Mode mode) {
    const std::lock_guard lock(g_mutex);
    g_registeredModes.insert_or_assign(name, std::move(mode));
    g_modeCache.clear();
}

std::optional<Mode> ModeByName(const std::string& name) {
    {
        const std::lock_guard lock(g_mutex);
        if (const auto regIt = g_registeredModes.find(name); regIt != g_registeredModes.end()) {
            return regIt->second;
        }
    }
    // Outside the lock: building a definition-backed mode compiles
    // tree-sitter queries, real work that shouldn't happen while holding a
    // mutex every other lookup/registration also takes.
    return DefinitionModeByName(name);
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
    if (const std::optional<std::string> registered = RegisteredModeNameForPath(path)) {
        if (auto mode = ModeByName(*registered); mode) {
            return std::move(*mode);
        }
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
