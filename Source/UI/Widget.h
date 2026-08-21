//
// Notcurses-backed replacement for the ftxui::ComponentBase/Screen/Cell
// foundation FTXUI used to provide for free (FTXUI -> Notcurses migration,
// Phase 1). Every widget in this directory derives from Widget and
// implements Paint() (was paint(ox::Canvas), then Paint(Canvas) again under
// FTXUI) -- Canvas below preserves that same local-coordinate,
// operator[]-based ergonomic, so each widget's actual painting logic needs
// minimal changes during this port too, mostly type names.
//
// Two real, deliberate simplifications versus the FTXUI-era version of this
// file, both consequences of Notcurses being a lower-level library than
// FTXUI rather than a peer:
//
//  - No Node/Requirement/ComputeRequirement plumbing. FTXUI needed
//    PaintNode (a private ftxui::Node subclass) plus a manually-driven
//    cursorAnchor_ Node to get a cursor position through its own layout
//    pipeline -- Notcurses has no equivalent pipeline to route through at
//    all; the composition root (Source/main.cpp, Phase 2) just asks
//    whichever Widget currently holds focus for CursorPosition() directly
//    and calls notcurses_cursor_enable itself. No PaintNode-equivalent
//    exists in this file anymore.
//  - No Container::Vertical/Horizontal, no size()/flex() decorators, no
//    Maybe(). FTXUI rebuilt its whole Element tree fresh every frame and
//    owned layout math itself; Notcurses has no layout system at all, so
//    that becomes this project's own problem -- see Source/UI/Layout.h
//    (Phase 3), not this file. Widget itself only needs to report/receive a
//    Box, the same "pull, don't push" shape ftxui::Node::SetBox already had.
//

#ifndef NED_UI_WIDGET_H
#define NED_UI_WIDGET_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

namespace ned::ui {

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int width  = 0;
    int height = 0;
};

// Absolute (screen-space) rectangle, inclusive on all four edges -- mirrors
// ftxui::Box's own shape/semantics exactly (x_min/x_max/y_min/y_max,
// Contain()), since Widget's own box-tracking logic was already written
// against that shape and there's no reason to reinvent it.
struct Box {
    int x_min = 0, x_max = -1, y_min = 0, y_max = -1;

    [[nodiscard]] constexpr bool Contain(int x, int y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }
};

// A small, introspectable color representation -- moved here from Theme.h
// (FTXUI -> Notcurses migration) since Cell (below) needs to store one
// directly and Theme.h now depends on Widget.h for Cell, not the other way
// around. Otherwise unchanged: still the same three-way
// Default/Palette16/TrueColor shape ftxui::Color's own three color kinds
// used, kept because it's genuinely necessary for ThemeFile.cpp's own
// round-trip text serialization (which needs the kind/RGB bytes back out,
// unlike an opaque library Color type) -- see ThemeFile.cpp's own header
// comment. Turning one into a real terminal color now happens inside
// Screen::Flush (Widget.cpp), the only place that still needs to know how
// Notcurses itself wants colors expressed.
struct Color {
    enum class Kind : std::uint8_t { Default,
                                     Palette16,
                                     TrueColor };

    Kind         kind         = Kind::Default;
    std::uint8_t paletteIndex = 0;             // valid when kind == Palette16
    std::uint8_t red = 0, green = 0, blue = 0; // valid when kind == TrueColor

    [[nodiscard]] constexpr bool operator==(const Color&) const = default;

    // Matches ox::RGB's own hex-literal convenience (e.g. RGB(0x2b2b40)) --
    // every true-color use in this codebase already writes colors this way.
    [[nodiscard]] static constexpr Color RGB(std::uint32_t hex) {
        return Color{.kind  = Kind::TrueColor,
                     .red   = static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                     .green = static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                     .blue  = static_cast<std::uint8_t>(hex & 0xFF)};
    }
    [[nodiscard]] static constexpr Color RGB(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return Color{.kind = Kind::TrueColor, .red = r, .green = g, .blue = b};
    }
    [[nodiscard]] static constexpr Color Palette(std::uint8_t index) {
        return Color{.kind = Kind::Palette16, .paletteIndex = index};
    }

