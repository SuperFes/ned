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
//   - A mid-line indentation body anchors its interior to its own COLUMN
//     rather than contributing a level (`anchorsAtOwnColumn`). The text
//     before it on its row -- YAML's `- ` marker -- fixes where its first
//     member sits, so `other` in `- key: v` belongs under `key` wherever
//     `key` happens to be, which level arithmetic cannot say: a level would
//     also collapse into the enclosing sequence's own, since the two open on
//     one row. This is `@aligned`'s rule for a body whose opener is empty.
//
// Nothing about alignment: `@aligned`, `@align.barrier` and `@indent.body` stay
// declared per language, as `Indent.h` says they should.
//

#ifndef NED_EDITOR_IMPRINTINDENT_H
#define NED_EDITOR_IMPRINTINDENT_H

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "Editor/Grammar/Node.h"

namespace ned::editor::imprint {

// One closing delimiter, as the indent walk wants it: the token's own range
// and grammar type, so `Indent.cpp` can find the container it closes by
// parentage exactly as it does for a query's `(X "}" @dedent)`.
//
// indent-cache-by-byte-range follow-up: `type` replaces what was a raw
// `Node::Id()` (nodeId) -- IndentCaptures' own maps moved off tree-generation
// -specific node identity (see that struct's own doc comment) to
// (startByte, endByte, type), so this token's identity is expressed the same
// way from the moment it's collected.
struct ImprintDedent {
    std::size_t      startByte = 0;
    std::size_t      endByte   = 0;
    std::string_view type;
};

// One container instance: its own range, grammar type, and the byte its
// interior begins at -- after the opener for a bracket body, the body's own
// start for an indentation body. `Indent.cpp` counts a container only for
// lines beginning inside it.
struct ImprintContainer {
    std::size_t      startByte = 0;
    std::size_t      endByte   = 0;
    std::string_view type;
    std::size_t      interiorStart = 0;
    // An indentation body that begins mid-line: its interior aligns to
    // startByte's own visual column instead of counting as a level. See the
    // header's own bullet.
    bool anchorsAtOwnColumn = false;
    // for-loop-header-imprint follow-up: set only when the closer is
    // followed by a real, unrelated trailing field (a for-loop's own body
    // statement) -- nullopt in the overwhelmingly common case where the
    // closer already sits at endByte. See Indent.h's IndentCaptures::interiorEnd
    // for why this travels as a separate, optional field rather than
    // narrowing endByte itself.
    std::optional<std::size_t> interiorEnd;
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
[[nodiscard]] ImprintIndentCaptures CollectIndentCaptures(const grammar::Node& root, std::string_view language,
                                                          std::string_view text);

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTINDENT_H
