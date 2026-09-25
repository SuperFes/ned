//
// Go import paths as directories: a module-local path is found under the
// nearest go.mod's directory, a required module's under the module cache (or
// wherever a replace directive points), and the standard library's under
// GOROOT/src. Import resolution's `:go-modules` key (ImportResolutionConfig.h).
//

#ifndef NED_EDITOR_GOMODULES_H
#define NED_EDITOR_GOMODULES_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct GoModRequire {
    std::string path;
    std::string version;
};

struct GoModReplace {
    std::string path;
    std::string version; // empty: every version of path
    std::string newPath; // a module path, or a local directory ("./x", "../x", "/x")
    std::string newVersion;
};

struct GoModFile {
    std::string               module;
    std::vector<GoModRequire> requirements;
    std::vector<GoModReplace> replaces;
};

// The directives import resolution needs; everything else (go, toolchain,
// exclude, retract, comments) is skipped.
[[nodiscard]] GoModFile ParseGoMod(std::string_view text);

// The module cache's case encoding: each upper-case letter becomes '!' and
// its lower-case form ("github.com/BurntSushi/toml" -> "github.com/!burnt!sushi/toml").
[[nodiscard]] std::string EscapeGoModulePath(std::string_view path);

// Where an import path's package directory is counted from: `root` joined
// with `remainder`. `prefix` is the part of the path `root` stands for, which
// a rewrite keeps as written. An empty `root` means nothing on disk answers
// the path, and nothing else is to be searched for it.
struct GoImportRoot {
    std::string           prefix;
    std::string           remainder;
    std::filesystem::path root;
};

// Resolves importPath from importingFile (an empty one resolves from
// searchStart instead): the nearest go.mod's own module, then its replace
// directives and requirements (longest path wins), then the standard library
// (a path whose first element has no dot).
[[nodiscard]] GoImportRoot GoImportRootFor(std::string_view importPath, const std::filesystem::path& searchStart);

// $GOROOT and $GOMODCACHE, else `go env` (asked once per process). Empty when
// neither says.
[[nodiscard]] std::filesystem::path GoRoot();
[[nodiscard]] std::filesystem::path GoModuleCache();

} // namespace ned::editor

#endif // NED_EDITOR_GOMODULES_H