    // Named 16-color constants -- kept under the same Black/Red/.../Bright*
    // names ox::XColor/ftxui::Color::Palette16 both used, so every existing
    // color choice in Theme.cpp keeps reading unchanged across this move.
    static const Color Default;
    static const Color Black, Red, Green, Yellow, Blue, Magenta, Cyan, White;
    static const Color BrightBlack, BrightRed, BrightGreen, BrightYellow, BrightBlue, BrightMagenta, BrightCyan,
        BrightWhite;

    // Blends two colors t of the way from a to b in RGB space -- replaces
    // ftxui::Color::Interpolate, which ModeLine's gradient background and
    // EchoArea's dimmed-text rendering both used. Always produces a
    // TrueColor result (RGB interpolation is meaningless for Default, and
    // Notcurses' own 16-color palette entries don't have a fixed universal
    // RGB value to blend from either) -- Default/Palette16 endpoints are
    // approximated via a fixed RGB table (Widget.cpp) before blending, the
    // same approximation ftxui::Color::Interpolate itself had to make
    // internally for the same reason.
    [[nodiscard]] static Color Interpolate(float t, const Color& a, const Color& b);
};

inline constexpr Color Color::Default{};
inline constexpr Color Color::Black         = Color::Palette(0);
inline constexpr Color Color::Red           = Color::Palette(1);
inline constexpr Color Color::Green         = Color::Palette(2);
inline constexpr Color Color::Yellow        = Color::Palette(3);
inline constexpr Color Color::Blue          = Color::Palette(4);
inline constexpr Color Color::Magenta       = Color::Palette(5);
inline constexpr Color Color::Cyan          = Color::Palette(6);
inline constexpr Color Color::White         = Color::Palette(7);
inline constexpr Color Color::BrightBlack   = Color::Palette(8);
inline constexpr Color Color::BrightRed     = Color::Palette(9);
inline constexpr Color Color::BrightGreen   = Color::Palette(10);
inline constexpr Color Color::BrightYellow  = Color::Palette(11);
inline constexpr Color Color::BrightBlue    = Color::Palette(12);
inline constexpr Color Color::BrightMagenta = Color::Palette(13);
inline constexpr Color Color::BrightCyan    = Color::Palette(14);
inline constexpr Color Color::BrightWhite   = Color::Palette(15);

// One screen cell -- mirrors ftxui::Cell's own field names exactly (a
// deliberate choice, not an accident of porting) so that every widget's
// existing `ftxui::Cell& cell = c[{...}]; cell.foreground_color = ...;`
// body needs nothing more than the type name changed. Unlike ftxui::Cell,
// foreground_color/background_color are this file's own Color (above)
// rather than an opaque library color -- one less conversion for
// Brush::ApplyTo (Theme.h) to do per cell, since Screen::Flush is the only
// place that still needs to turn a Color into a real terminal color.
struct Cell {
    std::string character = " ";
    Color       foreground_color;
    Color       background_color;
    bool        bold          = false;
    bool        italic        = false;
    bool        underlined    = false;
    bool        strikethrough = false;
    bool        inverted      = false;
};

// A full-terminal-sized grid of Cells that every Widget's Paint() call
// writes into (via Canvas, below) over the course of one frame, and which
// gets pushed out to the real ncplane exactly once per frame by Flush() --
// mirrors the role ftxui::Screen used to fill, including leaving Notcurses
// itself to do the actual terminal-diffing work at Flush time (Notcurses'
// own notcurses_render() already compresses a full-plane repaint down to
// only the cells that actually changed since the last frame, the same way
// FTXUI's own Screen::ToString() diffing used to -- no reason to duplicate
// that here).
class Screen {
  public:
    Screen(int width, int height) : width_(std::max(0, width)), height_(std::max(0, height)),
                                    cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {
    }

    [[nodiscard]] int Width() const {
        return width_;
    }
    [[nodiscard]] int Height() const {
        return height_;
    }

    [[nodiscard]] Cell& PixelAt(int x, int y) {
        return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
    }

