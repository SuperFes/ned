//
// `ned --test-language <dir>...` (also `ned-test-language`): run a language
// package's corpus against its grammar -- the authoring loop for a
// hand-written grammar.janet, and what an import ends with.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_TESTCOMMAND_H
#define NED_EDITOR_GRAMMAR_COMPILE_TESTCOMMAND_H

#include <iosfwd>
#include <string>
#include <vector>

namespace ned::editor::grammar::compile {

// Each directory is a language package (its language.janet names the
// grammar and scanner library; its own grammar.janet/tables, or the bundled
// grammar it borrows) with the corpus under `corpus/` or `test/corpus/`.
// Prints each failing case with the expected and actual trees and a
// per-package tally; with `bless`, rewrites the failing cases' expected
// trees in place instead. Returns the process exit code (1 when a case
// fails, 2 for a package that cannot be loaded).
int RunTestLanguage(const std::vector<std::string>& directories, bool bless, std::ostream& out, std::ostream& err);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_TESTCOMMAND_H
