//
// grammar.janet in, a language the engine can run out. The whole pipeline
// (Prepare.h, NodeTypes.h, ParseTable.h, LexTable.h, Tables.h) in
// tree-sitter's generator order, so a grammar compiles to the same
// behaviour its generated parser.c had.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_COMPILER_H
#define NED_EDITOR_GRAMMAR_COMPILE_COMPILER_H

#include <memory>

#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/Compile/Tables.h"

namespace ned::editor::grammar::compile {

// Throws CompileError with the reference's own wording for grammar errors
// (undefined symbols, unresolved conflicts, ...).
[[nodiscard]] std::unique_ptr<CompiledLanguage> CompileGrammar(const GrammarFile& file);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_COMPILER_H