    // Writes every cell in this Screen out to the given ncplane (which must
    // be at least Width() x Height()) and requests a real terminal
    // repaint -- the one place fg/bg Color and the bold/italic/underline/
    // strikethrough/inverted trait bools actually become Notcurses calls
    // (ncplane_set_fg_*/ncplane_set_bg_*/ncplane_set_styles/
    // ncplane_putegc_yx). Called once per frame from the main loop
    // (Source/main.cpp, Phase 2) after every visible Widget has painted
    // into this Screen.
    void Flush(ncplane* plane);

  private:
    int               width_;
    int               height_;
    std::vector<Cell> cells_;
};

// A view onto a rectangular region of a Screen, translating local
// (widget-relative) coordinates to the Screen's absolute ones -- mirrors
// the previous FTXUI-backed Canvas's own role exactly, unchanged in shape.
class Canvas {
  public:
    Canvas(Screen& screen, Box box) : screen_(screen), box_(box),
                                      size_{std::max(0, box.x_max - box.x_min + 1), std::max(0, box.y_max - box.y_min + 1)} {
    }

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Out-of-bounds writes are silently clipped to a discard cell -- same
    // "no bounds checking, that's the caller's job" contract the FTXUI- and
    // TermOx-era Canvases both had, kept as a cheap extra safety net.
    [[nodiscard]] Cell& operator[](Point p) {
        if (p.x < 0 || p.x >= size_.width || p.y < 0 || p.y >= size_.height) {
            return discard_;
        }
        return screen_.PixelAt(box_.x_min + p.x, box_.y_min + p.y);
    }

    // Returns a Canvas over the same underlying Screen but a different
    // (absolute, screen-space) Box -- what a container Widget (Layout.h's
    // own Container) uses to hand each child a Canvas scoped to that
    // child's own Box_() rather than the container's, so a child's own
    // `c[{.x = 0, .y = 0}]` writes always land at that child's own visual
    // top-left, exactly as every leaf widget's existing Paint() body
    // already assumes.
    [[nodiscard]] Canvas ForBox(Box box) const {
        return Canvas(screen_, box);
    }

  private:
    Screen& screen_;
    Box     box_;
    Size    size_;
    Cell    discard_;
};

// A mouse event already decoded from a raw ncinput -- mirrors MouseEvent's
// previous FTXUI-backed shape (button/motion plus modifier flags), kept
// because every OnEvent override that pattern-matches on these fields
// carries over unchanged in spirit. LOCAL widget-relative coordinates come
// from Widget::LocalMouseEvent, same as before; Event::mouse() (below)
// itself reports absolute/screen-space coordinates, same as
// ftxui::Event::mouse() did.
struct MouseEvent {
    Point at;

    enum class Button { None,
                        Left,
                        Middle,
                        Right,
                        WheelUp,
                        WheelDown };
    enum class Motion { Pressed,
                        Released,
                        Moved };

    Button button  = Button::None;
    Motion motion  = Motion::Moved;
    bool   shift   = false;
    bool   meta    = false; // Alt/Meta -- NCKEY_MOD_ALT
    bool   control = false;
};

// Wraps one decoded Notcurses ncinput, giving it the same is_mouse()/
// mouse() ergonomic ftxui::Event offered -- kept deliberately, since every
// widget's OnEvent override already reads that way and there's no reason
// to make Phase 3's per-widget port also relearn a new event vocabulary at
// the same time as everything else that has to change. Unlike
// ftxui::Event, there's no static named-instance table (Event::ArrowUp,
// etc.) -- ncinput already hands over a decoded, synthesized key code
// directly (NCKEY_UP, ...), so KeyTranslation.cpp compares against those
// constants instead of constructing/comparing whole Event values; Event
// only needs to expose the raw ncinput for that.
class Event {
  public:
    // Owns a copy of input (a plain, cheap-to-copy POD struct) rather than
    // borrowing a reference to it -- unlike ftxui::Event (an immutable value
    // type test code could freely construct ad hoc via named factories like
    // Event::Character("x")), a reference-based Event would leave every
    // such throwaway test-construction site holding a dangling pointer to a
    // temporary the instant construction finished. Owning it directly is
    // what makes Event a real, freely-constructible value type instead of a
    // borrowed view valid only for the caller's own immediate scope.
    explicit Event(const ncinput& input) : input_(input) {
    }

