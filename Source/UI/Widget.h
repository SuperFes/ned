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
#include <string_view>
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
// real variant, rather than an opaque library color type, because
// ThemeFile.cpp's round-trip text serialization needs the kind/RGB bytes
// back out -- see ThemeFile.cpp's own header comment.
//
// Palette16 is no longer something a *theme* can produce: themes are
// truecolor throughout. It survives as transport for the one thing that
// genuinely means "whatever this terminal calls colour N" -- an indexed SGR
// colour arriving from a program running inside the embedded terminal panel
// (Editor/Terminal/Emulator.cpp), which should keep honouring the user's own
// palette rather than being rewritten to our idea of red. Turning one into a real terminal color happens inside
// Screen::Flush (Widget.cpp), the only place that needs to know how
// Notcurses itself wants colors expressed.
struct Color {
    enum class Kind : std::uint8_t { Default,
                                     Palette16,
                                     TrueColor };

    Kind         kind         = Kind::Default;
    std::uint8_t paletteIndex = 0;             // valid when kind == Palette16 (embedded terminal only)
    std::uint8_t red = 0, green = 0, blue = 0; // valid when kind == TrueColor

    // Translucency follow-up: 255 is opaque and is what every existing
    // construction produces, so nothing that predates this field changed
    // meaning. Alpha is meaningful only for TrueColor -- Default is the
    // terminal's own background (there is nothing here to be partly), and
    // a palette index has no RGB value we could composite against. It is
    // never handed to Notcurses: Screen::Blend resolves it away (Widget.cpp),
    // and Screen::Flush only ever sees colors that are already opaque.
    std::uint8_t alpha = 255;

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

    // 0xrrggbbaa, the same byte order the theme files' own #rrggbbaa token
    // uses.
    [[nodiscard]] static constexpr Color RGBA(std::uint32_t hex) {
        return Color{.kind  = Kind::TrueColor,
                     .red   = static_cast<std::uint8_t>((hex >> 24) & 0xFF),
                     .green = static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                     .blue  = static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                     .alpha = static_cast<std::uint8_t>(hex & 0xFF)};
    }

    [[nodiscard]] constexpr Color WithAlpha(std::uint8_t a) const {
        Color copy = *this;
        copy.alpha = a;
        return copy;
    }

    [[nodiscard]] constexpr bool Opaque() const {
        return alpha == 255;
    }

