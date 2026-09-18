//
// The regex dialect a grammar's token patterns are written in, parsed to a
// small syntax tree the NFA builder consumes. The dialect is the subset of
// Rust's regex syntax (which tree-sitter's generator uses, in Unicode mode)
// that grammars actually use: literals and escapes (`\n`, `\xHH`, `\x{..}`,
// `\uHHHH`, `\u{..}`, `\UHHHHHHHH`), classes with ranges, nesting and
// negation, `\p{..}`/`\P{..}` properties, `.`, groups (capturing or not),
// alternation, and the quantifiers `* + ? {n} {n,} {n,m}` (a lazy `?` after
// a quantifier is accepted and ignored -- it changes nothing for a DFA).
// Assertions (`^ $ \b`), backreferences and class set operations are
// errors, as they are for tree-sitter.
//
// With the `i` flag every literal and class is closed under simple case
// folding (Unicode.h), exactly as the reference does before building the
// NFA.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_REGEX_H
#define NED_EDITOR_GRAMMAR_COMPILE_REGEX_H

#include <cstdint>
#include <string_view>
#include <vector>

#include "Editor/Grammar/Compile/Nfa.h"

namespace ned::editor::grammar::compile {

inline constexpr std::uint32_t kRegexUnbounded = static_cast<std::uint32_t>(-1);

struct RegexNode {
    enum class Kind : std::uint8_t { Empty,
                                     Class,
                                     Repetition,
                                     Concat,
                                     Alternation };
    Kind                   kind = Kind::Empty;
    CharacterSet           set;                   // Class (a literal is a one-character class)
    std::uint32_t          min = 0;               // Repetition
    std::uint32_t          max = kRegexUnbounded; // Repetition
    std::vector<RegexNode> children;              // Repetition: one; Concat/Alternation: members
};

// Throws CompileError naming what the pattern does that this dialect lacks.
[[nodiscard]] RegexNode ParseRegex(std::string_view pattern, bool caseInsensitive);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_REGEX_H
