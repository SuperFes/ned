//
// The base widget/painting foundation this project's Notcurses-backed UI is
// built on. Every widget in Source/UI/ derives from Widget and implements
// Paint(Canvas) -- Canvas gives it a local-coordinate, operator[]-based view
// onto the frame's shared Screen (both below).
//
// Notcurses is a lower-level library than a typical TUI framework, so this
// file also owns two things a framework would normally provide:
//
//  - Cursor placement. There's no layout-tree plumbing to route a cursor
//    position through -- the composition root (Source/main.cpp) just asks
//    whichever Widget currently holds focus for CursorPosition() directly
//    and calls notcurses_cursor_enable itself.
//  - Layout. Notcurses has no layout system of its own; box computation is
//    this project's own problem, handled by Source/UI/Layout.h, not this
//    file. Widget itself only needs to report/receive a Box (pull, don't
//    push).
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

    // completion-popup follow-up: needed to compare two
    // std::optional<Point> anchors (has the popup's anchor moved since the
    // last notify?) -- same defaulted-equality precedent Brush/Theme
    // already establish elsewhere in this UI layer.
    [[nodiscard]] constexpr bool operator==(const Point&) const = default;
};

struct Size {
    int width  = 0;
    int height = 0;
};

// Absolute (screen-space) rectangle, inclusive on all four edges.
struct Box {
    int x_min = 0, x_max = -1, y_min = 0, y_max = -1;

    [[nodiscard]] constexpr bool Contain(int x, int y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }
};

// A small, introspectable color representation -- lives here (not Theme.h)
// since Cell (below) needs to store one directly. Genuinely necessary as a
// real three-way Default/Palette16/TrueColor variant, rather than an opaque
// library color type, because ThemeFile.cpp's round-trip text serialization
// needs the kind/RGB bytes back out -- see ThemeFile.cpp's own header
// comment. Turning one into a real terminal color happens inside
// Screen::Flush (Widget.cpp), the only place that needs to know how
// Notcurses itself wants colors expressed.
struct Color {
    enum class Kind : std::uint8_t { Default,
                                     Palette16,
                                     TrueColor };

    Kind         kind         = Kind::Default;
    std::uint8_t paletteIndex = 0;             // valid when kind == Palette16
    std::uint8_t red = 0, green = 0, blue = 0; // valid when kind == TrueColor

    [[nodiscard]] constexpr bool operator==(const Color&) const = default;

    // Hex-literal convenience (e.g. RGB(0x2b2b40)) -- every true-color use
    // in this codebase writes colors this way.
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

    // Named 16-color constants.
    static const Color Default;
    static const Color Black, Red, Green, Yellow, Blue, Magenta, Cyan, White;
    static const Color BrightBlack, BrightRed, BrightGreen, BrightYellow, BrightBlue, BrightMagenta, BrightCyan,
        BrightWhite;

    // Blends two colors t of the way from a to b in RGB space -- used by
    // ModeLine's gradient background and EchoArea's dimmed-text rendering.
    // Always produces a TrueColor result (RGB interpolation is meaningless
    // for Default, and Notcurses' own 16-color palette entries don't have a
    // fixed universal RGB value to blend from either) -- Default/Palette16
    // endpoints are approximated via a fixed RGB table (Widget.cpp) before
    // blending.
    [[nodiscard]] static Color Interpolate(float t, const Color& a, const Color& b);
};

// Approximates any Color -- including Default/Palette16, which have no
// canonical RGB value of their own -- down to concrete RGB bytes, via the
// same fixed ANSI-palette table Color::Interpolate already relies on
// (Widget.cpp). Public because Minimap's pixel-blitter rasterizer
// (Minimap.cpp) needs real RGB bytes to build an RGBA image for
// ncvisual_from_rgba, not a terminal color channel -- Screen::Flush itself
// still doesn't need this, it hands Color straight to Notcurses' own
// fg/bg calls.
void ColorToRgb8(const Color& color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

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

// One screen cell. foreground_color/background_color are this file's own
// Color (above) rather than an opaque library color -- one less conversion
// for Brush::ApplyTo (Theme.h) to do per cell, since Screen::Flush is the
// only place that needs to turn a Color into a real terminal color.
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
// Notcurses' own notcurses_render() does the actual terminal-diffing work,
// compressing a full-plane repaint down to only the cells that actually
// changed since the last frame, so there's no reason to duplicate that
// here.
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
    // (Source/main.cpp) after every visible Widget has painted into this
    // Screen.
    void Flush(ncplane* plane);

  private:
    int               width_;
    int               height_;
    std::vector<Cell> cells_;
};

// A view onto a rectangular region of a Screen, translating local
// (widget-relative) coordinates to the Screen's absolute ones.
class Canvas {
  public:
    Canvas(Screen& screen, Box box) : screen_(screen), box_(box),
                                      size_{std::max(0, box.x_max - box.x_min + 1), std::max(0, box.y_max - box.y_min + 1)} {
    }

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Out-of-bounds writes are silently clipped to a discard cell rather
    // than bounds-checked -- a cheap safety net, not a contract callers
    // should rely on.
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

// A mouse event already decoded from a raw ncinput (button/motion plus
// modifier flags). LOCAL widget-relative coordinates come from
// Widget::LocalMouseEvent; Event::mouse() (below) itself reports
// absolute/screen-space coordinates.
struct MouseEvent {
    Point at;

    enum class Button { None,
                        Left,
                        Middle,
                        Right,
                        WheelUp,
                        WheelDown,
                        WheelLeft,
                        WheelRight };
    enum class Motion { Pressed,
                        Released,
                        Moved };

