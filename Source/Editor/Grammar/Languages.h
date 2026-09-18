//
// The bundled grammars, by name: each is a language package under
// `<data dir>/languages/<name>/` (Editor/LanguageFiles.h), loaded on first
// use through Grammar/LanguagePackage.h and kept for the process.
//

#ifndef NED_EDITOR_GRAMMAR_LANGUAGES_H
#define NED_EDITOR_GRAMMAR_LANGUAGES_H

#include <optional>
#include <string_view>

#include "Parser.h"

namespace ned::editor::grammar {

// Looks up a bundled grammar by its lowercase name (e.g. "json"). Returns
// std::nullopt if name isn't a bundled grammar -- not an error, since a
// caller (a Mode picking a language by file extension) needs to fall back
// gracefully to no highlighting for an unbundled language, the same way
// FundamentalMode already means "no highlighting" today. A package that
// exists but cannot be loaded throws (LanguagePackage.h).
[[nodiscard]] std::optional<Language> LanguageByName(std::string_view name);

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_LANGUAGES_H
