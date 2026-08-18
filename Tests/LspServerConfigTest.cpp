#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Lsp/LspServerConfig.h"

using ned::editor::lsp::LspServerCommand;
using ned::editor::lsp::SetLspServerCommand;

TEST_CASE("LspServerCommand is nullopt for a language nothing was ever configured for", "[Lsp]") {
    REQUIRE_FALSE(LspServerCommand("a-language-nobody-configured").has_value());
}

TEST_CASE("SetLspServerCommand registers a command retrievable by language name", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-c", {"clangd"});

    const auto command = LspServerCommand("lsp-server-config-test-c");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"clangd"});

    SetLspServerCommand("lsp-server-config-test-c", {}); // cleanup -- process-wide state
}

TEST_CASE("SetLspServerCommand stores multi-argument commands in order", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-python", {"pyright-langserver", "--stdio"});

    const auto command = LspServerCommand("lsp-server-config-test-python");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"pyright-langserver", "--stdio"});

    SetLspServerCommand("lsp-server-config-test-python", {}); // cleanup
}

TEST_CASE("Re-registering a language's command overwrites the previous one", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-overwrite", {"first-server"});
    SetLspServerCommand("lsp-server-config-test-overwrite", {"second-server"});

    const auto command = LspServerCommand("lsp-server-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-server"});

    SetLspServerCommand("lsp-server-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-clear", {"some-server"});
    REQUIRE(LspServerCommand("lsp-server-config-test-clear").has_value());

    SetLspServerCommand("lsp-server-config-test-clear", {});
    REQUIRE_FALSE(LspServerCommand("lsp-server-config-test-clear").has_value());
}
