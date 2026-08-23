//
// import-target-tree-sitter follow-up: node_modules search-path discovery
// for Editor/Link.cpp's ResolveFileLink, layered into a JS/TS-family mode's
// includePaths (Editor/ImportResolutionConfig.h's searchPackageDirs) the same
// way Editor/ToolchainIncludePaths.h layers a queried compiler's own system
// include paths into C/C++'s -- a bare import specifier ("import x from
// 'lodash'") isn't found relative to the importing file or the project root
// alone.
//
// v1 scope: Node's own directory-walk algorithm only -- no package.json
// "main"/"exports"/scoped-package ("@scope/name") resolution. A bare
// specifier that resolves to a real node_modules/<pkg> directory still needs
// ResolveFileLink's own index-file inference (ImportResolutionConfig's
// indexBasenames, "index") to find its entry point; a package whose real
// entry point isn't "index.<ext>" (i.e. anything using package.json "main")
// won't resolve. Good enough for the common case without a JSON-parsing,
// package-manager-aware resolver -- the same "hand-rolled resolver as a
// fallback, not a full implementation" scope every other cut in this
// follow-up shares.
//

#ifndef NED_EDITOR_NODEMODULES_H
#define NED_EDITOR_NODEMODULES_H

#include <filesystem>
#include <vector>

namespace ned::editor {

// Walks from baseDirectory upward through its own ancestors -- inclusive of
// projectRoot, stopping once the walk would go outside it (same bounded-walk
// precedent ToolchainIncludePaths.h already established: this is never a
// whole-filesystem search) -- collecting each ancestor's "node_modules"
// subdirectory where one exists as a real directory. Ordered nearest-first
// (baseDirectory's own node_modules, if any, before an ancestor's), matching
// Node's own resolution precedence. If projectRoot isn't an ancestor of
// baseDirectory at all (a buffer with no on-disk path, or one outside the
// project), the walk still runs from baseDirectory up to the filesystem
// root's own single remaining component rather than looping forever.
[[nodiscard]] std::vector<std::filesystem::path> NodeModulesSearchPaths(const std::filesystem::path& baseDirectory,
                                                                        const std::filesystem::path& projectRoot);

} // namespace ned::editor

#endif // NED_EDITOR_NODEMODULES_H
