#include "ModeOverrides.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <unordered_map>

#include "BundledLanguages.h"
#include "FileMagic.h"
#include "LanguageDefinition.h"
#include "LanguageRegistry.h"
#include "Modeline.h"
#include "Text/Buffer.h"
#include "Text/PerBufferMap.h"

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
    // See CachedModeForBuffer's doc comment in the header.
    text::PerBufferMap<Mode, const text::Buffer*> g_modeCache{&g_mutex};

    // set-mode's choice per buffer: outlives cache flushes (a language
    // registration clears g_modeCache), dropped with the buffer.
    text::PerBufferMap<std::string, const text::Buffer*> g_chosenModes{&g_mutex};

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

    // A `:filenames` entry holding a `/` claims a path by its trailing
    // components (".ssh/config"), where the basename alone is too generic.
    bool IsPathClaim(std::string_view filename) {
        return filename.find('/') != std::string_view::npos;
    }

    bool PathEndsWith(const std::filesystem::path& path, std::string_view claim) {
        const std::string generic = path.lexically_normal().generic_string();
        return generic.size() > claim.size() && generic.ends_with(claim) &&
               generic[generic.size() - claim.size() - 1] == '/';
    }

    const std::unordered_map<std::string, std::string>& BundledFilenameTable() {
        static const std::unordered_map<std::string, std::string> table = [] {
            std::unordered_map<std::string, std::string> built;
            for (const LanguageDefinition& definition : BundledLanguages()) {
                for (const std::string& filename : definition.filenames) {
                    if (!IsPathClaim(filename)) {
                        built.emplace(filename, ModeNameFor(definition));
                    }
                }
            }
            return built;
        }();
        return table;
    }

    const std::vector<std::pair<std::string, std::string>>& BundledPathClaims() {
        static const std::vector<std::pair<std::string, std::string>> claims = [] {
            std::vector<std::pair<std::string, std::string>> built;
            for (const LanguageDefinition& definition : BundledLanguages()) {
                for (const std::string& filename : definition.filenames) {
                    if (IsPathClaim(filename)) {
                        built.emplace_back(filename, ModeNameFor(definition));
                    }
                }
            }
            return built;
        }();
        return claims;
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
                if (IsPathClaim(candidate) ? PathEndsWith(path, candidate) : candidate == filename) {
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

namespace {

    // A content pattern compiled once per process; nullopt for one std::regex
    // rejects, which then never matches.
    const std::optional<std::regex>& CompiledContentPattern(const std::string& pattern) {
        static std::mutex                                                 mutex;
        static std::unordered_map<std::string, std::optional<std::regex>> cache;
        const std::lock_guard<std::mutex>                                 lock(mutex);
        auto                                                              it = cache.find(pattern);
        if (it == cache.end()) {
            std::optional<std::regex> compiled;
            try {
                compiled.emplace(pattern, std::regex::ECMAScript | std::regex::multiline);
            }
            catch (const std::regex_error&) {
            }
            it = cache.emplace(pattern, std::move(compiled)).first;
        }
        return it->second;
    }

    // Which language sharing `path`'s extension the file's first bytes say it
    // is: libmagic's MIME type first, then each sharer's content pattern.
    // nullopt leaves the extension with its owner -- also the answer for a
    // file that does not exist yet.
    std::optional<std::string> SharedExtensionModeName(const std::filesystem::path& path, std::string_view head) {
        const std::string               extension = path.extension().string();
        std::vector<LanguageDefinition> sharers;
        const auto                      consider = [&](const LanguageDefinition& definition) {
            if (std::ranges::find(definition.sharedExtensions, extension) != definition.sharedExtensions.end()) {
                sharers.push_back(definition);
            }
        };
        for (const RegisteredLanguage& registered : RegisteredLanguages()) {
            consider(registered.definition);
        }
        for (const LanguageDefinition& definition : BundledLanguages()) {
            consider(definition);
        }
        if (sharers.empty()) {
            return std::nullopt;
        }

        if (head.empty()) {
            return std::nullopt;
        }
        if (const std::optional<std::string> mime = MimeTypeOf(head)) {
            for (const LanguageDefinition& sharer : sharers) {
                if (std::ranges::find(sharer.mimeTypes, *mime) != sharer.mimeTypes.end()) {
                    return ModeNameFor(sharer);
                }
            }
        }
        for (const LanguageDefinition& sharer : sharers) {
            if (sharer.contentPattern.empty()) {
                continue;
            }
            if (const std::optional<std::regex>& pattern = CompiledContentPattern(sharer.contentPattern);
                pattern && std::regex_search(head.begin(), head.end(), *pattern)) {
                return ModeNameFor(sharer);
            }
        }
        return std::nullopt;
    }

    // What content-based resolution reads: the first 8 KiB, and the last
    // 4 KiB after an elision line when the file is longer -- enough for a
    // sniff and for a modeline at either end. Empty for a file not on disk.

} // namespace

Mode ModeForPath(const std::filesystem::path& path) {
    if (auto overrideMode = ModeForFileOverride(path); overrideMode) {
        return std::move(*overrideMode);
    }
    const std::string ends = ReadFileEnds(path);
    // A modeline in the file outranks everything but the user's own override.
    if (const std::optional<std::string> language = ParseModeline(ends).language) {
        if (auto mode = ModeByName(*language + "-mode"); mode) {
            return std::move(*mode);
        }
    }
    if (const std::optional<std::string> registered = RegisteredModeNameForPath(path)) {
        if (auto mode = ModeByName(*registered); mode) {
            return std::move(*mode);
        }
    }
    for (const auto& [claim, modeName] : BundledPathClaims()) {
        if (PathEndsWith(path, claim)) {
            if (auto mode = ModeByName(modeName); mode) {
                return std::move(*mode);
            }
        }
    }
    const auto& filenames = BundledFilenameTable();
    if (const auto it = filenames.find(path.filename().string()); it != filenames.end()) {
        if (auto mode = ModeByName(it->second); mode) {
            return std::move(*mode);
        }
    }
    if (const std::optional<std::string> shared = SharedExtensionModeName(path, std::string_view(ends).substr(0, 8192))) {
        if (auto mode = ModeByName(*shared); mode) {
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
    std::optional<std::string> chosen;
    {
        const std::lock_guard lock(g_mutex);
        if (const auto it = g_chosenModes.find(&buffer); it != g_chosenModes.end()) {
            chosen = it->second;
        }
    }
    std::optional<Mode>   chosenMode = chosen ? ModeByName(*chosen) : std::nullopt;
    Mode                  mode       = chosenMode ? std::move(*chosenMode) : ModeForBuffer(buffer);
    const std::lock_guard lock(g_mutex);
    return g_modeCache.insert_or_assign(&buffer, std::move(mode)).first->second;
}

void ClearModeCacheFor(const text::Buffer& buffer) {
    const std::lock_guard lock(g_mutex);
    g_modeCache.erase(&buffer);
    g_chosenModes.erase(&buffer);
}

bool SetChosenModeForBuffer(const text::Buffer& buffer, const std::string& modeName) {
    std::optional<Mode> mode = ModeByName(modeName);
    if (!mode) {
        return false;
    }
    const std::lock_guard lock(g_mutex);
    g_chosenModes.insert_or_assign(&buffer, modeName);
    g_modeCache.insert_or_assign(&buffer, std::move(*mode));
    return true;
}

std::vector<std::string> ModeNames() {
    std::vector<std::string> names;
    {
        const std::lock_guard lock(g_mutex);
        for (const auto& [name, mode] : g_registeredModes) {
            names.push_back(name);
        }
    }
    for (const RegisteredLanguage& registered : RegisteredLanguages()) {
        names.push_back(ModeNameFor(registered.definition));
    }
    for (const LanguageDefinition& definition : BundledLanguages()) {
        names.push_back(ModeNameFor(definition));
    }
    std::ranges::sort(names);
    names.erase(std::ranges::unique(names).begin(), names.end());
    return names;
}

void InsertPrewarmedMode(const text::Buffer& buffer, Mode mode) {
    const std::lock_guard lock(g_mutex);
    g_modeCache.try_emplace(&buffer, std::move(mode));
}

} // namespace ned::editor