    [[nodiscard]] const ncinput& raw() const {
        return input_;
    }

    [[nodiscard]] bool       is_mouse() const;
    [[nodiscard]] MouseEvent mouse() const; // decoded fresh each call, cheap

  private:
    ncinput input_;
};

// The replacement for both TermOx's ox::Widget and FTXUI's own
// ftxui::ComponentBase-derived Widget. Every Source/UI/ widget derives from
// this and implements Paint() and, for anything that takes mouse or
// keyboard input, overrides OnEvent -- unchanged in spirit from the FTXUI
// era, just no longer backed by a real base-class event-dispatch tree
// (Notcurses has none): the main loop (Phase 2) is what decides which
// Widget(s) an Event actually reaches, the same way it already decides
// which Widget gets painted where via Box.
class Widget;

// The Widget currently holding keyboard focus (via Widget::TakeFocus), or
// nullptr if none does yet -- forward-declared here so Widget::Focused()
// (below) can call it inline; defined in full, alongside FocusedWidget's own
// doc comment, near the end of this file next to TakeFocus's implementation
// context.
[[nodiscard]] Widget* FocusedWidget();

class Widget {
  public:
    virtual ~Widget();

    // Whether the composition root should include this widget in its
    // layout/paint/event-dispatch pass at all this frame -- was
    // ox::Widget::active, then FTXUI-era Widget::active. Same contract:
    // flipping it needs no separate reflow call, since every frame's
    // layout is recomputed from scratch by whoever owns this widget's
    // Layout tree (Phase 3) anyway.
    bool active = true;

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Public for the same internal-framework-seam reason the FTXUI-era
    // Widget documented (avoiding a friend-declaration tangle with the
    // layout code that has to call this from outside the class hierarchy)
    // -- not a real external API surface.
    void SetBox_(Box box) {
        box_ = box;
        const Size newSize{std::max(0, box.x_max - box.x_min + 1), std::max(0, box.y_max - box.y_min + 1)};
        if (newSize.width != size_.width || newSize.height != size_.height) {
            const Size previous = size_;
            size_               = newSize;
            OnResize(previous);
        }
    }

    [[nodiscard]] const Box& Box_() const {
        return box_;
    }

    // Hit-tests event against this widget's own last-painted bounds and, if
    // it's a mouse event landing inside them, returns it translated to
    // local coordinates -- std::nullopt otherwise. Same "every widget gets
    // every event, hit-testing is each widget's own job" contract the
    // FTXUI era already established (confirmed then by reading
    // ComponentBase/ContainerBase directly) and which the Phase 2 main
    // loop preserves rather than trying to hit-test centrally -- several
    // existing widgets (ScrollArrowButton's press-and-hold release check,
    // ProjectSidebar's resize-drag cooperation with BufferView) actively
    // depend on receiving mouse events outside their own bounds too, so a
    // centrally hit-tested dispatch would be a real behavior change, not a
    // neutral one.
    [[nodiscard]] std::optional<MouseEvent> LocalMouseEvent(const Event& event) const {
        if (!event.is_mouse()) {
            return std::nullopt;
        }
        MouseEvent mouse = event.mouse();
        if (!box_.Contain(mouse.at.x, mouse.at.y)) {
            return std::nullopt;
        }
        mouse.at = Point{mouse.at.x - box_.x_min, mouse.at.y - box_.y_min};
        return mouse;
    }

    virtual void Paint(Canvas canvas) = 0;

    // Returns true if this widget consumed the event -- was
    // ComponentBase::OnEvent's own return value, kept the same way for the
    // same reason MouseEvent/is_mouse()/mouse() were: minimizing what
    // Phase 3's per-widget port actually needs to relearn.
    virtual bool OnEvent(const Event& /*event*/) {
        return false;
    }

    // Override for widgets that need to react to a size change explicitly
    // -- default no-op.
    virtual void OnResize(Size /*previous*/) {
    }

