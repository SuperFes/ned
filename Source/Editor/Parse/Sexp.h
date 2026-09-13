#pragma once

#include <string>

#include "Editor/Parse/Green.h"

// Renders a green tree in tree-sitter's canonical S-expression form (named
// nodes only, `field: ` prefixes, `(ERROR)`, `(MISSING "tok")`,
// `(UNEXPECTED 'c')`) — the format the upstream corpus expectations are
// written in. Port of subtree.c's ts_subtree__write_to_string.

namespace ned::editor::parse {

std::string SubtreeToSexp(Subtree self, const abi::LanguageData* language);

} // namespace ned::editor::parse
