//
// `language.janet` -> LanguageDefinition. The file is pure Janet data (read
// by Editor/JanetData.h, no VM -- see that header for why loading must not
// need one), one struct of keyword-keyed fields; an unknown key, a
// wrong-typed value, or a `:name` disagreeing with the directory is a loud
// error naming the line, never a silently ignored field.
//
// Query files are DISCOVERED, not listed: for each kind,
// `<dir>/upstream/<kind>.janet` then `<dir>/<kind>.janet`, in that order --
// upstream first, ned's delta after, which is exactly the concatenation
// order a tags delta needs. `<dir>` is the language's own directory, or
// `:queries-from`'s (jank reads clojure's, tsx typescript's); an explicit
// `:queries {:kind [paths]}` entry replaces discovery for that kind alone
// (cpp's imports are c's file). A definition therefore names files only
// where the convention doesn't hold.
//

#ifndef NED_EDITOR_LANGUAGEPARSE_H
#define NED_EDITOR_LANGUAGEPARSE_H

#include <functional>
#include <string_view>

#include "LanguageDefinition.h"

namespace ned::editor {

// True when a query file exists at `path` -- FindEmbeddedLanguageFile for
// the bundled set, a filesystem check for a user's language directory.
using QueryFileExists = std::function<bool(std::string_view path)>;

// `directoryName` is the language's directory (also its default name);
// errors are prefixed "<directoryName>/language.janet:".
[[nodiscard]] LanguageDefinition ParseLanguageDefinition(std::string_view directoryName, std::string_view source);

// Fills definition.queries by the discovery rule above. `prefix` is
// prepended to discovered relative paths ("" for the embedded set, the
// user directory's own absolute parent for a filesystem one).
void DiscoverQueryFiles(LanguageDefinition& definition, const QueryFileExists& exists, std::string_view prefix = {});

} // namespace ned::editor

#endif // NED_EDITOR_LANGUAGEPARSE_H
