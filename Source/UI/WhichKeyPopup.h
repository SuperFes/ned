//
// which-key follow-up: a small, non-focusable OverlayHost popup listing the
// possible next chords while a prefix key (C-x, C-c, ...) is pending -- the
// real Emacs which-key package's own shape. Purely a renderer: BufferView
// computes the WhichKeyHint (via Dispatcher::Pending()/Keymaps()) and
// main.cpp's composition shows/hides this widget through OverlayHost in
// response, the same "Set*/register-then-connect" wiring every other
// cross-widget dependency in this codebase follows. Deliberately
// non-focusable (Widget::Focusable()'s own default) so it never steals
// keyboard input away from the pane that's mid-sequence -- keys keep
// flowing to Dispatcher::Feed exactly as if this weren't shown at all.
//

#ifndef NED_UI_WHICHKEYPOPUP_H
#define NED_UI_WHICHKEYPOPUP_H

#include "Theme.h"
#include "Widget.h"
#include "WhichKeyHint.h"

namespace ned::ui {

class WhichKeyPopup : public Widget {
  public:
    // theme must outlive this popup (same requirement as every other themed
    // widget in this codebase).
    explicit WhichKeyPopup(const Theme& theme);

    // Replaces the displayed content. Does not show/hide the widget itself
    // -- that's the caller's job via OverlayHost::Show/Hide, mirroring how
    // TerminalPanel's own content and visibility are driven separately.
    void SetHint(WhichKeyHint hint);

    // Rows the current hint needs to display in full (bindings.size() + 2
    // for the border), before any placement-side height cap -- the
    // placement function uses this to size the popup's Box without needing
    // its own copy of the hint.
    [[nodiscard]] int ContentRowCount() const;

    void Paint(Canvas c) override;

  private:
    const Theme&  theme_;
    WhichKeyHint  hint_;
};

} // namespace ned::ui

#endif // NED_UI_WHICHKEYPOPUP_H