    // "Has a real RGB value we can composite with", i.e. neither the
    // terminal's own background nor a palette index whose RGB we can only
    // guess at.
    [[nodiscard]] constexpr bool Composable() const {
        return kind == Kind::TrueColor;
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
// The sixteen names are now real RGB -- xterm's own default palette values
// -- rather than palette indices. Themes are truecolor and own their own
// contrast (Docs/Translucency.md), so a theme field must never resolve to
// "whatever the user's terminal decided index 9 means": that colour cannot
// be composited against, cannot carry alpha, and made every blend path
// define a case it had no answer for. The handful of call sites still
// reaching for a named colour rather than a Theme field are the ones a
// later phase should migrate into the theme.
inline constexpr Color Color::Black         = Color::RGB(0x000000);
inline constexpr Color Color::Red           = Color::RGB(0x800000);
inline constexpr Color Color::Green         = Color::RGB(0x008000);
inline constexpr Color Color::Yellow        = Color::RGB(0x808000);
inline constexpr Color Color::Blue          = Color::RGB(0x000080);
inline constexpr Color Color::Magenta       = Color::RGB(0x800080);
inline constexpr Color Color::Cyan          = Color::RGB(0x008080);
inline constexpr Color Color::White         = Color::RGB(0xC0C0C0);
inline constexpr Color Color::BrightBlack   = Color::RGB(0x808080);
inline constexpr Color Color::BrightRed     = Color::RGB(0xFF0000);
inline constexpr Color Color::BrightGreen   = Color::RGB(0x00FF00);
inline constexpr Color Color::BrightYellow  = Color::RGB(0xFFFF00);
inline constexpr Color Color::BrightBlue    = Color::RGB(0x0000FF);
inline constexpr Color Color::BrightMagenta = Color::RGB(0xFF00FF);
inline constexpr Color Color::BrightCyan    = Color::RGB(0x00FFFF);
inline constexpr Color Color::BrightWhite   = Color::RGB(0xFFFFFF);

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

// What Screen::Blend should do with a translucent color when the cell it
// lands on has no known background to composite against -- i.e. when the
// destination is Color::Default, the terminal's own background, which is
// also the only background a translucent terminal lets the desktop through.
// See Docs/Translucency.md; the short version is that a cell is one glyph
// plus one foreground plus one background, so "wash the background" and
// "stay see-through" cannot both happen, and different surfaces genuinely
// want different answers.
enum class AlphaPolicy {
    // Rules in order: composite if the destination background is known,
    // else dither into an empty cell, else tint the glyph's foreground.
    Auto,
    // Dither empty cells; leave cells carrying a glyph completely alone.
    // What patterns want -- a checkerboard behind code is unreadable.
    Dither,
    // Never dither; tint the foreground instead, glyph cells or not.
    TintText,
    // Give up the transparency and write the color opaque. The escape hatch
    // for a surface that must be legible above all else.
    Opaque,
    // Do nothing at all over a transparent background.
    Skip,
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
                                    cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)),
                                    backing_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {
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

    // The backing layer: a second grid flushed to a plane *below* the text
    // one, for backgrounds the text layer should not have to carry. A row
    // highlight painted here sits behind the glyphs instead of in the same
    // cell as them, so it never has to choose between washing the background
    // and keeping the syntax colour -- the two-pass idea in
    // Docs/Translucency.md, on real planes.
    //
    // Only the background matters here; the glyph and foreground of a backing
    // cell are never rendered, since the text plane above always supplies
    // them.
    [[nodiscard]] Cell& BackingAt(int x, int y) {
        return backing_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
    }

    // Backing cells persist between frames like the text ones do, so whoever
    // paints a highlight has to clear last frame's first.
    void ClearBacking() {
        for (Cell& cell : backing_) {
            cell = Cell{};
        }
    }

    // Writes every cell in this Screen out to the given ncplane (which must
    // be at least Width() x Height()) and requests a real terminal
    // repaint -- the one place fg/bg Color and the bold/italic/underline/
    // strikethrough/inverted trait bools actually become Notcurses calls
    // (ncplane_set_fg_*/ncplane_set_bg_*/ncplane_set_styles/
    // ncplane_putegc_yx). Called once per frame from the main loop
    // (Source/main.cpp) after every visible Widget has painted into this
    // Screen.
    // `backingPlane`, when non-null, receives the backing grid and must sit
    // below `plane`; a text cell with no background of its own then defers to
    // it rather than painting the terminal's default over it. Null means "no
    // backing layer", and the flush behaves exactly as it did before one
    // existed.
    void Flush(ncplane* plane, ncplane* backingPlane = nullptr);

    // Composites `src` onto the cell at (x, y) instead of overwriting it --
    // the translucency write path, beside the plain Cell& assignment
    // Canvas::operator[] hands out. Semantics (Widget.cpp implements them,
    // Tests/CompositingTest.cpp pins them):
    //
    //  - An empty `src.character` means "leave the destination's glyph and
    //    traits alone" -- how a wash tints a row of real text without
    //    erasing it. A non-empty one replaces glyph and traits outright.
    //  - An opaque src background is written straight through, so a fully
    //    opaque Blend is exactly today's assignment.
    //  - A translucent src background resolves per `policy` above.
    //  - A src *foreground* with no glyph of its own moves the
    //    destination's foreground instead of writing a glyph. That is the
    //    Fade paint: keep the syntax color, pull it toward the wash by the
    //    wash's own alpha -- an opaque one being simply a fade that leaves
    //    nothing of the original.
    //
    // Out-of-range coordinates are silently ignored, matching Canvas's own
    // clip-to-a-discard-cell safety net.
    void Blend(int x, int y, const Cell& src, AlphaPolicy policy = AlphaPolicy::Auto);

  private:
    int               width_;
    int               height_;
    std::vector<Cell> cells_;
    std::vector<Cell> backing_;
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

    // The backing layer beneath this Canvas's own cells -- a background that
    // renders behind the glyphs rather than in the same cell as them. Out of
    // bounds discards, exactly like operator[].
    [[nodiscard]] Cell& Backing(Point p) {
        if (p.x < 0 || p.x >= size_.width || p.y < 0 || p.y >= size_.height) {
            return discard_;
        }
        return screen_.BackingAt(box_.x_min + p.x, box_.y_min + p.y);
    }

    // Where this Canvas sits on the shared Screen. Paints that key off cell
    // position (dithering, patterns) need absolute coordinates so they stay
    // anchored to the screen rather than crawling when a widget moves.
    [[nodiscard]] Point Origin() const {
        return Point{.x = box_.x_min, .y = box_.y_min};
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

    // Screen::Blend in this Canvas's local coordinates. Deliberately
    // translates to absolute screen coordinates before blending: the dither
    // technique keys its ordered-dither matrix off cell position, so a
    // pattern or a translucent fill has to be anchored to the screen rather
    // than to the widget, or it would crawl every time the widget moves.
    void Blend(Point p, const Cell& src, AlphaPolicy policy = AlphaPolicy::Auto) {
        if (p.x < 0 || p.x >= size_.width || p.y < 0 || p.y >= size_.height) {
            return;
        }
        screen_.Blend(box_.x_min + p.x, box_.y_min + p.y, src, policy);
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

    // paste-perf-and-drag-drop follow-up: called once per complete
    // bracketed paste, with the literal pasted text -- EventLoop's own
    // drain loop accumulates everything between the terminal's own
    // \x1b[200~/\x1b[201~ markers into one string rather than ever routing
    // it through OnEvent per character (see EventLoopCallbacks::onPaste's
    // own doc comment). Default no-op; only BufferView overrides this
    // today, but any future focused widget (e.g. one recognizing a dropped
    // file path) gets this for free.
    virtual void OnPaste(std::string_view /*text*/) {
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

    // unified-left-dock follow-up: called on a widget that currently holds
    // keyboard focus when something *external* is about to make it
    // unreachable (LeftDock collapsing out from under a focused hosted
    // panel, e.g. ProjectSidebar -- either via the keyboard toggle or a
    // rail-glyph mouse click, neither of which routes back through the
    // focused widget's own OnEvent the way Escape/C-g does). Default no-op;
    // ProjectSidebar overrides it to the same effect its own Escape/C-g
    // handling already has (fire onFocusReturn_) so the keyboard doesn't
    // stay captured by a widget nothing can see or reach anymore. Distinct
    // from simply losing Focused() -- that's an effect of TakeFocus()
    // moving the flat registry pointer elsewhere, not a request a widget
    // can react to; this is the deliberate "you should let go" signal
    // TakeFocus() alone doesn't provide.
    virtual void OnFocusPreempted() {
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
