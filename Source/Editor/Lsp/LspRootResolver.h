//
// LSP multi-root follow-up. Resolves which directory an LSP server should be
// initialized against for one buffer, instead of every buffer in the process
// sharing editor::ProjectRoot() unconditionally (see LspManager.h's own
// updated header comment for how LspManager actually uses this).
//
// editor::ProjectRoot()'s own VCS-marker walk (ProjectRoot.h) can't separate
// independent packages inside one monorepo, since a monorepo typically has
// exactly one .git at the very top -- that's the exact case a flat
// process-wide root gets wrong (a Python subpackage's buffer initializing
// clangd -- or nothing at all -- against the outer repo root instead of its
// own pyproject.toml directory). This file adds a second, language-specific
// marker tier ahead of that VCS walk: real project-file markers
// (package.json, Cargo.toml, compile_commands.json, ...), the same
// convention every mainstream LSP client (VS Code included) resolves a
// server's root against.
//
// Mutex-guarded static map, mirroring LspServerConfig.h's own per-language
// table shape.
//

#ifndef NED_EDITOR_LSP_LSPROOTRESOLVER_H
#define NED_EDITOR_LSP_LSPROOTRESOLVER_H

#include <filesystem>
#include <string>
#include <vector>

namespace ned::editor::lsp {

// Overwrites the marker list for language (e.g. {"pyproject.toml", "setup.py"}
// for "python") -- re-registering replaces, mirroring
// LspServerConfig::SetLspServerCommand's own "redefining is expected use"
// convention. An empty list clears the override, reverting language to its
// compiled-in default (see LspRootMarkers) rather than to "no markers at
// all" -- unlike SetLspServerCommand, a marker list has a real built-in
// default to fall back to.
void SetLspRootMarkers(const std::string& language, std::vector<std::string> markers);

// The effective marker list for language: an explicit SetLspRootMarkers
// override if one is registered, else the compiled-in default for a handful
// of bundled languages (LspRootResolver.cpp), else empty. Empty is not an
// error -- ResolveLspRoot's marker tier just never matches anything and
// falls through to its next tier.
[[nodiscard]] std::vector<std::string> LspRootMarkers(const std::string& language);

// Resolves the LSP root for a buffer at bufferPath whose primary language is
// `language` (Editor/Mode.h's LanguageKeyForMode convention -- the same
// string LspServerConfig.h's command table is keyed by). Two tiers:
//
//  1. If editor::AutoDetectProjectRoot() is on and LspRootMarkers(language)
//     is non-empty, walks upward from bufferPath's containing directory for
//     the nearest ancestor containing one of those markers as an immediate
//     child (file or directory, checked the same way
//     ProjectRoot.cpp's own VCS-marker walk checks .git/.hg/.svn/.bzr) --
//     the nearest package.json for a "javascript" buffer, say.
//  2. editor::ProjectRoot() -- the existing single, process-wide root,
//     completely unchanged. Reached whenever tier 1 found nothing (no
//     markers configured for language, none matched, or auto-detect is
//     off), which is every buffer in a project that hasn't configured any
//     root markers -- this is what keeps ResolveLspRoot byte-for-byte
//     backward compatible with the pre-multi-root single-root behavior
//     until a project actually opts in by having (or configuring) markers.
[[nodiscard]] std::filesystem::path ResolveLspRoot(const std::filesystem::path& bufferPath, const std::string& language);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPROOTRESOLVER_H
