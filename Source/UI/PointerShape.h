//
// The mouse pointer's shape over ned, through OSC 22 (kitty's pointer-shape
// protocol, also in Konsole, foot, Ghostty and xterm). A terminal without it
// ignores the sequence, so nothing probes for support first: a probe's reply
// would arrive through Notcurses' input as stray keys.
//
// Widgets get no enter/leave events, so the shape is claimed per mouse event:
// EventLoop calls BeginPointerEvent before dispatching one, any widget whose
// resize divider is under the pointer (or mid-drag) calls RequestPointerShape,
// and CommitPointerShape afterwards writes the sequence only if the shape
// changed. A pointer that moves off a divider therefore falls back to Default
// without anyone noticing it left. Main-thread only, like event dispatch.
// Editor/PointerShapeSettings.h turns the whole thing off.
//

#ifndef NED_UI_POINTERSHAPE_H
#define NED_UI_POINTERSHAPE_H

#include <string>

namespace ned::ui {

enum class PointerShape { Default,
                          ColumnResize, // a vertical bar dragged left/right
                          RowResize };  // a horizontal bar dragged up/down

// The OSC 22 sequence selecting shape, by its CSS cursor name.
[[nodiscard]] std::string BuildPointerShapeSequence(PointerShape shape, bool wrapForTmux);

// buttonHeld keeps the current shape as the default for this event, so a drag
// that strays off its divider keeps the resize pointer until release.
void BeginPointerEvent(bool buttonHeld);
void RequestPointerShape(PointerShape shape);
void CommitPointerShape();
// Back to Default now, e.g. before suspending or exiting.
void                       ResetPointerShape();
[[nodiscard]] PointerShape CurrentPointerShape();

} // namespace ned::ui

#endif // NED_UI_POINTERSHAPE_H
