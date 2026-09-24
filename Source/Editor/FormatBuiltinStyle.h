//
// A language's bundled formatting style: Source/Languages/<lang>/style.janet,
// the same :space/:break/:blank/... schema as format.janet, loaded into the
// Builtin rule layer (FormatRules.h) with every key scoped to "<lang>/".
// This is where a language's canonical style guide lives (PHP's PSR-12), so
// it applies without any user config and any format.janet or
// ned/set-format-* rule still overrides it field by field.
//

#ifndef NED_EDITOR_FORMATBUILTINSTYLE_H
#define NED_EDITOR_FORMATBUILTINSTYLE_H

#include <filesystem>

namespace ned::editor {

// Replaces the Builtin layer with every `<languagesDir>/<lang>/style.janet`.
// Throws std::runtime_error ("path:line: message") for a malformed file, a
// key that is already language-scoped, or a non-rule key (:indent and the
// save-hygiene keys stay user settings); the layer is left untouched then.
void LoadBuiltinFormatStyles(const std::filesystem::path& languagesDir);

// The same, from DataDir()/languages.
void LoadBuiltinFormatStyles();

} // namespace ned::editor

#endif // NED_EDITOR_FORMATBUILTINSTYLE_H
