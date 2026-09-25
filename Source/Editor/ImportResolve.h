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
#include <string>
#include <string_view>
#include <vector>

#include "Editor/ImportResolutionConfig.h"
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

// The language's import-resolution config with the project's own
// override laid over it (Editor/ImportResolutionConfig.h).
[[nodiscard]] ImportResolutionConfig ImportResolutionConfigFor(const Mode& mode);

// A module path ("pkg.mod", "Foo::Bar", "my-app.core") as the relative file
// path it stands for, before extension widening ("pkg/mod", "Foo/Bar",
// "my_app/core").
[[nodiscard]] std::string ModulePathToFilePath(std::string_view module, const ImportResolutionConfig& config);

// The link an import names -- what go-to-file opens and what a rename's
// fixup resolves -- so the two read every specifier the same way.
[[nodiscard]] link::DetectedLink ImportLinkFor(const ImportTarget& target, const ImportResolutionConfig& config);

// A target counted from a fixed root rather than from the importing file:
// Godot's "res://ui/menu.gd", Dart's "package:app/src/x.dart" (the
// language's :root-prefixes and :package-scheme). Nothing else is searched
// for one -- not the importing file's directory, not the project root.
struct PrefixedImport {
    std::string           prefix;    // "res://", "package:app/"
    std::string           remainder; // "ui/menu.gd", "src/x.dart"
    std::filesystem::path root;      // empty when nothing on disk maps the prefix
};

[[nodiscard]] std::optional<PrefixedImport> MatchImportPrefix(const std::string&           target,
                                                              const std::filesystem::path& importingFile,
                                                              const Mode&                  mode);

[[nodiscard]] std::optional<ResolvedImport> ResolveImportLink(const link::DetectedLink&    detected,
                                                              const std::filesystem::path& importingFile,
                                                              const Mode&                  mode);

// Every directory ResolveImportLink would try, in the order it tries them:
// the importing file's own (ascended by Python's relative-import level,
// descended by Rust's file-per-module layout), then the project root, then
// the language's source roots, the mode's include paths, the toolchain's,
// and any node_modules chain.
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
