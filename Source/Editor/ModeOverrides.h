//
// A user-configurable override table pointing a filename or file extension
// at a Mode -- either one of the bundled *Mode() functions (Mode.h) or a
// grammar loaded at runtime (TreeSitter/DynamicGrammar.h).
//
// Originally built dynamic-registrations-only (dynamic-grammar-loading
// follow-up, as DynamicMode.h); widened and renamed (this follow-up) once
// it became clear the exact same mechanism was the right fix for two more
// real, related asks: remapping a *bundled* extension (e.g. .phtml ->
// PhpMode) without a rebuild, and matching by a file's *full name* (e.g.
// "CMakeLists.txt"), which is needed for any grammar keyed off a
// conventional filename rather than a distinguishing extension -- ".txt"
// alone can't tell CMakeLists.txt apart from any other text file.
// "Dynamic" would now be a misleading name for this file, since an override
// can point at a compiled-in mode just as easily as a dlopen'd one.
//
// Mutex-guarded static state, mirroring TabWidth.h/ProjectRoot.h's exact
// pattern, just holding a few maps instead of one scalar.
//

#ifndef NED_EDITOR_MODEOVERRIDES_H
#define NED_EDITOR_MODEOVERRIDES_H

#include <filesystem>
#include <optional>
#include <string>

#include "Mode.h"

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor {

// Registers an already-built Mode directly under name (ModeByName checks
// this table first) -- for a Mode whose only job is a keymap/comment-prefix
// with no grammar of its own (e.g. the VCS commit-message buffer's
// finish/abort bindings, see Editor/Vcs/Runner.h). Everything grammar-shaped
// registers a DEFINITION instead (Editor/LanguageRegistry.h), which
// ModeByName rebuilds fresh per lookup. Overwrites any previous
// registration under name -- re-registering is expected use.
void RegisterMode(const std::string& name, Mode mode);

// Flushes the per-buffer mode cache -- what every mode-affecting
// registration calls so an already-open buffer picks up the change on its
// next resync (see CachedModeForBuffer). LanguageRegistry.h's registration
// path is the out-of-file caller.
void ClearAllModeCaches();

// Looks up a Mode by name ("lua-mode", "c-mode"): RegisterMode's table
// first, then "<key>-mode" against the language registry (registered
// shadows bundled -- see LanguageRegistry.h), each definition-backed mode
// rebuilt fresh per lookup. std::nullopt if name matches nothing -- not an
// error, mirroring treesitter::LanguageByName's own "caller falls back
// gracefully" convention.
[[nodiscard]] std::optional<Mode> ModeByName(const std::string& name);

// Points extension at modeName -- resolved lazily against ModeByName
// whenever ModeForFileOverride is called below, not validated eagerly here,
// so Janet init.janet can register a dynamic grammar and point an extension
// at it in either order. extension may be given with or without a leading
// '.' (both "phtml" and ".phtml" resolve the same way) -- Janet callers
// naturally write the former, but ModeForFileOverride is called with
// std::filesystem::path::extension()'s own with-a-dot form.
void SetModeForExtension(const std::string& extension, const std::string& modeName);

// Points filename (the full, exact base name of a file, e.g.
// "CMakeLists.txt" or "Makefile" -- no wildcards/globbing) at modeName,
// resolved the same lazy way SetModeForExtension is. Checked before
// SetModeForExtension's table by ModeForFileOverride, matching Emacs'
// auto-mode-alist convention that a more specific (whole-filename) pattern
// wins over a less specific (extension-only) one.
void SetModeForFilename(const std::string& filename, const std::string& modeName);

// Resolves path against SetModeForFilename's table (path.filename(), exact
// match, checked first) and then SetModeForExtension's table
// (path.extension()), then ModeByName. main.cpp's ModeForExtension is the
// only caller, checking this before its own hardcoded bundled-extension
// table, so an override can replace a bundled mapping too, not just add a
// new one. std::nullopt if neither table has an entry for path, or the
// mapped name doesn't resolve via ModeByName.
[[nodiscard]] std::optional<Mode> ModeForFileOverride(const std::filesystem::path& path);

// per-buffer-mode follow-up. Checks ModeForFileOverride first, then falls
// back to a fixed extension table (routed through ModeByName rather than
// each bundled *Mode() factory directly, so this stays a thin caller of the
// registry above instead of a second, competing source of truth), then
// FundamentalMode() if nothing matches. Promoted out of main.cpp (was a
// local, main.cpp-only anonymous-namespace function) so both startup and
// BufferView's per-buffer resync path (see BufferView::
// SetOnActiveBufferChanged) call the exact same table, rather than two
// copies that could drift.
[[nodiscard]] Mode ModeForPath(const std::filesystem::path& path);

// FundamentalMode() for a buffer with no associated path (e.g. a scratch
// buffer), otherwise ModeForPath(*buffer.Path()).
[[nodiscard]] Mode ModeForBuffer(const text::Buffer& buffer);

// per-buffer-mode-cache follow-up: memoized ModeForBuffer, keyed by buffer
// identity. A Mode's highlight/fold/expandSelection/sexpMotion closures
// each carry a shared_ptr<treesitter::Parser>/Query/parsed-tree cache
// (Mode.cpp's TreeSitterModeFromLanguage) -- ModeForBuffer rebuilds all of
// that from scratch on every call, so calling it fresh on every buffer
// switch (as WindowManager used to) silently discarded an already-parsed
// tree the moment the user looked at a different buffer, forcing a full
// tree-sitter reparse on switching back even though nothing about that
// buffer had changed. This caches the built Mode per buffer instead, so a
// repeat call returns a cheap copy (Mode itself is a small value type;
// copying it just copies a few std::functions, sharing their captured
// state) that still has last call's parsed tree warm. Every real Mode-
// resolution call site (main.cpp's initial pane, WindowManager's active-
// buffer-changed callback and split/session-restore pane construction)
// should call this instead of ModeForBuffer directly; ModeForBuffer itself
// stays the uncached primitive this builds on, and what tests exercise
// directly to avoid depending on cache state across TEST_CASEs.
// Main-thread only, like every other piece of state in this file -- Mode
// resolution never happens off the UI thread.
[[nodiscard]] Mode CachedModeForBuffer(const text::Buffer& buffer);

// Erases buffer's entry (if any) from CachedModeForBuffer's cache. Called
// from WindowManager's shared buffer-close funnel (ReassignPanesShowing,
// alongside Multibuffer.h's ClearMultibufferIndexFor -- the same
// established "close funnel clears every per-buffer cache keyed by this
// Buffer*" precedent) so a closed buffer's cached Mode -- and the real
// tree-sitter Parser/Query/tree it holds onto -- doesn't linger forever,
// and so a *different*, later buffer can never accidentally key-collide
// with it if the allocator reuses the same address. Also the right call
// after rebinding a still-open buffer to a new path (SetPath, e.g.
// rename-file) whose extension resolves to a different mode -- otherwise
// the cached Mode built for the old path would never update.
void ClearModeCacheFor(const text::Buffer& buffer);

// background-mode-prewarm follow-up: installs mode into the cache for
// buffer, but only if nothing is cached for it yet -- unlike
// CachedModeForBuffer's own insert_or_assign, this never clobbers a Mode
// some other path (a real switch, another prewarm) already resolved and
// may already be in active use by a live pane sharing its tree-sitter
// tree cache. The one caller is ModePrewarm.cpp's background-thread
// completion callback.
void InsertPrewarmedMode(const text::Buffer& buffer, Mode mode);

} // namespace ned::editor

#endif // NED_EDITOR_MODEOVERRIDES_H
