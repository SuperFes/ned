//
// `ned --import-language <git-url-or-dir>` (also `ned-import-language`):
// turn a tree-sitter grammar repository into a ned language package. The
// grammar's `src/grammar.json` becomes `grammar.janet`, its upstream
// `queries/*.scm` become `upstream/<kind>.janet`, its `test/corpus` is
// copied, its `scanner.c` is staged under `scanner/` (and ported to a
// `<Name>Scanner.cpp` when the port-scanner helper is available), and a
// `language.janet` skeleton records what the admission policy asks for
// (Docs/LanguageCoverage.md). The package is then compiled and its corpus
// run, and the scorecard printed.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_IMPORTCOMMAND_H
#define NED_EDITOR_GRAMMAR_COMPILE_IMPORTCOMMAND_H

#include <iosfwd>
#include <string>

namespace ned::editor::grammar::compile {

struct ImportOptions {
    std::string source; // a git URL (cloned shallowly) or a local checkout
    std::string name;   // the language name; default: grammar.json's
    std::string subdir; // the grammar's directory inside a multi-grammar repository
    std::string ref;    // a tag or branch to clone
    std::string into;   // the languages root the package is written under
};

// Returns the process exit code: 0 with the package written and its corpus
// passing, 1 when the corpus has failures or the scanner still needs
// building, 2 when the import itself failed.
int RunImportLanguage(const ImportOptions& options, std::ostream& out, std::ostream& err);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_IMPORTCOMMAND_H
