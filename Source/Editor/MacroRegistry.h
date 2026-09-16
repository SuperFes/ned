//
// Named keyboard macros: name -> the KeyChord sequence Dispatcher would
// otherwise only remember as its single anonymous LastMacro() slot.
// Process-wide, mutex-guarded -- SnippetRegistry.h's exact shape (free
// functions, one static map), not Register.h's char-keyed RegisterTable:
// a macro is invoked *by name*, the same "name -> thing" convention
// snippets/VCS providers use, not RegisterTable's single-character
// point/text register model. ned/register-macro is the Janet-facing
// surface. There is deliberately no auto-persisted macro file (see
// ThemeFile.h's own "small enough to write by hand is small enough to not
// auto-dump" precedent) -- kmacro-insert-macro-definition instead writes
// the equivalent ned/register-macro call as text for a user to keep in
// their own init.janet.
//

#ifndef NED_EDITOR_MACROREGISTRY_H
#define NED_EDITOR_MACROREGISTRY_H

#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"

namespace ned::editor {

// Re-registering an existing name overwrites it (CommandRegistry/
// SnippetRegistry's "redefining is expected use" convention); an empty
// chord sequence erases the entry (the ned/set-* "empty clears"
// convention). An empty name throws std::runtime_error.
void RegisterMacro(const std::string& name, const std::vector<KeyChord>& chords);

[[nodiscard]] std::optional<std::vector<KeyChord>> MacroForName(const std::string& name);

// Every registered macro name, sorted -- for the search-everywhere picker
// and introspection (ned/macro-names).
[[nodiscard]] std::vector<std::string> MacroNames();

// Test isolation only (the registry is process-wide static state).
void ClearAllMacros();

} // namespace ned::editor

#endif // NED_EDITOR_MACROREGISTRY_H
