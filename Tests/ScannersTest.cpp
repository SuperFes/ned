#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/BundledLanguages.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Languages/Scanners/Scanners.h"
#include "Editor/Parse/Abi.h"

using ned::editor::languages::scanners::FindBundledScanner;

namespace {

const ned::editor::parse::abi::LanguageData* Tables(const ned::editor::grammar::Language& language) {
    return language.Raw();
}

} // namespace

// The generated parser.c files still linked reach their scanners through
// tree-sitter's C symbol names, which only the ported files define now
// (no scanner.c is compiled), so linking at all proves they run the ports;
// this holds the registry to the same set.
TEST_CASE("Every bundled grammar with external tokens has its scanner in the registry", "[Scanners]") {
    std::size_t withScanner = 0;
    for (const ned::editor::LanguageDefinition& definition : ned::editor::BundledLanguages()) {
        if (definition.grammarless)
            continue;
        const std::string grammar = definition.grammar.empty() ? definition.name : definition.grammar;
        if (grammar != definition.name)
            continue; // jank/clojure, tsx/typescript borrow a grammar; the grammar's own definition covers it
        INFO("language: " << definition.name);
        const auto language = ned::editor::grammar::LanguageByName(definition.name);
        REQUIRE(language.has_value());
        const auto* data    = Tables(*language);
        const auto* scanner = FindBundledScanner(definition.name);
        if (data->externalTokenCount == 0) {
            CHECK(scanner == nullptr);
            continue;
        }
        ++withScanner;
        REQUIRE(scanner != nullptr);
        CHECK(scanner->create != nullptr);
        CHECK(scanner->scan != nullptr);
        CHECK(data->externalScanner.scan != nullptr);
    }
    CHECK(withScanner == 29);
    CHECK(FindBundledScanner("no-such-language") == nullptr);
}

TEST_CASE("A scanner's state round-trips through its serialization buffer", "[Scanners]") {
    for (const char* name : {"bash", "python", "markdown", "yaml", "ruby"}) {
        INFO("language: " << name);
        const auto* scanner = FindBundledScanner(name);
        REQUIRE(scanner != nullptr);
        void* payload = scanner->create();
        REQUIRE(payload != nullptr);
        char           buffer[ned::editor::parse::abi::kSerializationBufferSize];
        const unsigned length = scanner->serialize(payload, buffer);
        CHECK(length <= sizeof(buffer));
        scanner->deserialize(payload, buffer, length);
        scanner->deserialize(payload, nullptr, 0);
        scanner->destroy(payload);
    }
}
