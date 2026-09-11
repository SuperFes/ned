//
// file-rename-propagation follow-up: resolving an import/include specifier
// to the real file it names, with no BufferView involved.
//
// This is BufferView::OpenDetectedLink's own resolution half, lifted out
// verbatim so a second caller can use it -- Editor/ImportFixup.h's planner,
// which has to answer "does this import name the file that just moved?" for
// every candidate file in a project and has no pane, no viewport and no
// active buffer to ask. Nothing about the rule was editor-state-dependent
// in the first place: it needs the importing file's own path, that file's
// Mode, and the specifier. go-to-file-at-point still calls exactly this, so
// the two can never answer differently.
//
// Not pure -- it stats candidate paths and reads project settings, which is
// what resolving to a real file means. The arithmetic that runs afterwards
// (Editor/ImportFixup.h) is the pure half.
//

#ifndef NED_EDITOR_IMPORTRESOLVE_H
#define NED_EDITOR_IMPORTRESOLVE_H

#include <filesystem>
#include <optional>
#include <vector>

#include "Editor/Link.h"
#include "Editor/Mode.h"

namespace ned::editor {

// Resolves detected (kind File; a Url is the caller's own business) against
// importingFile's directory, the project root, the mode's configured
// include paths, the toolchain's own system include paths and -- for a
// language whose config asks for it -- the nearest node_modules chain,
// widened by that language's candidate extensions and package index
// basenames. An empty importingFile resolves from the project root instead,
// the same fallback a buffer with no file of its own already took.
//
// nullopt when nothing on disk matches: an unresolvable link is a dead one
// to report, never a path to create.
struct ResolvedImport {
    std::filesystem::path path;
    // The directory the specifier was counted from -- the importing file's
    // own (adjusted) directory, the project root, or an include path. See
    // Link.h's ResolveFileLink for why a rewrite has to know which.
    std::filesystem::path base;
};

[[nodiscard]] std::optional<ResolvedImport> ResolveImportLink(const link::DetectedLink&    detected,
                                                              const std::filesystem::path& importingFile,
                                                              const Mode&                  mode);

// Every directory ResolveImportLink would try, in the order it tries them:
// the importing file's own (ascended by Python's relative-import level,
// descended by Rust's file-per-module layout), then the project root, then
// the mode's include paths, the toolchain's, and any node_modules chain.
//
// Exposed because Editor/ImportFixup.h has to ask the same question with
// the answer already gone -- an externally detected move takes the file a
// specifier named before ned hears about it, leaving nothing to resolve
// against. Matching against these roots and only these is what keeps that
// inverse question from inventing a root no resolver would ever have used.
[[nodiscard]] std::vector<std::filesystem::path> ImportSearchRoots(const link::DetectedLink&    detected,
                                                                   const std::filesystem::path& importingFile,
                                                                   const Mode&                  mode);

} // namespace ned::editor

#endif // NED_EDITOR_IMPORTRESOLVE_H
