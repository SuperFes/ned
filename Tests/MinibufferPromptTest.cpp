#include <catch2/catch_test_macros.hpp>

#include "Editor/MinibufferPrompt.h"

using ned::editor::MinibufferPrompt;

TEST_CASE("A fresh MinibufferPrompt has empty text and shows only its label", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    REQUIRE(prompt.Text().empty());
    REQUIRE(prompt.StatusText() == "Find file: ");
    REQUIRE(prompt.CursorByteOffset() == 0);
}

TEST_CASE("InsertChar accumulates typed characters into Text and StatusText", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Switch to buffer: ");

    prompt.InsertChar(U'a');
    prompt.InsertChar(U'b');
    prompt.InsertChar(U'c');

    REQUIRE(prompt.Text() == "abc");
    REQUIRE(prompt.StatusText() == "Switch to buffer: abc");
    REQUIRE(prompt.CursorByteOffset() == 3);
}

TEST_CASE("DeleteBackward removes the codepoint before the cursor, and is a no-op at the start", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    prompt.DeleteBackward(); // no-op, nothing typed yet
    REQUIRE(prompt.Text().empty());

    prompt.InsertChar(U'a');
    prompt.InsertChar(U'b');
    prompt.DeleteBackward();

    REQUIRE(prompt.Text() == "a");
}

TEST_CASE("InsertChar handles multi-byte codepoints correctly", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");

    prompt.InsertChar(U'é'); // 'e' with acute accent, 2 UTF-8 bytes
    REQUIRE(prompt.Text() == "\xc3\xa9");

    prompt.DeleteBackward();
    REQUIRE(prompt.Text().empty());
}

TEST_CASE("SetText wholesale-replaces the text and moves the cursor to the end, for completion", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("Find file: ");

    prompt.InsertChar(U'a');
    prompt.SetText("/tmp/ned-san");

    REQUIRE(prompt.Text() == "/tmp/ned-san");
    REQUIRE(prompt.StatusText() == "Find file: /tmp/ned-san");
    REQUIRE(prompt.CursorByteOffset() == 12);

    prompt.DeleteBackward();
    REQUIRE(prompt.Text() == "/tmp/ned-sa");
}

TEST_CASE("MoveCursorLeft/Right reposition the cursor without editing text", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");
    prompt.SetText("abc");
    REQUIRE(prompt.CursorByteOffset() == 3);

    prompt.MoveCursorLeft();
    prompt.MoveCursorLeft();
    REQUIRE(prompt.CursorByteOffset() == 1);

    prompt.MoveCursorLeft();
    prompt.MoveCursorLeft(); // already at the start -- no-op
    REQUIRE(prompt.CursorByteOffset() == 0);

    prompt.MoveCursorRight();
    REQUIRE(prompt.CursorByteOffset() == 1);

    prompt.MoveCursorToEnd();
    REQUIRE(prompt.CursorByteOffset() == 3);
    prompt.MoveCursorRight(); // already at the end -- no-op
    REQUIRE(prompt.CursorByteOffset() == 3);

    prompt.MoveCursorToStart();
    REQUIRE(prompt.CursorByteOffset() == 0);
}

TEST_CASE("InsertChar inserts at the cursor, not always at the end", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");
    prompt.SetText("ac");
    prompt.MoveCursorLeft(); // between 'a' and 'c'

    prompt.InsertChar(U'b');

    REQUIRE(prompt.Text() == "abc");
    REQUIRE(prompt.CursorByteOffset() == 2); // just past the inserted 'b'
}

TEST_CASE("DeleteForward removes the codepoint at the cursor, and is a no-op at the end", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");
    prompt.SetText("abc");

    prompt.DeleteForward(); // already at the end -- no-op
    REQUIRE(prompt.Text() == "abc");

    prompt.MoveCursorToStart();
    prompt.DeleteForward();

    REQUIRE(prompt.Text() == "bc");
    REQUIRE(prompt.CursorByteOffset() == 0);
}

TEST_CASE("DeleteBackward/DeleteForward handle multi-byte codepoints as a single unit", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("");
    prompt.SetText("a\xc3\xa9z"); // "a", U+00E9, "z"

    prompt.MoveCursorToStart();
    prompt.MoveCursorRight(); // past 'a', cursor now right before the 2-byte é
    prompt.DeleteForward();
    REQUIRE(prompt.Text() == "az");

    prompt.SetText("a\xc3\xa9z");
    prompt.MoveCursorToEnd();
    prompt.MoveCursorLeft(); // past 'z', cursor now right after the 2-byte é
    prompt.DeleteBackward();
    REQUIRE(prompt.Text() == "az");
}

TEST_CASE("CursorDisplayColumn counts codepoints across label and text, one column each", "[MinibufferPrompt]") {
    MinibufferPrompt prompt("> ");
    prompt.InsertChar(U'é'); // 2 UTF-8 bytes, 1 display column
    prompt.InsertChar(U'x');

    REQUIRE(prompt.CursorDisplayColumn() == 4); // label "> " (2 columns) + 'é' + 'x' (1 column each)

    prompt.MoveCursorToStart();
    REQUIRE(prompt.CursorDisplayColumn() == 2); // right after the label, before any text
}
