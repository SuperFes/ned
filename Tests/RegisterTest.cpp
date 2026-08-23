#include <catch2/catch_test_macros.hpp>

#include "Editor/Register.h"

using ned::editor::PointRegisterValue;
using ned::editor::RegisterTable;

TEST_CASE("An unset register returns nullptr for both Point and Text", "[Register]") {
    RegisterTable table;

    REQUIRE(table.Point(U'a') == nullptr);
    REQUIRE(table.Text(U'a') == nullptr);
}

TEST_CASE("SetPoint/Point round-trips a point register", "[Register]") {
    RegisterTable table;

    table.SetPoint(U'a', "scratch", {42});

    const PointRegisterValue* value = table.Point(U'a');
    REQUIRE(value != nullptr);
    REQUIRE(value->bufferName == "scratch");
    REQUIRE(value->byteOffsets == std::vector<std::size_t>{42});
    REQUIRE(table.Text(U'a') == nullptr); // wrong kind
}

TEST_CASE("SetText/Text round-trips a text register", "[Register]") {
    RegisterTable table;

    table.SetText(U'b', "hello world");

    const std::string* text = table.Text(U'b');
    REQUIRE(text != nullptr);
    REQUIRE(*text == "hello world");
    REQUIRE(table.Point(U'b') == nullptr); // wrong kind
}

TEST_CASE("Setting a register to a new kind overwrites the old one cleanly", "[Register]") {
    RegisterTable table;

    table.SetPoint(U'a', "scratch", {5});
    REQUIRE(table.Point(U'a') != nullptr);

    table.SetText(U'a', "now text");
    REQUIRE(table.Point(U'a') == nullptr);
    REQUIRE(table.Text(U'a') != nullptr);
    REQUIRE(*table.Text(U'a') == "now text");

    table.SetPoint(U'a', "other", {9});
    REQUIRE(table.Text(U'a') == nullptr);
    REQUIRE(table.Point(U'a') != nullptr);
    REQUIRE(table.Point(U'a')->bufferName == "other");
}

TEST_CASE("Distinct register names, including a non-ASCII one, don't collide", "[Register]") {
    RegisterTable table;

    table.SetText(U'a', "alpha");
    table.SetText(U'b', "beta");
    table.SetText(U'é', "e-acute"); // 'é', a real non-ASCII codepoint

    REQUIRE(*table.Text(U'a') == "alpha");
    REQUIRE(*table.Text(U'b') == "beta");
    REQUIRE(*table.Text(U'é') == "e-acute");
}

// multi-cursor-register follow-up.

TEST_CASE("SetPoint with multiple offsets round-trips every one, primary first", "[Register]") {
    RegisterTable table;

    table.SetPoint(U'a', "scratch", {10, 20, 30});

    const PointRegisterValue* value = table.Point(U'a');
    REQUIRE(value != nullptr);
    REQUIRE(value->byteOffsets == std::vector<std::size_t>{10, 20, 30});
}

TEST_CASE("SetText is equivalent to SetTextPieces with a single piece", "[Register]") {
    RegisterTable table;

    table.SetText(U'a', "solo");
    REQUIRE(*table.TextPieces(U'a') == std::vector<std::string>{"solo"});
    REQUIRE(*table.Text(U'a') == "solo");
}

TEST_CASE("SetTextPieces stores multiple pieces, joined by newlines for Text()", "[Register]") {
    RegisterTable table;

    table.SetTextPieces(U'a', {"foo", "bar"});
    REQUIRE(*table.TextPieces(U'a') == std::vector<std::string>{"foo", "bar"});
    REQUIRE(*table.Text(U'a') == "foo\nbar");
}
