#include <catch2/catch_test_macros.hpp>

#include "Editor/SnippetRegistry.h"

using ned::editor::ClearAllSnippets;
using ned::editor::RegisterSnippet;
using ned::editor::SnippetBodyForTrigger;
using ned::editor::SnippetTriggers;

namespace {

// The registry is process-wide static state -- every test scopes its
// registrations so nothing leaks across tests (FormatCommandGuard's
// precedent in CommandsTest.cpp).
struct SnippetRegistryGuard {
    SnippetRegistryGuard() {
        ClearAllSnippets();
    }
    ~SnippetRegistryGuard() {
        ClearAllSnippets();
    }
};

} // namespace

TEST_CASE("SnippetRegistry registers and looks up a trigger", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "for (${1:i};)");
    REQUIRE(SnippetBodyForTrigger("cpp", "for") == "for (${1:i};)");
    REQUIRE(!SnippetBodyForTrigger("cpp", "while").has_value());
    REQUIRE(!SnippetBodyForTrigger("python", "for").has_value());
}

TEST_CASE("SnippetRegistry falls back to the global tier", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "todo", "TODO: $0");
    REQUIRE(SnippetBodyForTrigger("cpp", "todo") == "TODO: $0");
    REQUIRE(SnippetBodyForTrigger("", "todo") == "TODO: $0");
}

TEST_CASE("SnippetRegistry lets a language-specific trigger shadow the global one", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "main", "global");
    RegisterSnippet("cpp", "main", "int main() { $0 }");
    REQUIRE(SnippetBodyForTrigger("cpp", "main") == "int main() { $0 }");
    REQUIRE(SnippetBodyForTrigger("python", "main") == "global");
}

TEST_CASE("SnippetRegistry overwrites on re-register and clears on empty body", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "v1");
    RegisterSnippet("cpp", "for", "v2");
    REQUIRE(SnippetBodyForTrigger("cpp", "for") == "v2");
    RegisterSnippet("cpp", "for", "");
    REQUIRE(!SnippetBodyForTrigger("cpp", "for").has_value());
}

TEST_CASE("SnippetRegistry rejects an empty trigger", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    REQUIRE_THROWS_AS(RegisterSnippet("cpp", "", "body"), std::runtime_error);
}

TEST_CASE("SnippetTriggers merges language-specific and global tiers sorted", "[SnippetRegistry]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "a");
    RegisterSnippet("cpp", "while", "b");
    RegisterSnippet("", "todo", "c");
    RegisterSnippet("", "for", "d"); // shadowed, still just one "for" listed
    const std::vector<std::string> triggers = SnippetTriggers("cpp");
    REQUIRE(triggers == std::vector<std::string>{"for", "todo", "while"});
    REQUIRE(SnippetTriggers("python") == std::vector<std::string>{"for", "todo"});
    REQUIRE(SnippetTriggers("") == std::vector<std::string>{"for", "todo"});
}
