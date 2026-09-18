//
// `ned --compile-language <dir>...` (also `ned-langc`): compile a language
// package's grammar.janet to its `tables` file. The whole command lives here
// rather than in main.cpp so it is unit-testable; main.cpp only dispatches.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_COMPILECOMMAND_H
#define NED_EDITOR_GRAMMAR_COMPILE_COMPILECOMMAND_H

#include <iosfwd>
#include <string>
#include <vector>

namespace ned::editor::grammar::compile {

// Each source is a language directory (its grammar.janet is compiled) or a
// grammar.janet path. `output` names the tables file and is only allowed
// with one source; the default is `tables` beside the grammar. One line per
// language on `out`; an error on `err` names the source and the mistake --
// grammar errors read as the generator's own (undefined symbol, unresolved
// conflict). Returns the process exit code.
int RunCompileLanguage(const std::vector<std::string>& sources, const std::string& output, std::ostream& out, std::ostream& err);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_COMPILECOMMAND_H
