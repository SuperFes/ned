//
// CUE import paths as directories: a path under the enclosing module (the
// nearest cue.mod/module.cue) is found under that module's directory, and
// anything else under cue.mod's gen/, pkg/ or usr/. Import resolution's
// `:cue-modules` key (ImportResolutionConfig.h).
//

#ifndef NED_EDITOR_CUEMODULES_H
#define NED_EDITOR_CUEMODULES_H

#include <filesystem>
#include <string>
#include <string_view>

namespace ned::editor {

// The module path a cue.mod/module.cue declares, without its major-version
// suffix ("example.com/m@v0" -> "example.com/m"); empty when it declares none.
[[nodiscard]] std::string ParseCueModulePath(std::string_view moduleFile);

// Where an import path's package directory is counted from: `root` joined
// with `remainder`. `prefix` is the part of the path `root` stands for, which
// a rewrite keeps as written. An empty `root` means nothing on disk answers
// the path: the standard library, a module only the cache holds, or a
// package-qualified path ("a/b:name").
struct CueImportRoot {
    std::string           prefix;
    std::string           remainder;
    std::filesystem::path root;
};

[[nodiscard]] CueImportRoot CueImportRootFor(std::string_view importPath, const std::filesystem::path& searchStart);

} // namespace ned::editor

#endif // NED_EDITOR_CUEMODULES_H
