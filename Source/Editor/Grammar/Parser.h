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

#include "Editor/Parse/Abi.h"

#include "Editor/Parse/Parser.h"

#include "Tree.h"

namespace ned::editor::grammar {

// A non-owning handle to a loaded language's tables (Parse/Abi.h) -- they
// belong to the language package that loaded them (LanguagePackage.h) and
// live for the process, nothing here owns or frees them. See Languages.h
// for how a Language is obtained by name.
class Language {
  public:
    explicit Language(const parse::abi::LanguageData* language) noexcept;

    [[nodiscard]] const parse::abi::LanguageData* Raw() const noexcept;

  private:
    const parse::abi::LanguageData* language_;
};

class Parser {
  public:
    // Throws std::runtime_error if the language's tables are not the engine's
    // own layout (Parse/Abi.h's kAbiVersion) -- ordinarily only reachable
    // through a package whose `tables` file predates a format change and
    // was not recompiled from its grammar.janet.
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
