//
// A fold source built from a compiled-in imprint instead of a hand-written
// folds.scm.
//
// Same contract as `Mode::fold` -- byte ranges of every foldable block in a
// buffer's full text -- so a Mode can take either and nothing downstream can
// tell the difference. That equivalence was not assumed, and it is why no
// bundled language has a fold query any more: measured over 66 real files in
// the eleven languages that had one, the queries produced zero fold ranges
// this does not. `Tests/ImprintTest.cpp` pins what they said.
//
// Needs no grammar.json at runtime; `Editor/ImprintTables.h` carries the table.
//

#ifndef NED_EDITOR_IMPRINTFOLD_H
#define NED_EDITOR_IMPRINTFOLD_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

#include "Editor/Imprint.h"
#include "Editor/Mode.h"
#include "Editor/TreeSitter/Node.h"

namespace ned::editor::imprint {

// A FoldFunction for `language` (a tree-sitter language name), or an empty
// function if there is no compiled-in table for it -- in which case the caller
// keeps whatever it had, which is the hand-written query.
//
// `policy` is applied per node; see FoldPolicy for what is taste and what is
// structure. The multi-line rule is NOT applied here: it belongs to
// `Editor/CodeFold.h`, which already enforces it for every fold source, and
// duplicating it would be a second place to get it wrong.
// Fold blocks for an ALREADY-PARSED tree. This is the form a Mode uses, so
// the imprint rides the parse the mode already did rather than starting its
// own -- a second parser per buffer would double the per-keystroke cost, and
// this codebase has been bitten by exactly that before (see ROADMAP's note on
// the minimap re-highlighting on every keystroke).
//
// Returns nothing for a language with no compiled-in table, which is the
// honest answer for one that does not fold by delimiters at all.
// `text` is the buffer the tree was parsed from: an indentation body's fold
// starts on the row above its own first line (see `FoldAnchorStart`), and
// nothing but the text can say where that row begins.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
CollectFoldBlocks(const treesitter::Node& root, std::string_view language, std::string_view text,
                  FoldPolicy policy = {});

// The standalone form, for a caller with no tree of its own -- it owns a
// parser and an incremental cache. Prefer CollectFoldBlocks wherever a parse
// already exists.
[[nodiscard]] FoldFunction BuildFoldFunction(std::string_view language, FoldPolicy policy = {});

// Merges fold sources: every source names the regions it knows about, and the
// answer is their union, sorted and deduplicated.
//
// Union rather than precedence, deliberately, and this is the extension point
// worth understanding. A fold source is *additive* -- it asserts "this range
// is foldable", never "this range is not" -- so two sources cannot contradict
// each other, only cover different ground. That makes three cases fall out of
// one mechanism with no special-casing:
//
//   - a delimiter language (C, Rust, Go, ...) takes the imprint;
//   - a language that folds by its own rules takes only its own function --
//     Org folds by headline depth and Markdown by section structure, and
//     neither has a compiled-in table, so the imprint contributes nothing and
//     nothing has to say so;
//   - a language wanting both gets both, which is the case this exists for.
//     It will not stay at two.
//
// If a source ever needs to *suppress* a range rather than add one, this is
// where that would go, and it would be a real change rather than a tweak.
[[nodiscard]] FoldFunction MergeFoldSources(std::vector<FoldFunction> sources);

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTFOLD_H
