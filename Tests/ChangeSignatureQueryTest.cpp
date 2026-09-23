#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "Editor/Mode.h"

using ned::editor::CallMarker;
using ned::editor::CppMode;
using ned::editor::FundamentalMode;
using ned::editor::JsonMode;
using ned::editor::SignatureMarker;
using ned::editor::SignatureParameter;

namespace {

std::string_view Slice(std::string_view text, std::size_t start, std::size_t end) {
    return text.substr(start, end - start);
}

} // namespace

TEST_CASE("Modes with no signatures/calls query configured have empty functions", "[ChangeSignature]") {
    CHECK_FALSE(static_cast<bool>(FundamentalMode().signatures));
    CHECK_FALSE(static_cast<bool>(FundamentalMode().calls));
    CHECK_FALSE(static_cast<bool>(JsonMode().signatures));
    CHECK_FALSE(static_cast<bool>(JsonMode().calls));
}

TEST_CASE("CppMode signatures finds a free function's parameters", "[ChangeSignature]") {
    const auto  mode = CppMode();
    REQUIRE(static_cast<bool>(mode.signatures));

    const std::string text = "int add(int a, int b) {\n"
                             "    return a + b;\n"
                             "}\n";
    const auto markers = mode.signatures(text);
    REQUIRE(markers.size() == 1);
    const SignatureMarker& marker = markers[0];
    CHECK(Slice(text, marker.nameStartByte, marker.nameEndByte) == "add");
    CHECK(marker.startByte == 0);
    REQUIRE(marker.parameters.size() == 2);
    CHECK(Slice(text, marker.parameters[0].nameStartByte, marker.parameters[0].nameEndByte) == "a");
    CHECK_FALSE(marker.parameters[0].hasDefaultValue);
    CHECK_FALSE(marker.parameters[0].isVariadic);
    CHECK(Slice(text, marker.parameters[1].nameStartByte, marker.parameters[1].nameEndByte) == "b");
}

TEST_CASE("CppMode signatures captures default values and a trailing bare ellipsis", "[ChangeSignature]") {
    const auto  mode = CppMode();
    const std::string text = "void log(const char* fmt, int level = 0, ...) {\n"
                             "}\n";
    const auto markers = mode.signatures(text);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].parameters.size() == 3);

    const SignatureParameter& fmt = markers[0].parameters[0];
    CHECK(Slice(text, fmt.nameStartByte, fmt.nameEndByte) == "fmt");
    CHECK_FALSE(fmt.hasDefaultValue);

    const SignatureParameter& level = markers[0].parameters[1];
    CHECK(Slice(text, level.nameStartByte, level.nameEndByte) == "level");
    REQUIRE(level.hasDefaultValue);
    CHECK(Slice(text, level.defaultStartByte, level.defaultEndByte) == "0");

    const SignatureParameter& ellipsis = markers[0].parameters[2];
    CHECK(ellipsis.isVariadic);
}

TEST_CASE("CppMode signatures reports a variadic_parameter_declaration parameter as variadic", "[ChangeSignature]") {
    const auto  mode = CppMode();
    const std::string text = "void trace(int code, ... args) {\n"
                             "}\n";
    const auto markers = mode.signatures(text);
    REQUIRE(markers.size() == 1);
    REQUIRE(markers[0].parameters.size() == 2);
    CHECK(markers[0].parameters[1].isVariadic);
}

TEST_CASE("CppMode signatures collapses a with-body method's narrow/wide double match", "[ChangeSignature]") {
    const auto  mode = CppMode();
    const std::string text = "struct Widget {\n"
                             "    void resize(int w, int h) {\n"
                             "        width = w;\n"
                             "        height = h;\n"
                             "    }\n"
                             "};\n";
    const auto markers = mode.signatures(text);
    REQUIRE(markers.size() == 1);
    CHECK(Slice(text, markers[0].nameStartByte, markers[0].nameEndByte) == "resize");
    // The wider (with-body) range must be the one that survives, not the
    // bare declarator's own narrower one -- endByte reaches past the body's
    // own closing brace, not just the parameter list.
    CHECK(markers[0].endByte > text.find("h;\n    }") );
}

TEST_CASE("CppMode signatures finds a bodyless prototype and its out-of-line definition as two markers",
          "[ChangeSignature]") {
    const auto  mode = CppMode();
    const std::string text = "struct Widget {\n"
                             "    void resize(int w, int h);\n"
                             "};\n"
                             "\n"
                             "void Widget::resize(int w, int h) {\n"
                             "    width = w;\n"
                             "    height = h;\n"
                             "}\n";
    const auto markers = mode.signatures(text);
    REQUIRE(markers.size() == 2);
    CHECK(Slice(text, markers[0].nameStartByte, markers[0].nameEndByte) == "resize");
    CHECK(Slice(text, markers[1].nameStartByte, markers[1].nameEndByte) == "resize");
    // The prototype has no body -- its own range ends at the declarator's
    // closing ')', well before the out-of-line definition even starts.
    CHECK(markers[0].endByte < text.find("void Widget::resize"));
    // The out-of-line definition's range reaches past its own body.
    CHECK(markers[1].endByte > text.rfind("height = h;"));
}

TEST_CASE("CppMode calls finds bare, qualified and member call expressions with their own argument ranges",
          "[ChangeSignature]") {
    const auto  mode = CppMode();
    REQUIRE(static_cast<bool>(mode.calls));

    const std::string text = "void run() {\n"
                             "    add(1, 2);\n"
                             "    Math::add(3, 4);\n"
                             "    obj.add(5, 6);\n"
                             "    ptr->add(7, foo(8, 9));\n"
                             "}\n";
    const auto markers = mode.calls(text);
    // Five calls total: the four written explicitly plus foo(8, 9), nested
    // inside ptr->add(...)'s own argument list.
    REQUIRE(markers.size() == 5);

    std::vector<std::string> callees;
    for (const CallMarker& marker : markers) {
        callees.emplace_back(Slice(text, marker.calleeStartByte, marker.calleeEndByte));
    }
    CHECK(callees == std::vector<std::string>{"add", "add", "add", "add", "foo"});

    // ptr->add(7, foo(8, 9)) is the fourth call in source order -- its own
    // argument list has exactly two top-level arguments, the nested call's
    // internal comma must not be counted as a third.
    const CallMarker& ptrAdd = markers[3];
    REQUIRE(ptrAdd.arguments.size() == 2);
    CHECK(Slice(text, ptrAdd.arguments[0].startByte, ptrAdd.arguments[0].endByte) == "7");
    CHECK(Slice(text, ptrAdd.arguments[1].startByte, ptrAdd.arguments[1].endByte) == "foo(8, 9)");
}
