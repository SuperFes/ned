#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>

#include "Editor/TreeSitter/DynamicGrammar.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Tree.h"

using namespace ned::editor::treesitter;

namespace {

// A real, non-bundled tree-sitter grammar's shared library, if this
// machine happens to have one installed system-wide -- exercising the
// actual dlopen/dlsym path against a real .so, not just its failure
// modes, needs a real library on disk. Deliberately not FetchContent'd
// (this is exactly what dynamic loading is for: consuming whatever's
// already on the host, not something ned's own build controls), so
// tests that need it SKIP rather than fail when it's absent -- an
// intentional, documented exception to this project's usual "no
// external test dependencies" discipline.
const std::filesystem::path kLuaLibrary = "/usr/lib64/libtree-sitter-lua.so";

} // namespace

TEST_CASE("LoadDynamicLanguage throws for a library path that doesn't exist", "[TreeSitter][Dynamic]") {
    REQUIRE_THROWS_AS(LoadDynamicLanguage("/not/a/real/path/libtree-sitter-nonsense.so", "nonsense"),
                      std::runtime_error);
}

TEST_CASE("LoadDynamicLanguage throws for a real library missing the requested symbol", "[TreeSitter][Dynamic]") {
    if (!std::filesystem::exists(kLuaLibrary)) {
        SKIP("system-wide libtree-sitter-lua.so not found on this machine");
    }
    REQUIRE_THROWS_AS(LoadDynamicLanguage(kLuaLibrary, "not_a_real_symbol_suffix"), std::runtime_error);
}

TEST_CASE("LoadDynamicLanguage loads a real system grammar and it parses", "[TreeSitter][Dynamic]") {
    if (!std::filesystem::exists(kLuaLibrary)) {
        SKIP("system-wide libtree-sitter-lua.so not found on this machine");
    }

    const Language language = LoadDynamicLanguage(kLuaLibrary, "lua");
    REQUIRE(language.Raw() != nullptr);

    Parser parser(language);
    Tree   tree = parser.Parse("local x = 1");
    REQUIRE_FALSE(tree.IsNull());
    REQUIRE_FALSE(tree.RootNode().IsNull());
}
