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

// Loads name's grammar from libraryPath (see TreeSitter/DynamicGrammar.h)
// and queryPath's content, builds a Mode from them, and registers it under
// name -- overwriting any previous registration under the same name, the
// same "re-registering is expected use, not an error" convention
// CommandRegistry::Register already established. Throws std::runtime_error
// (propagated from the grammar load, queryPath's read, or a malformed
// query) -- a bad Janet call here is expected to surface as a real error to
// the user, not fail silently.
// queryPath (highlights) and foldQueryPath (generic-code-folding follow-up)
// are both optional, same "@fold"-capture query convention
// TreeSitterModeFromLanguage's own querySource/foldQuerySource parameters
// use -- an empty path means the dynamically registered grammar gets no
// highlighting/no fold support respectively, the same outcome any bundled
// mode missing one of those queries already has. Some real grammars ship
// only one of the two (e.g. a fold/locals query but no highlights.scm), so
// requiring both was never a real constraint to begin with.
void RegisterDynamicMode(const std::string& name, const std::filesystem::path& libraryPath,
                         const std::filesystem::path& queryPath = {}, const std::filesystem::path& foldQueryPath = {});

// Looks up a Mode by name. Checks names registered via RegisterDynamicMode
// first, then the bundled *Mode() functions' own names ("c-mode",
// "json-mode", ... see BundledModeFactories in the .cpp) -- dynamic checked
// first purely as a safe default (a name is only ever ambiguous between the
// two if some future bundled mode's name happened to also be a valid
// tree-sitter C symbol suffix; none of the current ones are, since they all
// carry a "-mode" suffix and a hyphen can't appear in a C identifier, so
// RegisterDynamicMode's own dlsym("tree_sitter_<name>") call would already
// fail before a real collision could occur). std::nullopt if name matches
// neither -- not an error, mirroring treesitter::LanguageByName's own
// "caller falls back gracefully" convention.
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

} // namespace ned::editor

#endif // NED_EDITOR_MODEOVERRIDES_H
