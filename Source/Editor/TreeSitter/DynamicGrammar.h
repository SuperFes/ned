//
// Runtime-loaded tree-sitter grammars (dynamic-grammar-loading follow-up) --
// the counterpart to Languages.h's compile-time bundled registry, for any
// language not in that curated set. Loads a shared library via dlopen and
// resolves "tree_sitter_<name>", tree-sitter's own C entry-point naming
// convention -- the same one Languages.cpp's bundled forward-declarations
// rely on, just resolved at runtime instead of link time.
//

#ifndef NED_EDITOR_TREESITTER_DYNAMICGRAMMAR_H
#define NED_EDITOR_TREESITTER_DYNAMICGRAMMAR_H

#include <filesystem>
#include <string_view>

#include "Parser.h"

namespace ned::editor::treesitter {

// Loads languageName's grammar from libraryPath (dlopen + dlsym
// "tree_sitter_<languageName>"), returning a Language wrapping the result.
// Throws std::runtime_error if the library can't be opened or doesn't
// export that symbol.
//
// The underlying dlopen handle is intentionally never dlclose'd -- it's
// kept resident for the rest of the process's lifetime, since the returned
// Language's TSLanguage* points into that library's own data for as long as
// anything built from it (a Parser, a Query) might still be in use, and
// there's no per-language unload/reload story yet. Matches the same "load
// once for the process lifetime, don't build a teardown story nothing needs
// yet" scope cut already established for janet::Environment.
[[nodiscard]] Language LoadDynamicLanguage(const std::filesystem::path& libraryPath, std::string_view languageName);

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_DYNAMICGRAMMAR_H
