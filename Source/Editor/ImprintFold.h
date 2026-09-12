//
// A fold source built from a compiled-in imprint instead of a hand-written
// folds.scm.
//
// Same contract as `Mode::fold` -- byte ranges of every foldable block in a
// buffer's full text -- so a Mode can take either and nothing downstream can
// tell the difference. That equivalence is not assumed: `Tests/ImprintTest.cpp`
// holds this against the hand-written queries byte for byte across every
// bundled language that has both.
//
// Needs no grammar.json at runtime; `Editor/ImprintTables.h` carries the table.
//

#ifndef NED_EDITOR_IMPRINTFOLD_H
#define NED_EDITOR_IMPRINTFOLD_H

#include <string>
#include <string_view>

#include "Editor/Imprint.h"
#include "Editor/Mode.h"

namespace ned::editor::imprint {

// A FoldFunction for `language` (a tree-sitter language name), or an empty
// function if there is no compiled-in table for it -- in which case the caller
// keeps whatever it had, which is the hand-written query.
//
// `policy` is applied per node; see FoldPolicy for what is taste and what is
// structure. The multi-line rule is NOT applied here: it belongs to
// `Editor/CodeFold.h`, which already enforces it for every fold source, and
// duplicating it would be a second place to get it wrong.
[[nodiscard]] FoldFunction BuildFoldFunction(std::string_view language, FoldPolicy policy = {});

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTFOLD_H
