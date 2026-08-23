//
// RAII wrapper around a tree-sitter TSParser (tree-sitter foundation
// follow-up) -- see Node.h's header comment for the overall "why a
// hand-rolled wrapper" rationale.
//
// Parse(text) alone always does a full parse. Incremental-tree-sitter-
// reparse follow-up: Parse(text, oldTree) instead reuses oldTree's unedited
// subtrees, provided oldTree has already been brought up to date via
// Tree::Edit -- most callers should reach for IncrementalParse.h's
// IncrementalParseCache rather than calling this overload directly, since it
// also derives the TSInputEdit itself from a plain "here is the new text"
// diff against its own last call.
//

#ifndef NED_EDITOR_TREESITTER_PARSER_H
#define NED_EDITOR_TREESITTER_PARSER_H

#include <string_view>

#include <tree_sitter/api.h>

#include "Tree.h"

namespace ned::editor::treesitter {

// A non-owning handle to one of tree-sitter's statically-linked-in grammar
// languages (e.g. the value returned by tree_sitter_json()) -- the
// TSLanguage itself lives in the grammar's own static/read-only data for the
// process lifetime, nothing here owns or frees it. See Languages.h for how a
// Language is actually obtained by name.
class Language {
  public:
    explicit Language(const TSLanguage* language) noexcept;

    [[nodiscard]] const TSLanguage* Raw() const noexcept;

  private:
    const TSLanguage* language_;
};

class Parser {
  public:
    // Throws std::runtime_error if language is incompatible with this
    // tree-sitter build's ABI version (ts_parser_set_language's own failure
    // mode -- ordinarily only reachable if a dynamically-loaded grammar, see
    // the dynamic-grammar-loading follow-up, was built against a mismatched
    // tree-sitter version).
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
    TSParser* parser_ = nullptr;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_PARSER_H
