//
// Small FTXUI-backed replacement for the ox::Widget/ox::Canvas/ox::Glyph
// foundation TermOx used to provide for free (TermOx -> FTXUI migration).
// Every widget in this directory derives from Widget and implements Paint()
// the same way it used to implement paint(ox::Canvas) -- Canvas below
// preserves that exact local-coordinate, operator[]-based ergonomic so each
// widget's actual painting logic needed minimal changes during the port,
// only the type names.
//

#ifndef NED_UI_WIDGET_H
#define NED_UI_WIDGET_H

#include <algorithm>
#include <optional>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace ned::ui {

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int width  = 0;
    int height = 0;
};

// A view onto a rectangular region of a real ftxui::Screen, translating
// local (widget-relative) coordinates to the Screen's absolute ones --
// mirrors ox::Canvas's own role exactly.
class Canvas {
  public:
    Canvas(ftxui::Screen& screen, ftxui::Box box)
        : screen_(screen), box_(box),
          size_{std::max(0, box.x_max - box.x_min + 1), std::max(0, box.y_max - box.y_min + 1)} {}

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Out-of-bounds writes are silently clipped to a discard cell -- matches
    // ox::Canvas's own "no bounds checking, that's the caller's job"
    // contract closely enough in practice (every widget already loops
    // within its own reported size), while being a cheap, harmless extra
    // safety net against an off-by-one introduced during the port itself.
    [[nodiscard]] ftxui::Cell& operator[](Point p) {
        if (p.x < 0 || p.x >= size_.width || p.y < 0 || p.y >= size_.height) {
            return discard_;
        }
        return screen_.PixelAt(box_.x_min + p.x, box_.y_min + p.y);
    }

  private:
    ftxui::Screen& screen_;
    ftxui::Box     box_;
    Size           size_;
    ftxui::Cell    discard_;
};

// A mouse event already hit-tested and translated to this widget's own
// local coordinates -- see Widget::LocalMouseEvent. Mirrors ox::Mouse's own
// shape closely (button/motion plus modifier flags) since old widgets'
// mouse_press(ox::Mouse)/mouse_move/mouse_release bodies pattern-matched on
// exactly these fields; button/motion reuse FTXUI's own enums directly
// rather than redefining equivalents.
struct MouseEvent {
    Point                 at; // LOCAL to the widget, like ox::Mouse::at was
    ftxui::Mouse::Button  button;
    ftxui::Mouse::Motion  motion;
    bool                  shift   = false;
    bool                  meta    = false;
    bool                  control = false;
};

// The replacement for ox::Widget. Every Source/UI/ widget derives from this
// and implements Paint() (was paint(ox::Canvas)) and, for anything that
// takes mouse or keyboard input, overrides OnEvent (ComponentBase's own
// virtual -- was mouse_press/mouse_move/mouse_wheel/key_press, now unified
// into one method the way FTXUI itself unifies all input into one Event
// type). size() replaces ox::Widget's own `size` field, kept current
// automatically each frame by the Node this wraps, the same "always
// current, no caching needed" contract ox::Widget's own size field had.
// Focus (was ox::FocusPolicy) is handled per-widget via ComponentBase's own
// Focusable()/TakeFocus() rather than reintroduced as a field here -- FTXUI
// already has a real focus model, no need to shadow it.
//
// Real, empirically-confirmed architectural difference from TermOx worth
// knowing before writing any OnEvent override (see the mode-overrides ->
// FTXUI migration spike): FTXUI does NOT hit-test mouse events against a
// widget's own bounds before delivering them -- every leaf component in the
// tree receives every mouse event unconditionally (confirmed by reading
// ComponentBase's and ContainerBase's own real OnEvent/OnMouseEvent
// implementations, not assumed), and it's each widget's own job to check
// position. LocalMouseEvent below is that check, done once so individual
// widgets don't each hand-roll it.
class Widget : public ftxui::ComponentBase {
  public:
    // Whether a composition-root Renderer (main.cpp) should include this
    // widget in its layout at all this frame -- was ox::Widget::active.
    // Unlike TermOx, flipping this needs no accompanying manual reflow
    // call: FTXUI rebuilds its whole Element tree fresh every frame (an
    // empirically confirmed migration finding, not an assumption -- toggling
    // a child's inclusion in an hbox and letting the very next frame render
    // naturally was enough for siblings to reclaim/cede the space, no
    // equivalent of ox::Widget::resize(...) needed at all). Checked by
    // whichever composition-level code decides this widget's parent
    // container's children each frame; Widget itself never reads it.
    bool active = true;

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Paint()/OnResize()/MinimumSize() below and SetBox_ are public rather
    // than protected/private purely to avoid a friend-declaration/anonymous-
    // namespace tangle with the small Node helper (Widget.cpp) that has to
    // call them from outside the class hierarchy (it wraps a Widget&, it
    // isn't one) -- this isn't a real external API surface (nothing outside
    // Source/UI/'s own widget implementations has a reason to call these),
    // just an internal framework seam, so the slightly looser access is a
    // reasonable, deliberate trade against friend-declaration ceremony.
    void SetBox_(ftxui::Box box) {
        box_               = box;
        const Size newSize{std::max(0, box.x_max - box.x_min + 1), std::max(0, box.y_max - box.y_min + 1)};
        if (newSize.width != size_.width || newSize.height != size_.height) {
            const Size previous = size_;
            size_                = newSize;
            OnResize(previous);
        }
    }

    // Hit-tests event against this widget's own last-painted bounds and, if
    // it's a mouse event that lands inside them, returns it translated to
    // local coordinates -- std::nullopt otherwise (not a mouse event, or
    // outside these bounds). See this class's own header comment for why
    // every OnEvent override needs to do this itself.
    [[nodiscard]] std::optional<MouseEvent> LocalMouseEvent(ftxui::Event event) const {
        if (!event.is_mouse()) {
            return std::nullopt;
        }
        const ftxui::Mouse& mouse = event.mouse(); // Event::mouse() is non-const, hence event taken by value here
        if (!box_.Contain(mouse.x, mouse.y)) {
            return std::nullopt;
        }
        return MouseEvent{
            .at      = Point{mouse.x - box_.x_min, mouse.y - box_.y_min},
            .button  = mouse.button,
            .motion  = mouse.motion,
            .shift   = mouse.shift,
            .meta    = mouse.meta,
            .control = mouse.control,
        };
    }

    virtual void Paint(Canvas canvas) = 0;

    // Override for widgets that need to react to a size change explicitly
    // (mirrors ox::Widget::resize(previous_size), which some widgets used
    // to reflow cached layout state) -- default no-op.
    virtual void OnResize(Size /*previous*/) {}

    // Minimum size this widget reports to FTXUI's layout system --
    // ox::Widget's own SizePolicy was set per-instance at construction, but
    // every real use site in this codebase overrode it anyway via an
    // explicit `| SizePolicy::fixed(n)`/`| flex()` at the composition site
    // in main.cpp, so that's where sizing is decided now too (via FTXUI's
    // own size()/flex() decorators) -- this just reports "no minimum,"
    // matching what every pre-migration widget's own default amounted to
    // once main.cpp's own override was applied.
    virtual Size MinimumSize() const {
        return Size{0, 0};
    }

  private:
    ftxui::Element OnRender() final;
    Size           size_;
    ftxui::Box     box_; // absolute, screen-space -- see LocalMouseEvent
};

} // namespace ned::ui

#endif // NED_UI_WIDGET_H
