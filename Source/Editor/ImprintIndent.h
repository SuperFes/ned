//
// An indent source built from the compiled-in imprint instead of a hand-written
// indents.scm -- the third structural driver off the delimited-body fact, after
// folding and bracket matching.
//
// A fold source contributes an assertion and an indent source contributes a
// quantity (see Docs/ParsingEngine.md, "Fold sources compose; indent sources do
// not"): every container this reports adds one level to the lines inside it,
// so unlike `ImprintFold.h` this cannot be unioned with a query blindly. Two
// rules make it composable anyway. A query capture on the same node stands --
// `@aligned` and `@indent.body` refine a container the imprint also reports,
// and the walk already counts a node once however many sets name it. And a
// query may say *minus*: `@indent.suppress` removes a container from what the
// imprint contributes, without touching anything the query itself asserts.
// C++'s top-level `namespace` body is the case that needs it, and it is a
// layout convention rather than a structural fact, which is exactly the
// Tier 0 / Tier 1 line.
//
// What is reported, and why each rule is an instance test rather than a table
// lookup:
//
//   - A bracket body counts iff `DelimitersOf(node)` finds its brackets --
//     Kotlin's `function_body` is `{ ... }` or `= expr`, and C#'s
//     `namespace_declaration` carries its braces in a child. Its closer is a
//     dedent, aligning the closing line with the line the container opened on,
//     which is what `(X "}" @dedent)` said by hand. A keyword body (bash's
//     `do ... done`, fish's `if ... end`) is the same thing with the pair the
//     table recorded, and is what `(do_group) @indent` + `(do_group "done"
//     @dedent)` said by hand.
//   - An indentation body counts iff it has no introducer of its own and has
//     a header: something it is indented relative to. The first is a grammar
//     fact (`openerIsFirst`): Python's `if_statement` owns its `if x:` row and
//     its `block` is the thing indented beneath it. The second is a text fact,
//     and it is `FoldAnchorStart`'s own -- a body on its own line has a header
//     iff some line above is shallower, which is what rules out a root (YAML's
//     `stream`, a document's top-level mapping, a TOML pair at column zero)
//     with nothing said per language; a body that begins mid-line (`- key: v`,
//     a compact YAML mapping inside a sequence item) has its header on that
//     same row. In an indentation language the body IS the indentation, so a
//     body indented relative to nothing is not a container.
//
// Nothing about alignment: `@aligned`, `@align.barrier` and `@indent.body` stay
// declared per language, as `Indent.h` says they should.
//

#ifndef NED_EDITOR_IMPRINTINDENT_H
#define NED_EDITOR_IMPRINTINDENT_H

#include <cstddef>
#include <string_view>
#include <vector>

#include "Editor/TreeSitter/Node.h"

namespace ned::editor::imprint {

// One closing delimiter, as the indent walk wants it: the token's own range and
// its identity, so `Indent.cpp` can find the container it closes by parentage
// exactly as it does for a query's `(X "}" @dedent)`.
struct ImprintDedent {
    std::size_t startByte = 0;
    std::size_t endByte   = 0;
    const void* nodeId    = nullptr;
};

// One container instance: its identity, and the byte its interior begins at
// -- after the opener for a bracket body, the body's own start for an
// indentation body. `Indent.cpp` counts a container only for lines beginning
// inside it.
struct ImprintContainer {
    const void* nodeId        = nullptr;
    std::size_t interiorStart = 0;
};

struct ImprintIndentCaptures {
    std::vector<ImprintContainer> containers;
    std::vector<ImprintDedent>    dedents;
};

// Every indent container and closing delimiter in an already-parsed tree, for
// a language with a compiled-in table. Empty for one without, which is the
// honest answer for a language that does not indent by delimiters at all.
// `text` is the buffer the tree was parsed from -- an indentation body's
// "begins its own line" is a fact about the text, not the tree.
[[nodiscard]] ImprintIndentCaptures CollectIndentCaptures(const treesitter::Node& root, std::string_view language,
                                                          std::string_view text);

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTINDENT_H
