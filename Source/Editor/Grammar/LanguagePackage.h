//
// A language package is a directory: `grammar.janet` (the grammar), its
// compiled `tables` (`ned --compile-language`; compiled in-process when
// the file is absent), and the query files beside them. Loading one gives
// the engine its tables plus the grammar's external scanner -- a bundled
// one by name (Languages/Scanners/Scanners.h), or a shared library the
// definition names with `:scanner-library`, which exports
//
//     extern "C" const ned::editor::parse::ScannerVTable* ned_scanner_<name>();
//
// (Parse/Scanner.h's NED_SCANNER_LIBRARY_EXPORT writes that function).
// Loaded packages live for the process, keyed by directory: a Language is
// a non-owning handle into them.
//

#ifndef NED_EDITOR_GRAMMAR_LANGUAGEPACKAGE_H
#define NED_EDITOR_GRAMMAR_LANGUAGEPACKAGE_H

#include <filesystem>
#include <string>

#include "Parser.h"

namespace ned::editor::grammar {

struct PackageScanner {
    std::string           name;    // the `ned_scanner_<name>` symbol / bundled registry key
    std::filesystem::path library; // empty: the bundled registry
};

// Throws std::runtime_error with a path-qualified message when the
// directory holds neither `tables` nor `grammar.janet`, either is
// malformed, the grammar declares external tokens but no scanner was found,
// or the scanner library cannot be opened or lacks its export.
[[nodiscard]] Language LoadLanguagePackage(const std::filesystem::path& directory, const PackageScanner& scanner);

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_LANGUAGEPACKAGE_H
