//
// The Unicode facts a grammar's token regexes need: `\p{...}` classes
// (general categories and the identifier properties) and the simple case
// folding a case-insensitive pattern applies. Categories and folding come
// from utf8proc, the identifier tables from UnicodeTables.cpp.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_UNICODE_H
#define NED_EDITOR_GRAMMAR_COMPILE_UNICODE_H

#include <optional>
#include <string_view>
#include <vector>

#include "Editor/Grammar/Compile/Nfa.h"

namespace ned::editor::grammar::compile::unicode {

// The characters with property or general category `name` -- a one- or
// two-letter category ("L", "Nd"), a long category alias ("Letter",
// "Control"), an identifier property ("XID_Start") or an emoji property
// ("Emoji", "Emoji_Modifier"/"EMod", ...). Nullopt for a name
// this doesn't know.
[[nodiscard]] std::optional<CharacterSet> Property(std::string_view name);

// Whether `c` is a letter (general category L* or Nl) -- the generator's
// keyword-candidate test, which asks Rust's `char::is_alphabetic`. That is
// the Alphabetic property, which additionally covers some marks and symbols
// (Other_Alphabetic); a token spelled with those is not a keyword here.
[[nodiscard]] bool IsAlphabetic(std::uint32_t c);

// Every character that folds to the same case-folding orbit as `c`,
// including `c` itself (`k` -> k, K, KELVIN SIGN).
[[nodiscard]] const std::vector<std::uint32_t>& CaseFoldOrbit(std::uint32_t c);

// `set` closed under case folding.
[[nodiscard]] CharacterSet CaseFold(const CharacterSet& set);

} // namespace ned::editor::grammar::compile::unicode

#endif // NED_EDITOR_GRAMMAR_COMPILE_UNICODE_H
