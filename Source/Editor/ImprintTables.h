//
// The delimited-body imprint for each language, derived from its
// grammar.janet the first time a language asks and cached for the process.
//
// `Grammar/GrammarImprint.h` reads the imprint off a grammar's rules; this
// is the lookup by language name that the drivers (`ImprintFold.h`,
// `ImprintBracket.h`, `ImprintIndent.h`) and Mode building go through. A
// definition that borrows another language's grammar (jank -> clojure) is
// answered with that grammar's imprint.
//

#ifndef NED_EDITOR_IMPRINTTABLES_H
#define NED_EDITOR_IMPRINTTABLES_H

#include <map>
#include <string>
#include <string_view>

#include "Editor/Imprint.h"

namespace ned::editor::imprint {

// The imprint for `language` (a language definition's name -- "cpp",
// "python", "jank", ...), or an empty map for a language whose grammar the
// imprint reads nothing out of, or that has no grammar at all. An empty
// result is a real answer, not an error: it means the caller falls back to
// whatever it did before, which is the hand-written query.
[[nodiscard]] const std::map<std::string, DelimitedBody>& TableFor(std::string_view language);

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTTABLES_H
