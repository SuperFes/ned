#include <catch2/catch_test_macros.hpp>

#include "Editor/PointerGraphNode.h"

using ned::editor::FormatPointerGraphLabel;
using ned::editor::PointerGraphNode;

TEST_CASE("FormatPointerGraphLabel formats name, type, and value", "[PointerGraphNode]") {
    PointerGraphNode node{.name = "head", .type = "Node *", .value = "0x1000"};
    REQUIRE(FormatPointerGraphLabel(node) == "head: Node * = 0x1000");
}

TEST_CASE("FormatPointerGraphLabel omits the type when empty", "[PointerGraphNode]") {
    PointerGraphNode node{.name = "x", .value = "42"};
    REQUIRE(FormatPointerGraphLabel(node) == "x = 42");
}

TEST_CASE("FormatPointerGraphLabel appends a cycle marker when cyclic", "[PointerGraphNode]") {
    PointerGraphNode node{.name = "next", .type = "Node *", .value = "0x1000", .cyclic = true};
    REQUIRE(FormatPointerGraphLabel(node) == "next: Node * = 0x1000 (cycle)");
}
