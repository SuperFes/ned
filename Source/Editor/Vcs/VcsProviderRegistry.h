//
// The process-wide registry of VcsProvider implementations, and the
// "which one is active for this project" resolution on top of it.
// Mutex-guarded static state, mirroring TabWidth.h/ProjectRoot.h's exact
// pattern (see either for the convention this follows), just holding a
// provider table instead of one scalar.
//

#ifndef NED_EDITOR_VCS_VCSPROVIDERREGISTRY_H
#define NED_EDITOR_VCS_VCSPROVIDERREGISTRY_H

#include <filesystem>
#include <memory>
#include <string>

#include "VcsProvider.h"

namespace ned::editor::vcs {

// Registers provider under name, taking ownership. Re-registering under
// an already-used name overwrites it in place (same registration order),
// the same "re-registering is expected use, not an error" convention
// CommandRegistry::Register/ModeOverrides::RegisterDynamicMode already
// establish -- a Janet plugin reloading its own init code is expected
// use, not something to guard against.
void RegisterProvider(const std::string& name, std::unique_ptr<VcsProvider> provider);

// Returns whichever registered provider's Detect(root) returns true
// first, checked in registration order (first match wins) -- deliberately
// not "most specific match" or any other ranking, since a real ambiguous
// case (two providers both claiming the same root) isn't expected to
// arise in practice and isn't worth designing around speculatively.
// Result is cached per (canonicalized) root; the cache is populated
// lazily and only invalidated by an explicit ClearProviderCache() call
// (registering a new provider does not by itself invalidate any
// already-cached resolution) -- Detect() may touch the filesystem, so
// this must not run on every frame/every call. Returns nullptr if no
// registered provider's Detect matches.
[[nodiscard]] VcsProvider* ActiveProviderFor(const std::filesystem::path& root);

// Clears any cached root -> provider resolutions from ActiveProviderFor,
// without touching the registry itself. Call after registering a
// provider if a fresh resolution against roots checked earlier is
// wanted; otherwise a stale nullptr/wrong-provider answer can persist for
// a root that was resolved before the relevant provider existed.
void ClearProviderCache();

// Test-only: removes every registered provider and clears the resolution
// cache, restoring a clean-slate registry. Mirrors the *TestReset-style
// helpers this codebase already uses for other mutex-guarded static state
// under test (e.g. ModeOverrides' own tests re-registering freely).
void ClearRegistry();

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSPROVIDERREGISTRY_H
