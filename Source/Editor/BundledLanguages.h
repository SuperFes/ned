//
// The bundled languages, as LanguageDefinitions -- each parsed from its own
// `Source/Languages/<name>/language.janet` (Editor/LanguageParse.h) with
// query files discovered by convention. This table is what every
// per-language fact in the editor derives from: the mode-name registry, the
// extension table, the capture defaults, LSP root markers, import
// resolution, injection aliases and bundled snippets.
//

#ifndef NED_EDITOR_BUNDLEDLANGUAGES_H
#define NED_EDITOR_BUNDLEDLANGUAGES_H

#include <string_view>
#include <vector>

#include "LanguageDefinition.h"

namespace ned::editor {

// Every bundled definition, sorted by name. Building the table also
// registers the bundled escapes (Languages/*.cpp), so a definition naming
// one can always be built.
[[nodiscard]] const std::vector<LanguageDefinition>& BundledLanguages();

// By language key ("cpp"), or null.
[[nodiscard]] const LanguageDefinition* BundledLanguage(std::string_view name);

} // namespace ned::editor

#endif // NED_EDITOR_BUNDLEDLANGUAGES_H