    Button button  = Button::None;
    Motion motion  = Motion::Moved;
    bool   shift   = false;
    bool   meta    = false; // Alt/Meta -- NCKEY_MOD_ALT
    bool   control = false;
};

// Wraps one decoded Notcurses ncinput, giving it an is_mouse()/mouse()
// ergonomic every widget's OnEvent override reads against. There's no
// static named-instance table (Event::ArrowUp, etc.) -- ncinput already
// hands over a decoded, synthesized key code directly (NCKEY_UP, ...), so
// KeyTranslation.cpp compares against those constants instead of
// constructing/comparing whole Event values; Event only needs to expose the
// raw ncinput for that.
class Event {
  public:
    // Owns a copy of input (a plain, cheap-to-copy POD struct) rather than
    // borrowing a reference to it -- a reference-based Event would leave any
    // throwaway test-construction site (constructing one ad hoc to feed a
    // widget) holding a dangling pointer to a temporary the instant
    // construction finished. Owning it directly is what makes Event a real,
    // freely-constructible value type instead of a borrowed view valid only
    // for the caller's own immediate scope.
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

// Base class every Source/UI/ widget derives from, implementing Paint()
// and, for anything that takes mouse or keyboard input, overriding OnEvent.
// There is no base-class event-dispatch tree -- the main loop
// (Source/main.cpp) is what decides which Widget(s) an Event actually
// reaches, the same way it already decides which Widget gets painted where
// via Box.
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
    // layout/paint/event-dispatch pass at all this frame. Flipping it needs
    // no separate reflow call, since every frame's layout is recomputed
    // from scratch by whoever owns this widget's Layout tree anyway.
    bool active = true;

    [[nodiscard]] const Size& size() const {
        return size_;
    }

    // Public to avoid a friend-declaration tangle with the layout code that
    // has to call this from outside the class hierarchy -- not a real
    // external API surface.
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
    // local coordinates -- std::nullopt otherwise. Every widget receives
    // every event regardless of position; hit-testing is each widget's own
    // job, not something the main loop does centrally -- several existing
    // widgets (ScrollArrowButton's press-and-hold release check,
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

    // Returns true if this widget consumed the event.
    virtual bool OnEvent(const Event& /*event*/) {
        return false;
    }

    // Override for widgets that need to react to a size change explicitly
    // -- default no-op.
    virtual void OnResize(Size /*previous*/) {
    }

    // Minimum size this widget reports -- unused by Widget itself, kept as
    // a hook for Source/UI/Layout.h to consult if a widget ever needs to
    // report a real minimum rather than main.cpp's composition just
    // hardcoding one.
    virtual Size MinimumSize() const {
        return Size{0, 0};
    }

    // Whether this widget can hold keyboard focus. Default false (TabBar,
    // ProjectSidebar, ScrollArrowButton, ModeLine, EchoArea all leave it
    // unset); only BufferView overrides this to true.
    [[nodiscard]] virtual bool Focusable() const {
        return false;
    }

    // Makes this widget the one the main loop routes keyboard Events (and
    // CursorPosition()/CursorShape() queries) to. Registers this widget
    // directly with the small process-wide focus registry in Widget.cpp
    // (mirroring the same mutex-guarded static-state pattern
    // TabWidth.h/ProjectRoot.h already use for "one coherent process-wide
    // fact," which "which widget currently has keyboard focus" genuinely is
    // here -- there is exactly one real terminal cursor).
    //
    // The destructor (below) clears the registry if it currently points at
    // this widget, for the same reason a raw observer pointer anywhere else
    // would need that discipline: nothing else in this design ever
    // guarantees the focused widget outlives the registry's own reference
    // to it. Skipping this is a real, confirmed dangling-pointer SIGSEGV,
    // not a hypothetical one -- caught live in WindowManagerTest.cpp: one
    // TEST_CASE's BufferView takes focus, that TEST_CASE ends and destroys
    // it, and the next TEST_CASE's FeedSequence(), which routes a keyboard
    // Event straight to FocusedWidget() (main.cpp's own real dispatch, see
    // EventLoopCallbacks::onEvent), dereferences the now-freed pointer.
    void TakeFocus();

    // Whether this widget currently holds keyboard focus, used by
    // WindowManager to find which pane's BufferView is the currently active
    // one. A plain identity check against the same registry TakeFocus()
    // writes to.
    [[nodiscard]] bool Focused() const {
        return FocusedWidget() == this;
    }

    // Local (widget-relative) position the real terminal cursor should be
    // placed at, or std::nullopt to leave it hidden -- consulted directly
    // by the main loop (see this file's own header comment). Only
    // BufferView overrides this today.
    [[nodiscard]] virtual std::optional<Point> CursorPosition() const {
        return std::nullopt;
    }

    enum class CursorShape { Block,
                             Underline,
                             Bar };

    // Cursor glyph shape -- Bar (a thin vertical caret) by default.
    // Notcurses' own notcurses_cursor_enable has no shape parameter at all
    // (shape is a terminal-emulator-level DECSCUSR concept Notcurses
    // doesn't expose), so the main loop would need to emit that escape
    // sequence itself if this ever needs to be more than decorative --
    // flagged here as a known follow-up rather than silently dropped.
    [[nodiscard]] virtual CursorShape CursorShapeHint() const {
        return CursorShape::Bar;
    }

  private:
    Size size_;
    Box  box_; // absolute, screen-space -- see LocalMouseEvent
};

} // namespace ned::ui

#endif // NED_UI_WIDGET_H
