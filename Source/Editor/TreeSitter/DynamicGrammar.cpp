#include "DynamicGrammar.h"

#include <dlfcn.h>

#include <stdexcept>
#include <string>

namespace ned::editor::treesitter {

Language LoadDynamicLanguage(const std::filesystem::path& libraryPath, std::string_view languageName) {
    void* const handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        throw std::runtime_error("ned: failed to load tree-sitter grammar library '" + libraryPath.string() +
                                 "': " + dlerror());
    }

    const std::string symbolName = "tree_sitter_" + std::string(languageName);

    // dlerror() must be cleared immediately before the dlsym() call, per
    // dlsym's own documented idiom -- it's the only way to distinguish a
    // symbol that genuinely resolves to a null value from a lookup failure,
    // since dlsym itself returns nullptr in both cases.
    dlerror();
    void* const       symbol = dlsym(handle, symbolName.c_str());
    const char* const error  = dlerror();
    if (error != nullptr) {
        throw std::runtime_error("ned: tree-sitter grammar library '" + libraryPath.string() + "' has no symbol '" +
                                 symbolName + "': " + error);
    }

    // The unavoidable object-pointer-to-function-pointer cast every real
    // dlsym-based loader needs (POSIX explicitly sanctions this despite
    // strict ISO C++ not guaranteeing it in general).
    using LanguageFn      = const TSLanguage* (*)();
    const auto languageFn = reinterpret_cast<LanguageFn>(symbol);
    return Language(languageFn());
}

} // namespace ned::editor::treesitter
