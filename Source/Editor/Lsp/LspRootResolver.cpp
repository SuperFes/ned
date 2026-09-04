#include "LspRootResolver.h"

#include <mutex>
#include <system_error>
#include <unordered_map>

#include "Editor/ProjectRoot.h"

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

    // Compiled-in defaults for the languages this codebase bundles a mode
    // for and that plausibly run a per-subpackage LSP server -- confirmed
    // against each ecosystem's own real, conventional root-marker file, not
    // guessed. Deliberately not exhaustive (html/css/json/yaml/toml/
    // markdown/org/xml/janet/clojure get none): those rarely run a
    // per-subdirectory server instance the way a monorepo's code languages
    // do, and an empty list here is harmless -- ResolveLspRoot's marker tier
    // just never matches, falling through to editor::ProjectRoot() exactly
    // as before this file existed.
    const std::unordered_map<std::string, std::vector<std::string>>& BuiltinDefaults() {
        static const std::unordered_map<std::string, std::vector<std::string>> defaults = {
            {"c", {"compile_commands.json", ".clangd", "CMakeLists.txt"}},
            {"cpp", {"compile_commands.json", ".clangd", "CMakeLists.txt"}},
            {"python", {"pyproject.toml", "setup.py", "setup.cfg"}},
            {"javascript", {"package.json", "jsconfig.json"}},
            {"typescript", {"package.json", "tsconfig.json"}},
            {"tsx", {"package.json", "tsconfig.json"}},
            {"php", {"composer.json"}},
            {"rust", {"Cargo.toml"}},
        };
        return defaults;
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

std::vector<std::string> LspRootMarkers(const std::string& language) {
    const std::lock_guard<std::mutex> lock(RootMarkersMutex());
    if (const auto it = RootMarkersOverrides().find(language); it != RootMarkersOverrides().end()) {
        return it->second;
    }
    const auto& defaults = BuiltinDefaults();
    if (const auto it = defaults.find(language); it != defaults.end()) {
        return it->second;
    }
    return {};
}

std::filesystem::path ResolveLspRoot(const std::filesystem::path& bufferPath, const std::string& language) {
    if (editor::AutoDetectProjectRoot()) {
        const std::vector<std::string> markers = LspRootMarkers(language);
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

            for (std::filesystem::path dir = start.parent_path();; dir = dir.parent_path()) {
                for (const std::string& marker : markers) {
                    std::error_code existsEc;
                    if (std::filesystem::exists(dir / marker, existsEc)) {
                        return dir;
                    }
                }
                if (dir == dir.parent_path()) { // reached the filesystem root
                    break;
                }
            }
        }
    }

    // No marker configured for language, none matched, or auto-detect is
    // off -- the existing single, process-wide root, unchanged.
    return editor::ProjectRoot();
}

} // namespace ned::editor::lsp
