#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Lsp/ConnectionKey.h"

using ned::editor::lsp::ConnectionHost;
using ned::editor::lsp::ConnectionServer;
using ned::editor::lsp::MakeConnectionKey;

TEST_CASE("The project root's local connection is keyed by its server alone", "[Lsp]") {
    const std::string key = MakeConnectionKey(std::nullopt, "/work/app", "/work/app", "cpp");

    REQUIRE(key == "cpp");
    REQUIRE(ConnectionHost(key) == std::nullopt);
    REQUIRE(ConnectionServer(key) == "cpp");
}

TEST_CASE("Another local root's connection names its root", "[Lsp]") {
    const std::string key = MakeConnectionKey(std::nullopt, "/work/app/sub", "/work/app", "python");

    REQUIRE(key == "/work/app/sub\x1fpython");
    REQUIRE(ConnectionHost(key) == std::nullopt);
    REQUIRE(ConnectionServer(key) == "python");
}

TEST_CASE("A host's connection names the host, even for a root matching the local project root", "[Lsp]") {
    const std::string key = MakeConnectionKey(std::string("me@box"), "/work/app", "/work/app", "cpp");

    REQUIRE(key != "cpp");
    REQUIRE(key != MakeConnectionKey(std::string("other"), "/work/app", "/work/app", "cpp"));
    REQUIRE(ConnectionHost(key) == "me@box");
    REQUIRE(ConnectionServer(key) == "cpp");
}
