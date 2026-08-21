//
// A one-row Emacs-style mode line: buffer name and line:column position,
// recomputed fresh every paint() call (no external synchronization needed).
//

#ifndef NED_UI_MODELINE_H
#define NED_UI_MODELINE_H

#include <functional>

#include "ActiveBuffer.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ModeLine : public Widget {
  public:
    // activeBuffer, mode, and theme must outlive this ModeLine. mode is the
    // same Mode main.cpp picks once at startup and never rebinds per-buffer
    // (see BufferView's own entry in CLAUDE.md) -- shown here so it's
    // visible somewhere, mode-overrides follow-up, matching Emacs' own mode
    // line convention of naming the active major mode.
    ModeLine(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme);

    void Paint(Canvas c) override;

    // Chrome-redesign follow-up: when set and returning true, the gradient
    // uses the theme's modeLineFocusedGradientStart/End (the accent-tinted
    // pair) instead of the plain one -- the focus signal for which pane has
    // the keyboard. Unset (the default, every pre-existing construction
    // site and test) means never focused, i.e. the plain gradient. Pane's
    // ctor wires this to its own BufferView's Widget::Focused().
    void SetFocusProvider(std::function<bool()> provider);

  private:
    const ActiveBuffer&   activeBuffer_;
    const editor::Mode&   mode_;
    const Theme&          theme_;
    std::function<bool()> focusProvider_;
};

} // namespace ned::ui

#endif // NED_UI_MODELINE_H
