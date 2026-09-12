//
// The bundled languages, as LanguageDefinitions. This table is what every
// per-language fact in the editor derives from -- the mode-name registry,
// the extension table, the language-scoped capture defaults -- and the
// literals here are the C++ stand-in for each language's own
// `Source/Languages/<name>/language.janet`, which replaces them.
//

#ifndef NED_EDITOR_BUNDLEDLANGUAGES_H
#define NED_EDITOR_BUNDLEDLANGUAGES_H

#include <string_view>
#include <vector>

#include "LanguageDefinition.h"

namespace ned::editor {

// Every bundled definition, in a fixed order (fundamental first). Building
// the table also registers the bundled escapes (Languages/*.cpp), so a
// definition naming one can always be built.
[[nodiscard]] const std::vector<LanguageDefinition>& BundledLanguages();

// By language key ("cpp"), or null.
[[nodiscard]] const LanguageDefinition* BundledLanguage(std::string_view name);

} // namespace ned::editor

#endif // NED_EDITOR_BUNDLEDLANGUAGES_H
