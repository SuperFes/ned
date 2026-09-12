//
// The delimited-body imprint for each bundled language, as a compiled-in
// table rather than something read from a grammar.json at runtime.
//
// `TreeSitter/GrammarImprint.h` derives an imprint by parsing a
// grammar.json. That file only exists in the FetchContent tree -- it is not
// installed, and an installed `ned` has no access to it -- so the table has
// to travel with the binary.
//
// ImprintTables.cpp is GENERATED and checked in. Regenerate it with:
//
//     NED_BLESS_IMPRINT=1 ./build/ned_tests "[Imprint]"
//
// and read the diff. `Tests/ImprintTest.cpp` holds the generated table
// against live inference over the real grammars on every run, so a stale
// table fails the build rather than quietly serving yesterday's answer --
// the same guard shape `Tests/ThemeKeyDocsTest.cpp` and the oracle already
// use. A build-time codegen binary would make staleness structurally
// impossible instead, at the cost of a tool that must run on the build host;
// worth revisiting if this table ever grows past the handful of languages it
// covers.
//

#ifndef NED_EDITOR_IMPRINTTABLES_H
#define NED_EDITOR_IMPRINTTABLES_H

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Imprint.h"

namespace ned::editor::imprint {

// The imprint for `language` (the tree-sitter language name -- "cpp",
// "python", ...), or an empty map for a language with no compiled-in table.
// An empty result is a real answer, not an error: it means the caller falls
// back to whatever it did before, which is the hand-written query.
[[nodiscard]] const std::map<std::string, DelimitedBody>& TableFor(std::string_view language);

// Every language with a compiled-in table, sorted.
[[nodiscard]] std::vector<std::string> TabledLanguages();

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTTABLES_H
