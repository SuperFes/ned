//
// grammar.janet -> the syntax and lexical grammars the table builders take.
// A port of tree-sitter's generator `parse_grammar.rs` and `prepare_grammar/`
// (v0.25.10): the same passes in the same order, so the tables ned builds
// from a grammar say what tree-sitter's did.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_PREPARE_H
#define NED_EDITOR_GRAMMAR_COMPILE_PREPARE_H

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/GrammarFile.h"

namespace ned::editor::grammar::compile {

struct PreparedGrammar {
    SyntaxGrammar        syntax;
    LexicalGrammar       lexical;
    InlinedProductionMap inlines;
    AliasMap             defaultAliases;
};

// GrammarFile -> InputGrammar: rule-form translation (REPEAT becomes
// choice(repeat1, blank), a pattern's flags keep only `i`) and the
// pruning of rules nothing reaches.
[[nodiscard]] InputGrammar ParseInputGrammar(const GrammarFile& file);

// The whole preparation pipeline. Throws CompileError.
[[nodiscard]] PreparedGrammar PrepareGrammar(const InputGrammar& input);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_PREPARE_H
