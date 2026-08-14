//
// RAII wrapper around a tree-sitter TSParser (tree-sitter foundation
// follow-up) -- see Node.h's header comment for the overall "why a
// hand-rolled wrapper" rationale.
//
// Parse() always does a full re-parse of the text handed to it -- there's no
// ts_tree_edit-based incremental reparsing yet. That's a deliberate,
// documented v1 scope cut, not an oversight: tree-sitter is fast enough that
// a full reparse is the right first cut for correctness, with incremental
// editing as a follow-up optimization only if a real [Performance] test
// says it's needed (matching this project's own established "prove it with
// a test, don't pre-optimize" discipline -- see ROADMAP.md's rope-rebalance
// and VisualColumn stories).
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

    // Full parse of text -- see this file's own header comment for why this
    // isn't incremental yet.
    [[nodiscard]] Tree Parse(std::string_view text) const;

  private:
    TSParser* parser_ = nullptr;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_PARSER_H
