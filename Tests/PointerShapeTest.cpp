#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "Editor/PointerShapeSettings.h"
#include "UI/PointerShape.h"

using ned::ui::BeginPointerEvent;
using ned::ui::BuildPointerShapeSequence;
using ned::ui::CommitPointerShape;
using ned::ui::CurrentPointerShape;
using ned::ui::PointerShape;
using ned::ui::RequestPointerShape;
using ned::ui::ResetPointerShape;

namespace {

void Event(bool buttonHeld, std::optional<PointerShape> requested = std::nullopt) {
    BeginPointerEvent(buttonHeld);
    if (requested) {
        RequestPointerShape(*requested);
    }
    CommitPointerShape();
}

} // namespace

TEST_CASE("BuildPointerShapeSequence names each shape by its CSS cursor", "[PointerShape]") {
    CHECK(BuildPointerShapeSequence(PointerShape::ColumnResize, false) == "\x1b]22;ew-resize\x1b\\");
    CHECK(BuildPointerShapeSequence(PointerShape::RowResize, false) == "\x1b]22;ns-resize\x1b\\");
    CHECK(BuildPointerShapeSequence(PointerShape::Default, false) == "\x1b]22;default\x1b\\");
    CHECK(BuildPointerShapeSequence(PointerShape::Default, true) == "\x1bPtmux;\x1b\x1b]22;default\x1b\x1b\\\x1b\\");
}

TEST_CASE("The pointer takes a shape while an event claims it and drops it when none does", "[PointerShape]") {
    ResetPointerShape();
    Event(false, PointerShape::ColumnResize);
    CHECK(CurrentPointerShape() == PointerShape::ColumnResize);
    Event(false);
    CHECK(CurrentPointerShape() == PointerShape::Default);
}

TEST_CASE("A held button keeps the pointer's shape off the divider until release", "[PointerShape]") {
    ResetPointerShape();
    Event(false, PointerShape::RowResize); // hover
    Event(true);                           // drag strays off the divider
    CHECK(CurrentPointerShape() == PointerShape::RowResize);
    Event(false); // release elsewhere
    CHECK(CurrentPointerShape() == PointerShape::Default);
}

TEST_CASE("Turning pointer shapes off restores the default on the next event and ignores claims", "[PointerShape]") {
    ResetPointerShape();
    Event(false, PointerShape::ColumnResize);
    ned::editor::SetPointerShapesEnabled(false);
    Event(true, PointerShape::ColumnResize);
    CHECK(CurrentPointerShape() == PointerShape::Default);
    ned::editor::SetPointerShapesEnabled(true);
}
