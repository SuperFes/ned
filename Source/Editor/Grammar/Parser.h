//
// The parser the editor drives -- since the Phase 4b engine swap a thin
// wrapper over ned's own parse::Engine rather than a TSParser*. See Node.h's
// header comment for the overall wrapper rationale.
//
// Parse(text) alone always does a full parse. Incremental-tree-sitter-
// reparse follow-up: Parse(text, oldTree) instead reuses oldTree's unedited
// subtrees, provided oldTree has already been brought up to date via
// Tree::Edit -- most callers should reach for IncrementalParse.h's
// IncrementalParseCache rather than calling this overload directly, since it
// also derives the edit itself from a plain "here is the new text" diff
// against its own last call.
//

#ifndef NED_EDITOR_GRAMMAR_PARSER_H
#define NED_EDITOR_GRAMMAR_PARSER_H

#include <memory>
#include <string_view>

#include "Editor/Parse/Parser.h"

#include "Tree.h"

// The opaque grammar handle a generated `tree_sitter_<name>()` entry point
// returns -- tree_sitter/api.h's own typedef, forward-declared here so
// nothing in ned_lib needs the tree-sitter runtime's headers (the runtime
// itself is no longer linked; ned's engine interprets the generated tables
// directly).
typedef struct TSLanguage TSLanguage; // NOLINT(modernize-use-using)

namespace ned::editor::grammar {

// A non-owning handle to one of tree-sitter's statically-linked-in grammar
// languages (e.g. the value returned by tree_sitter_json()) -- the
// TSLanguage itself lives in the grammar's own static/read-only data for the
// process lifetime, nothing here owns or frees it. See Languages.h for how a
// Language is actually obtained by name. The generated grammar artifacts
// (parser.c tables, lexers, external scanners) are exactly what ned's own
// engine interprets, so this stays the currency for grammar identity.
class Language {
  public:
    explicit Language(const TSLanguage* language) noexcept;

    [[nodiscard]] const TSLanguage* Raw() const noexcept;

  private:
    const TSLanguage* language_;
};

class Parser {
  public:
    // Throws std::runtime_error if language was generated for an ABI version
    // outside the engine's supported range (13-15) -- ordinarily only
    // reachable if a dynamically-loaded grammar (see the
    // dynamic-grammar-loading follow-up) was built against a mismatched
    // tree-sitter version.
    explicit Parser(const Language& language);
    ~Parser();

    Parser(Parser&& other) noexcept;
    Parser& operator=(Parser&& other) noexcept;
    Parser(const Parser&)            = delete;
    Parser& operator=(const Parser&) = delete;

    // Full parse of text.
    [[nodiscard]] Tree Parse(std::string_view text) const;

    // Incremental parse: oldTree must be the tree text was previously parsed
    // into, already updated via Tree::Edit to describe every edit applied
    // since that parse (see this file's own header comment). oldTree is read
    // only, never mutated or invalidated by this call -- it's safe to let it
    // go out of scope immediately after.
    [[nodiscard]] Tree Parse(std::string_view text, const Tree& oldTree) const;

  private:
    std::unique_ptr<parse::Engine> engine_;
};

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_PARSER_H
