#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Lsp/ServerConfig.h"

using ned::editor::lsp::FormatOnSaveEnabled;
using ned::editor::lsp::ServerCommand;
using ned::editor::lsp::SignatureHelpAutoTriggerEnabled;
using ned::editor::lsp::SetLspFormatOnSaveEnabled;
using ned::editor::lsp::SetLspServerCommand;
using ned::editor::lsp::SetLspSignatureHelpAutoTriggerEnabled;

TEST_CASE("ServerCommand is nullopt for a language nothing was ever configured for", "[Lsp]") {
    REQUIRE_FALSE(ServerCommand("a-language-nobody-configured").has_value());
}

TEST_CASE("SetLspServerCommand registers a command retrievable by language name", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-c", {"clangd"});

    const auto command = ServerCommand("lsp-server-config-test-c");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"clangd"});

    SetLspServerCommand("lsp-server-config-test-c", {}); // cleanup -- process-wide state
}

TEST_CASE("SetLspServerCommand stores multi-argument commands in order", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-python", {"pyright-langserver", "--stdio"});

    const auto command = ServerCommand("lsp-server-config-test-python");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"pyright-langserver", "--stdio"});

    SetLspServerCommand("lsp-server-config-test-python", {}); // cleanup
}

TEST_CASE("Re-registering a language's command overwrites the previous one", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-overwrite", {"first-server"});
    SetLspServerCommand("lsp-server-config-test-overwrite", {"second-server"});

    const auto command = ServerCommand("lsp-server-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-server"});

    SetLspServerCommand("lsp-server-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Lsp]") {
    SetLspServerCommand("lsp-server-config-test-clear", {"some-server"});
    REQUIRE(ServerCommand("lsp-server-config-test-clear").has_value());

    SetLspServerCommand("lsp-server-config-test-clear", {});
    REQUIRE_FALSE(ServerCommand("lsp-server-config-test-clear").has_value());
}

TEST_CASE("SignatureHelpAutoTriggerEnabled defaults to true and round-trips through the setter", "[Lsp]") {
    REQUIRE(SignatureHelpAutoTriggerEnabled()); // default, per ServerConfig.h's own doc comment

    SetLspSignatureHelpAutoTriggerEnabled(false);
    REQUIRE_FALSE(SignatureHelpAutoTriggerEnabled());

    SetLspSignatureHelpAutoTriggerEnabled(true); // restore -- process-wide state
    REQUIRE(SignatureHelpAutoTriggerEnabled());
}

TEST_CASE("FormatOnSaveEnabled defaults to false and round-trips through the setter", "[Lsp]") {
    REQUIRE_FALSE(FormatOnSaveEnabled()); // default, per ServerConfig.h's own doc comment (opt-in)

    SetLspFormatOnSaveEnabled(true);
    REQUIRE(FormatOnSaveEnabled());

    SetLspFormatOnSaveEnabled(false); // restore -- process-wide state
    REQUIRE_FALSE(FormatOnSaveEnabled());
}
