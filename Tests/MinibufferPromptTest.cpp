#include <catch2/catch_test_macros.hpp>

#include "Editor/MinibufferPrompt.h"

using ned::editor::MinibufferPrompt;

TEST_CASE("A fresh MinibufferPrompt has empty text and shows only its label", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    REQUIRE(prompt.Text().empty());
    REQUIRE(prompt.StatusText() == "Find file: ");
}

TEST_CASE("AppendChar accumulates typed characters into Text and StatusText", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Switch to buffer: ");

    prompt.AppendChar(U'a');
    prompt.AppendChar(U'b');
    prompt.AppendChar(U'c');

    REQUIRE(prompt.Text() == "abc");
    REQUIRE(prompt.StatusText() == "Switch to buffer: abc");
}

TEST_CASE("DeleteChar removes the last codepoint, and is a no-op on empty text", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    prompt.DeleteChar(); // no-op, nothing typed yet
    REQUIRE(prompt.Text().empty());

    prompt.AppendChar(U'a');
    prompt.AppendChar(U'b');
    prompt.DeleteChar();

    REQUIRE(prompt.Text() == "a");
}

TEST_CASE("AppendChar handles multi-byte codepoints correctly", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");

    prompt.AppendChar(U'é'); // 'e' with acute accent, 2 UTF-8 bytes
    REQUIRE(prompt.Text() == "\xc3\xa9");

    prompt.DeleteChar();
    REQUIRE(prompt.Text().empty());
}

TEST_CASE("SetText wholesale-replaces the text, for completion", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    prompt.AppendChar(U'a');
    prompt.SetText("/tmp/ned-san");

    REQUIRE(prompt.Text() == "/tmp/ned-san");
    REQUIRE(prompt.StatusText() == "Find file: /tmp/ned-san");

    prompt.DeleteChar();
    REQUIRE(prompt.Text() == "/tmp/ned-sa");
}
