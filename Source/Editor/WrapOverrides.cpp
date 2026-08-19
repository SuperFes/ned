#include "WrapOverrides.h"

#include <mutex>
#include <unordered_map>

namespace ned::editor {

namespace {

    std::mutex                            g_mutex;
    std::unordered_map<std::string, bool> g_extensionOverrides;
    std::unordered_map<std::string, bool> g_filenameOverrides;

    // Duplicated from ModeOverrides.cpp's own StripLeadingDot rather than
    // shared -- not worth a new cross-file dependency for something this
    // small, the same call this codebase's own IsDotDirectory (ProjectSearch.cpp/
    // ProjectTree.cpp) already makes.
    std::string StripLeadingDot(std::string_view extension) {
        if (!extension.empty() && extension.front() == '.') {
            extension.remove_prefix(1);
        }
        return std::string(extension);
    }

} // namespace

void SetWrapForExtension(const std::string& extension, bool wrap) {
    const std::lock_guard lock(g_mutex);
    g_extensionOverrides.insert_or_assign(StripLeadingDot(extension), wrap);
}

void SetWrapForFilename(const std::string& filename, bool wrap) {
    const std::lock_guard lock(g_mutex);
    g_filenameOverrides.insert_or_assign(filename, wrap);
}

std::optional<bool> WrapLinesForFileOverride(const std::filesystem::path& path) {
    const std::lock_guard lock(g_mutex);
    if (const auto it = g_filenameOverrides.find(path.filename().string()); it != g_filenameOverrides.end()) {
        return it->second;
    }
    if (const auto it = g_extensionOverrides.find(StripLeadingDot(path.extension().string())); it != g_extensionOverrides.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool EffectiveWrapLines(const std::optional<std::filesystem::path>& path, const Mode& mode) {
    if (path) {
        if (const std::optional<bool> override = WrapLinesForFileOverride(*path); override) {
            return *override;
        }
    }
    return mode.wrapLines;
}

} // namespace ned::editor
