//
// project-settings-lsp-init-options follow-up. Toolchain-queried default
// include-path discovery for Editor/Link.cpp's ResolveFileLink, layered
// underneath (never overriding) Editor/ProjectSettings.h's own
// user-configured includePaths -- a hardcoded guess at /usr/include-style
// system paths goes stale across distros (multiarch triplet subdirs,
// version-numbered C++ stdlib dirs, NixOS/container layouts with no fixed
// prefix at all -- the exact non-portable-system-layout problem
// Lsp/LspServerConfig.h's own doc comment already rejected auto-detecting
// for LSP commands). Querying the real, installed compiler instead is
// always correct for whatever's actually on this machine.
//
// v1 scope: C/C++ only (language == "c" or "cpp") -- the one case where a
// real toolchain and a portable, textual way to ask it "where do you search
// for headers" both exist (`cc`/`c++` `-E -v -xc`/`-xc++ /dev/null`, parsing
// the "#include <...> search starts here:" ... "End of search list." block
// every GCC/Clang-compatible compiler emits on stderr). Every other
// language this project supports either has no such toolchain or no
// portable way to ask it (see ROADMAP.md) -- QueryToolchainIncludePaths
// returns nullopt immediately for anything but "c"/"cpp", not a guess.
//
// A probe spawns a real subprocess, so results are cached to
// $XDG_CACHE_HOME/ned/toolchain-include-paths.json (disposable/regenerable
// data, not user config or editor state -- this codebase's own XDG
// convention) keyed by language, each entry timestamped and expired after
// IncludePathCacheTtlSeconds(). Shared across every ned process on this
// machine (not per-process memoized), so starting several editors at once
// only pays the subprocess cost from whichever one gets there first.
// refresh-toolchain-include-paths (Commands.cpp) clears the whole cache
// file for a manual, immediate refresh.
//

#ifndef NED_EDITOR_TOOLCHAININCLUDEPATHS_H
#define NED_EDITOR_TOOLCHAININCLUDEPATHS_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor {

// Runs the compiler for language (currently just "c"/"cpp") and parses its
// real system include search paths -- nullopt for any other language, or if
// the compiler isn't on $PATH, spawning it fails, or its output doesn't
// parse as expected. Never throws.
[[nodiscard]] std::optional<std::vector<std::filesystem::path>> QueryToolchainIncludePaths(const std::string& language);

// Cached wrapper around QueryToolchainIncludePaths: serves a cached,
// not-yet-expired result from $XDG_CACHE_HOME/ned/toolchain-include-paths.json
// if one exists, else probes for real and writes the result back (atomically
// -- a sibling .ned-tmp + rename, same convention ProjectSession.cpp/
// ProjectReplace.cpp already use) before returning it. Returns an empty list
// (not nullopt) for a language with no probe support or a failed probe --
// ResolveFileLink treats an empty includePaths list as "nothing more to
// search," the same as no entry configured at all. Never throws -- a cache
// read/write failure (unwritable $XDG_CACHE_HOME, a malformed cache file,
// ...) just falls back to an uncached probe.
[[nodiscard]] std::vector<std::filesystem::path> ToolchainIncludePathsForLanguage(const std::string& language);

// Deletes the whole cache file, if present -- refresh-toolchain-include-paths's
// own implementation; the next ToolchainIncludePathsForLanguage call for any
// language re-probes from scratch. A no-op (not an error) if no cache file
// exists yet, or if it can't be removed.
void ClearToolchainIncludePathCache();

// Mutex-guarded process-wide scalar, mirroring Editor/TabWidth.h's exact
// shape. Default 86400 (24h) -- a compiler's own installed include paths
// change only on a compiler upgrade, not per session. ned/set-include-path-
// cache-ttl-seconds is the Janet binding; 0 (or negative) disables caching
// outright, so every call re-probes.
void              SetIncludePathCacheTtlSeconds(int seconds);
[[nodiscard]] int IncludePathCacheTtlSeconds();

} // namespace ned::editor

#endif // NED_EDITOR_TOOLCHAININCLUDEPATHS_H
