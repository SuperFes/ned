//
// import-target-tree-sitter follow-up: node_modules search-path discovery
// for Editor/Link.cpp's ResolveFileLink, layered into a JS/TS-family mode's
// includePaths (Editor/ImportResolutionConfig.h's searchPackageDirs) the same
// way Editor/ToolchainIncludePaths.h layers a queried compiler's own system
// include paths into C/C++'s -- a bare import specifier ("import x from
// 'lodash'") isn't found relative to the importing file or the project root
// alone.
//
// v1 scope was Node's own directory-walk algorithm only -- no package.json
// "main"/"exports" resolution, just ResolveFileLink's own generic index-file
// inference ("index.<ext>"). ResolvePackageEntryPoint below (resolver-gaps
// follow-up) widens that: real package.json "main"/"exports" support, tried
// by Editor/Link.cpp's TryVariants before falling back to the bare
// "index.<ext>" guess. Scoped-package ("@scope/name") resolution still needs
// no special handling here -- it's already just another path segment
// (node_modules/@scope/name) as far as this walk and TryVariants are
// concerned.
//

#ifndef NED_EDITOR_NODEMODULES_H
#define NED_EDITOR_NODEMODULES_H

#include <filesystem>
#include <optional>
#include <string>
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

// resolver-gaps follow-up: given a real package directory (e.g.
// node_modules/lodash, already confirmed to exist by the caller), reads its
// own package.json and resolves its real entry-point file -- "exports"
// (preferred, Node's modern resolution field: a bare string, or an object
// keyed by subpath with "." as the package root, itself either a string or a
// nested per-condition object tried in "import"/"node"/"default"/"require"
// order -- a best-effort, non-spec-exhaustive walk, not a full Node
// resolution algorithm) falling back to the older "main" string field.
// candidateExtensions (ImportResolutionConfig's own per-language list) is
// tried appended to the resolved path when it doesn't exist verbatim, and
// once more as "<resolved>/index.<ext>" when the resolved path is itself a
// directory (a package whose "main" points at a subdirectory rather than a
// file). nullopt when there's no package.json, neither field resolves to an
// existing file, or the file named doesn't actually exist on disk --
// Editor/Link.cpp's TryVariants falls back to its own bare index-file
// inference in every such case, so this is a pure widening, never a
// regression versus the pre-existing behavior.
[[nodiscard]] std::optional<std::filesystem::path>
ResolvePackageEntryPoint(const std::filesystem::path& packageDirectory, const std::vector<std::string>& candidateExtensions);

} // namespace ned::editor

#endif // NED_EDITOR_NODEMODULES_H
