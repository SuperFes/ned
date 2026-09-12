//
// Matching-delimiter lookup, driven by the imprint.
//
// `Docs/ParsingEngine.md` and `Docs/LanguageCoverage.md` both list brace
// matching among the things a delimited-body fact should give you for free,
// alongside folds and indent. It was never built: ned has `forward-sexp`/
// `backward-sexp` (balanced-expression *motion*, via `Mode::sexpMotion`) but
// nothing that answers "where is the partner of the bracket under my cursor".
//
// The imprint already knows. Every entry in `Editor/ImprintTables.h` is a node
// whose production opens and closes with a matched pair, so the answer is that
// node's own first and last children -- no bracket-counting scan, no confusion
// about a brace inside a string or comment, because the parse already settled
// that.
//
// Deliberately no `Mode` hook. Unlike fold or indent this needs point, which
// `Mode`'s capability signatures do not carry, and unlike them there is no
// hand-written query to compose with -- it is a query *of* the imprint rather
// than a source feeding a pipeline.
//

#ifndef NED_EDITOR_IMPRINTBRACKET_H
#define NED_EDITOR_IMPRINTBRACKET_H

#include <cstddef>
#include <optional>
#include <string_view>

#include "Editor/Imprint.h"
#include <string>

#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Tree.h"

namespace ned::editor::imprint {

// The pair whose opener or closer point sits on or immediately after, or
// nullopt when point is not on a delimiter at all.
//
// "On or immediately after" matches how every editor treats this: with the
// caret just past a closing brace, that brace is the one you meant. When both
// readings are available -- `){` with the caret between them -- the one point
// sits *on* wins, which is the commoner intent.
//
// Only bracket-delimited bodies answer. An indentation body (Python's `block`)
// has no opener to match, and reporting its dedent as a "bracket" would be a
// lie told to a feature whose whole job is precision.
[[nodiscard]] std::optional<DelimiterPair> MatchingDelimitersAt(const treesitter::Node& root,
                                                                std::string_view language, std::size_t point);

// The partner offset to jump to for a caret at `point`: the closer's start
// when point is on the opener, the opener's start when point is on the closer.
// nullopt when point is not on a delimiter.
[[nodiscard]] std::optional<std::size_t> MatchingDelimiterOffset(const treesitter::Node& root,
                                                                 std::string_view language, std::size_t point);

// "cpp-mode" -> "cpp". The same suffix strip Mode.cpp does when deriving its
// own language key, exposed so a command can reach the table without
// reimplementing the convention.
[[nodiscard]] std::string LanguageKeyForMode(std::string_view modeName);


} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINTBRACKET_H
