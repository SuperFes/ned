//
// generic-popup follow-up: a reusable floating OverlayHost box generalized
// out of the original which-key popup, in two modes selected by
// SetFocusable():
//
//  - Non-focusable (default, Focusable() stays false): a pure renderer,
//    exactly WhichKeyPopup's own old contract -- some other widget (usually
//    BufferView) keeps keyboard focus and drives this one's displayed
//    content via repeated SetModel() calls as its own session's selection
//    changes. Which-key itself, and every EchoArea-squeezed candidate list
//    this is meant to eventually replace (M-x, theme picker, LSP code
//    action select, ...), use this mode.
//  - Focusable (SetFocusable(true)): this widget takes real keyboard focus
//    and drives its own selection entirely via OnEvent -- Up/Down/C-p/C-n
//    move it, digit keys 1-9 jump-select-and-activate in a single
//    keystroke (the same one-keypress convenience LSP code action select
//    already had before this widget existed, now built in so every
//    focus-mode consumer gets it for free), Enter activates the current
//    selection, Escape/C-g cancels. Anything else is offered to
//    SetOnKey()'s handler before being dropped, so a consumer (the
//    buffer-list panel) can layer extra per-row actions (mark/kill/
//    refresh) without reimplementing navigation. The buffer-list panel is
//    this mode's first real consumer.
//

#ifndef NED_UI_LISTPOPUP_H
#define NED_UI_LISTPOPUP_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

struct ListPopupRow {
    std::string left;          // optional leading column (a key chord, a mark glyph, "N)") -- empty means none
    std::string main;          // the row's main label
    bool        accented = false; // paint `left` in the theme's accent brush (which-key's chord color)
    // completion-popup follow-up: overrides `left`'s color entirely (ahead
    // of `accented`) with an explicit foreground -- a completion item's
    // kind glyph is colored per-kind (Theme::BrushFor(SyntaxClassFor(...))),
    // not the single fixed accent color every other `left` column uses.
    // Unset (every other consumer) keeps the existing accented/plain choice.
    std::optional<Color> leftForeground;
    // completion-popup follow-up: optional trailing column, right-aligned
    // against the popup's right border (a completion item's type/signature
    // detail) -- empty means none, same convention as `left`. Painted in
    // the same dim/accent style `left` uses, never `accented`-selectable
    // itself; `main` is truncated before it rather than allowed to run
    // underneath.
    std::string right;
};

struct ListPopupModel {
    std::string                title; // border title
    std::vector<ListPopupRow>  rows;

    // Set => paint a selection bar across this row (theme_.selectionBackground,
    // ProjectSidebar's own selected-row technique). Unset => a plain static
    // list, which-key's original look.
    std::optional<std::size_t> selectedIndex;

    // completion-popup follow-up: absolute screen position to open near,
    // set only by a completion-at-point-style consumer -- unset (every
    // other consumer today: which-key, M-x/find-file/.../code-action-select)
    // keeps the existing static/docked placement untouched. The placement
    // function reads this back via ListPopup::Anchor() rather than this
    // model directly, the same "widget echoes back one derived fact"
    // shape ContentRowCount() already establishes.
    std::optional<Point> anchor;
};

class ListPopup : public Widget {
  public:
    // theme must outlive this popup (same requirement as every other themed
    // widget in this codebase).
    explicit ListPopup(const Theme& theme);

    // Replaces the displayed content. Does not show/hide the widget itself
    // -- that's the caller's job via OverlayHost::Show/Hide.
    void SetModel(ListPopupModel model);

    // Rows the current model needs to display in full (rows.size() + 2 for
    // the border), before any placement-side height cap -- the placement
    // function uses this to size the popup's Box without needing its own
    // copy of the model.
    [[nodiscard]] int ContentRowCount() const;

    // completion-popup follow-up: the current model's anchor, for a
    // placement function to position this popup near (see ListPopupModel::
    // anchor's own doc comment). std::nullopt for every non-anchored
    // consumer.
    [[nodiscard]] std::optional<Point> Anchor() const;

    // Selects the focus-owning mode described in this file's header comment.
    // Defaults to false (which-key's original non-focusable behavior).
    void SetFocusable(bool focusable);
    [[nodiscard]] bool Focusable() const override {
        return focusable_;
    }

    // Focus-mode callbacks -- ignored entirely in non-focus mode, since
    // OnEvent is never reached there (nothing ever calls TakeFocus() on a
    // non-focusable widget).
    void SetOnHighlightChange(std::function<void(std::size_t)> onHighlightChange);
    void SetOnActivate(std::function<void(std::size_t)> onActivate);
    void SetOnCancel(std::function<void()> onCancel);
    void SetOnKey(std::function<void(const editor::KeyChord&)> onKey);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    const Theme&    theme_;
    ListPopupModel  model_;
    bool            focusable_ = false;

    std::function<void(std::size_t)>              onHighlightChange_;
    std::function<void(std::size_t)>               onActivate_;
    std::function<void()>                           onCancel_;
    std::function<void(const editor::KeyChord&)>    onKey_;

    bool HandleKeyEvent(const Event& event);
};

} // namespace ned::ui

#endif // NED_UI_LISTPOPUP_H
