#include "RootResolver.h"

#include <mutex>
#include <system_error>
#include <unordered_map>

#include "Editor/LanguageRegistry.h"
#include "Editor/Project/Root.h"

namespace ned::editor::lsp {

namespace {

    std::mutex& RootMarkersMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, std::vector<std::string>>& RootMarkersOverrides() {
        static std::unordered_map<std::string, std::vector<std::string>> overrides;
        return overrides;
    }

} // namespace

void SetLspRootMarkers(const std::string& language, std::vector<std::string> markers) {
    const std::lock_guard<std::mutex> lock(RootMarkersMutex());
    if (markers.empty()) {
        RootMarkersOverrides().erase(language);
    }
    else {
        RootMarkersOverrides()[language] = std::move(markers);
    }
}

std::vector<std::string> RootMarkers(const std::string& language) {
    const std::lock_guard<std::mutex> lock(RootMarkersMutex());
    if (const auto it = RootMarkersOverrides().find(language); it != RootMarkersOverrides().end()) {
        return it->second;
    }
    // The defaults live in each language's own definition (language.janet's
    // :lsp-root-markers), registered shadowing bundled. Most languages
    // declare none -- html/css/json/yaml/markdown/... rarely run a
    // per-subdirectory server instance -- and an empty list is harmless: the
    // marker tier never matches and the buffer's root falls through to
    // editor::ProjectRoot().
    const std::optional<LanguageDefinition> definition = FindLanguageDefinition(language);
    return definition.has_value() ? definition->lspRootMarkers : std::vector<std::string>{};
}

std::filesystem::path ResolveLspRoot(const std::filesystem::path& bufferPath, const std::string& language) {
    if (editor::AutoDetectProjectRoot()) {
        const std::vector<std::string> markers = RootMarkers(language);
        if (!markers.empty()) {
            // Same absolutize-then-weakly_canonical order as
            // ProjectRoot.cpp's DetectProjectRoot -- see that function's own
            // comment for why the order matters (a relative path with no
            // existing leading portion must still absolutize first, or
            // weakly_canonical leaves it relative and the walk below never
            // terminates against a sane filesystem root).
            std::error_code       ec;
            std::filesystem::path start = std::filesystem::absolute(bufferPath, ec);
            if (ec) {
                start = bufferPath;
            }
            if (const std::filesystem::path canonical = std::filesystem::weakly_canonical(start, ec); !ec) {
                start = canonical;
            }

            if (std::optional<std::filesystem::path> root = FindMarkerRoot(start, markers)) {
                return std::move(*root);
            }
        }
    }

    // No marker configured for language, none matched, or auto-detect is
    // off -- the existing single, process-wide root, unchanged.
    return editor::ProjectRoot();
}

} // namespace ned::editor::lsp
