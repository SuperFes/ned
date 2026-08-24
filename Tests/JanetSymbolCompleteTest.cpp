#include <catch2/catch_test_macros.hpp>

#include "Editor/JanetSymbolComplete.h"

using ned::editor::JanetSymbolPrefixStart;

TEST_CASE("Stops at whitespace, keeping the whole binding name", "[JanetSymbolComplete]") {
    const std::string content = "(ned/regist";
    CHECK(JanetSymbolPrefixStart(content, content.size()) == 1); // just past '('
}

TEST_CASE("A hyphen inside a name doesn't end the prefix", "[JanetSymbolComplete]") {
    const std::string content = "backward-de";
    CHECK(JanetSymbolPrefixStart(content, content.size()) == 0);
}

TEST_CASE("A slash inside a name doesn't end the prefix", "[JanetSymbolComplete]") {
    const std::string content = "ned/set-tab-wid";
    CHECK(JanetSymbolPrefixStart(content, content.size()) == 0);
}

TEST_CASE("Point at the very start yields an empty prefix", "[JanetSymbolComplete]") {
    const std::string content = "ned/insert";
    CHECK(JanetSymbolPrefixStart(content, 0) == 0);
}

TEST_CASE("A leading paren is not part of the symbol", "[JanetSymbolComplete]") {
    const std::string content = "(ned/insert \"x\")\n(ned/for";
    const std::size_t point   = content.size();
    CHECK(JanetSymbolPrefixStart(content, point) == 18); // just past the second '('
}
