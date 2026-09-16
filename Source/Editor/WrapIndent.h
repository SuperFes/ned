//
// Whether a soft-wrapped line's continuation rows hang under its own
// leading whitespace, rather than restarting flush at the gutter's own
// left edge -- the on-screen counterpart to a hard-wrapped paragraph's own
// hang indent (see Fill.h's list-marker-aware fill-paragraph), applied
// uniformly to every wrapped line regardless of its content (no
// list-marker detection here -- a soft wrap can land anywhere in the
// middle of a sentence, so "match the line's own leading whitespace" is
// the only rule general enough to always make sense). Process-wide,
// mutex-guarded static state, mirroring FinalNewline.h's
// SetEnsureFinalNewline/EnsureFinalNewline bool pattern exactly. Default
// on -- purely a rendering choice with no effect on buffer content, undo,
// or any other subsystem, so there is no real downside to enabling it
// unconditionally the way there can be for a content-affecting default.
// Configured from Janet via ned/set-wrap-indent.
//

#ifndef NED_EDITOR_WRAPINDENT_H
#define NED_EDITOR_WRAPINDENT_H

namespace ned::editor {

void               SetWrapIndent(bool enabled);
[[nodiscard]] bool WrapIndent();

} // namespace ned::editor

#endif // NED_EDITOR_WRAPINDENT_H