    // Minimum size this widget reports -- unused by Widget itself now that
    // there's no FTXUI layout system computing requirements from it, kept
    // as a hook for Source/UI/Layout.h (Phase 3) to consult if a widget
    // ever needs to report a real minimum rather than main.cpp's
    // composition just hardcoding one, matching every real pre-Notcurses
    // use site anyway (each one already overrode sizing explicitly at its
    // own composition call site).
    virtual Size MinimumSize() const {
        return Size{0, 0};
    }

    // Whether this widget can hold keyboard focus -- was
    // ComponentBase::Focusable(). Default false, matching every widget
    // that never overrode it under FTXUI (TabBar, ProjectSidebar,
    // ScrollArrowButton, ModeLine, EchoArea); only BufferView overrides
    // this to true.
    [[nodiscard]] virtual bool Focusable() const {
        return false;
    }

    // Makes this widget the one the main loop routes keyboard Events (and
    // CursorPosition()/CursorShape() queries) to -- was
    // ComponentBase::TakeFocus(), which used to walk up FTXUI's own
    // Container tree marking each ancestor's active child. No such tree
    // exists to walk anymore: this just registers this widget directly
    // with the small process-wide focus registry in Widget.cpp (mirroring
    // the same mutex-guarded static-state pattern TabWidth.h/ProjectRoot.h
    // already use for "one coherent process-wide fact," which "which
    // widget currently has keyboard focus" genuinely is here -- there is
    // exactly one real terminal cursor). WindowManager's existing
    // `pane->Buffer().TakeFocus()` call sites need no change at all.
    //
    // The destructor (below) clears the registry if it currently points at
    // this widget, for the same reason a raw observer pointer anywhere else
    // would need that discipline: nothing else in this design ever
    // guarantees the focused widget outlives the registry's own reference
    // to it (unlike FTXUI's tree-based focus-selector, which lived inside
    // the same Container being destroyed alongside its children). Skipping
    // this is a real, confirmed dangling-pointer SIGSEGV, not a
    // hypothetical one -- caught live in WindowManagerTest.cpp: one
    // TEST_CASE's BufferView takes focus, that TEST_CASE ends and destroys
    // it, and the next TEST_CASE's FeedSequence(), which routes a keyboard
    // Event straight to FocusedWidget() (main.cpp's own real dispatch, see
    // EventLoopCallbacks::onEvent), dereferences the now-freed pointer.
    void TakeFocus();

    // Whether this widget currently holds keyboard focus -- was
    // ComponentBase::Focused(), used by WindowManager to find which pane's
    // BufferView is the currently active one. A plain identity check
    // against the same registry TakeFocus() writes to.
    [[nodiscard]] bool Focused() const {
        return FocusedWidget() == this;
    }

    // Local (widget-relative) position the real terminal cursor should be
    // placed at, or std::nullopt to leave it hidden -- unchanged in shape
    // from the FTXUI era, just consulted directly by the main loop now
    // instead of being routed through ftxui::Node's own
    // ComputeRequirement/SetBox cursor plumbing (see this file's own
    // header comment). Only BufferView overrides this today.
    [[nodiscard]] virtual std::optional<Point> CursorPosition() const {
        return std::nullopt;
    }

    enum class CursorShape { Block,
                             Underline,
                             Bar };

    // Cursor glyph shape -- Bar (a thin vertical caret) by default, the
    // same deliberate small visual upgrade over TermOx's own shapeless
    // cursor concept the FTXUI era introduced (the user gave explicit
    // latitude for this kind of thing); Notcurses' own
    // notcurses_cursor_enable has no shape parameter at all (shape is a
    // terminal-emulator-level DECSCUSR concept Notcurses doesn't expose),
    // so the main loop (Phase 2) is expected to emit that escape sequence
    // itself if this ever needs to be more than decorative -- flagged here
    // as a known Phase 2 follow-up rather than silently dropped.
    [[nodiscard]] virtual CursorShape CursorShapeHint() const {
        return CursorShape::Bar;
    }

  private:
    Size size_;
    Box  box_; // absolute, screen-space -- see LocalMouseEvent
};

} // namespace ned::ui

#endif // NED_UI_WIDGET_H
