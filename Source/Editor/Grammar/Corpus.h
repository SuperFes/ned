//
// Grammar test corpora: the `test/corpus/*.txt` format tree-sitter grammars
// ship (a `====` header naming the case, its input, a `----` divider, the
// expected tree as an S-expression) and the run of one case against a
// language. Shared by the conformance tests and `ned --test-language`.
//
// The reader follows the reference's own rules exactly -- fence suffixes,
// `:skip`/`:error`/`:cst`/`:platform`/`:language` markers, comment stripping and
// whitespace normalization of the expected tree -- so a corpus written for
// the grammar's upstream tooling reads the same here.
//

#ifndef NED_EDITOR_GRAMMAR_CORPUS_H
#define NED_EDITOR_GRAMMAR_CORPUS_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Parse/Parser.h"

namespace ned::editor::grammar::corpus {

struct Case {
    std::string              name;
    std::string              file; // corpus-relative path, for reporting
    std::string              input;
    std::string              expected; // normalized; empty for :error cases
    bool                     skip            = false;
    bool                     error           = false;
    bool                     cst             = false; // expected is a concrete-syntax listing, not an S-expression
    bool                     platformMatches = true;
    bool                     hasFields       = false;
    std::vector<std::string> languages; // :language(...) values; single "" if none
    // Byte span of the expected tree in the file (after the divider line, up
    // to the next header), for rewriting it.
    std::size_t expectedStart = 0;
    std::size_t expectedEnd   = 0;
};

[[nodiscard]] std::vector<Case> ParseCorpusFile(std::string_view content, const std::string& fileLabel);

// The reference's normalization of an expected tree: `;` comment lines
// removed, whitespace collapsed to single spaces, none before `)`.
[[nodiscard]] std::string NormalizeExpected(std::string_view raw);
// Whether an expected tree spells fields (`name: (node)`); a case without
// them is compared with fields stripped from the actual tree.
[[nodiscard]] bool        HasFieldSyntax(std::string_view sexp);
[[nodiscard]] std::string StripSexpFields(std::string_view sexp);

// The tree as the corpus compares it.
[[nodiscard]] std::string ActualSexp(const parse::GreenTree& tree, bool keepFields);

// The reference's `:cst` rendering (`tree-sitter parse --cst`): one node
// per line with its row:column range, anonymous tokens quoted, leaf text
// in backticks, depth as indentation. Compared verbatim against a `:cst`
// case's expected text.
[[nodiscard]] std::string RenderCst(const parse::GreenTree& tree, std::string_view input);

// The tree in the form `item` compares -- the S-expression or the CST.
[[nodiscard]] std::string ActualOutput(const parse::GreenTree& tree, const Case& item);

// One node per line, two spaces per depth -- the layout a corpus file
// records an expected tree in.
[[nodiscard]] std::string PrettySexp(std::string_view sexp);

// What `--bless` writes as the expected text for a failing case: the pretty
// S-expression, or the CST listing as is.
[[nodiscard]] std::string BlessedExpected(const Case& item, std::string_view actual);

// Every corpus file under `directory`, sorted, whatever the extension
// (tree-sitter-make's end in .mk, tree-sitter-typst's in .scm).
[[nodiscard]] std::vector<std::filesystem::path> CorpusFiles(const std::filesystem::path& directory);

struct CaseResult {
    bool        passed = false;
    std::string actual; // the compared form; empty for a passing :error case
};

[[nodiscard]] CaseResult RunCase(parse::Engine& engine, const Case& item);

} // namespace ned::editor::grammar::corpus

#endif // NED_EDITOR_GRAMMAR_CORPUS_H
