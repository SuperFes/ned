#ifndef NED_TESTS_POINTERSHAPETESTSUPPORT_H
#define NED_TESTS_POINTERSHAPETESTSUPPORT_H

#include "TestEvents.h"
#include "UI/PointerShape.h"
#include "UI/Widget.h"

namespace ned::ui::test {

// A buttonless motion report, as Notcurses decodes one.
[[nodiscard]] inline Event Hover(int x, int y) {
    return Mouse(x, y, MouseEvent::Button::None, MouseEvent::Motion::Released);
}

// The pointer shape after widget handles event, the way EventLoop brackets
// every mouse event it dispatches.
inline PointerShape PointerAfter(Widget& widget, const Event& event, bool buttonHeld = false) {
    BeginPointerEvent(buttonHeld);
    widget.OnEvent(event);
    CommitPointerShape();
    return CurrentPointerShape();
}

} // namespace ned::ui::test

#endif // NED_TESTS_POINTERSHAPETESTSUPPORT_H
