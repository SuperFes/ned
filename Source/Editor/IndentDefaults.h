//
// configurable-formatter follow-up. The compiled-in, per-language safe
// default IndentStyle -- sourced from each language's own canonical
// formatter/style guide where one exists (PEP 8, rustfmt, PSR-12, gofmt,
// Prettier-as-de-facto-standard, ...), not tuned to any one person's taste.
// See Docs/FormattingRules.md for the full table with per-language source
// citations, including the handful that are honestly labeled a judgment
// call (no single canonical convention exists) rather than dressed up as
// settled fact.
//
// This is the new middle tier EffectiveIndentStyle (IndentStyle.h) consults
// between "no per-mode override configured" and the flat process-wide
// default: per-mode override -> this table, keyed by the mode's own
// language key -> process-wide default. A language with no entry here
// (FundamentalMode, or one whose convention isn't strong enough to assert)
// falls straight through to the process-wide default, same as before this
// file existed.
//

#ifndef NED_EDITOR_INDENTDEFAULTS_H
#define NED_EDITOR_INDENTDEFAULTS_H

#include <optional>
#include <string_view>

#include "IndentStyle.h"

namespace ned::editor {

// languageKey is LanguageDefinition::name ("python", "cpp", ... --
// imprint::LanguageKeyForMode's own convention, the inverse of ModeNameFor).
// nullopt when this language has no built-in entry.
[[nodiscard]] std::optional<IndentStyle> BuiltinIndentStyleForLanguage(std::string_view languageKey);

} // namespace ned::editor

#endif // NED_EDITOR_INDENTDEFAULTS_H
