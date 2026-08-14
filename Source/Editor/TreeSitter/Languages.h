//
// The registry of tree-sitter grammars statically linked into this binary
// (tree-sitter foundation follow-up) -- distinct from, and a prerequisite
// for, the dynamic-grammar-loading follow-up's runtime-loaded ones. Each
// bundled grammar is a separate FetchContent'd repo in CMakeLists.txt
// exposing exactly one C entry point, `tree_sitter_<name>()`, per
// tree-sitter's own convention -- this file is the one place those get
// forward-declared and named.
//
// Phase 1 (tree-sitter foundation) bundled only "json", to prove the
// plumbing end-to-end; the bundle-remaining-grammars follow-up adds "c",
// "cpp", "php", "javascript", "typescript", "tsx", "html", "css", "python",
// "bash", "janet", and "markdown" -- Perl was on the user's own requested
// list but is skipped: no official tree-sitter grammar for it ships a
// pre-generated parser.c (would need the Node-based tree-sitter CLI this
// project's build deliberately never depends on), and the user explicitly
// agreed to drop it rather than work around that. See ROADMAP.md.
//

#ifndef NED_EDITOR_TREESITTER_LANGUAGES_H
#define NED_EDITOR_TREESITTER_LANGUAGES_H

#include <optional>
#include <string_view>

#include "Parser.h"

namespace ned::editor::treesitter {

// Looks up a bundled grammar by its lowercase name (e.g. "json"). Returns
// std::nullopt if name isn't a bundled grammar -- not an error, since a
// caller (a Mode picking a language by file extension) needs to fall back
// gracefully to no highlighting for an unbundled language, the same way
// FundamentalMode already means "no highlighting" today.
[[nodiscard]] std::optional<Language> LanguageByName(std::string_view name);

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_LANGUAGES_H
