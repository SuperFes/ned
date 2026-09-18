//
// The bundled external scanners, by language name. Each lives in its own
// file here, ported from the grammar it belongs to (Tools/port-scanner.py
// does the mechanical half) against Editor/Parse/Scanner.h.
//

#ifndef NED_EDITOR_LANGUAGES_SCANNERS_SCANNERS_H
#define NED_EDITOR_LANGUAGES_SCANNERS_SCANNERS_H

#include <string_view>

#include "Editor/Parse/Scanner.h"

namespace ned::editor::languages::scanners {

// The scanner for `language` (a bundled language's name: "bash",
// "markdown-inline", ...), or null for a language whose grammar has no
// external tokens.
[[nodiscard]] const parse::ScannerVTable* FindBundledScanner(std::string_view language);

} // namespace ned::editor::languages::scanners

#endif // NED_EDITOR_LANGUAGES_SCANNERS_SCANNERS_H
