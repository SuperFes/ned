//
// RAII wrapper around a libvterm VTerm/VTermScreen pair (terminal-panel
// follow-up) -- the same "wrap the C library behind an idiomatic C++ layer"
// approach Source/Editor/Grammar/ and Source/Janet/ already established.
// This is the pure emulation state: bytes from a pty go in via Feed(), a
// queryable cell grid (plus scrollback ring) comes out, and keyboard input
// goes in via SendKey() with the encoded pty-bound reply drained via
// TakeOutput(). No UI, no pty, no threads -- Source/UI/TerminalPanel is what
// composes this with a PtyProcess and paints the result.
//
// Three application-driven side channels ride the same in-out shape:
// SendMouse forwards a click/wheel/drag once the application has asked for
// mouse reporting (MouseReporting), Title() carries whatever OSC 0/2 title
// it set, and TakeClipboardText() drains an OSC 52 clipboard write. All
// three stop here -- pushing the title into a tab strip or the text into a
// system clipboard is the host panel's job, not this layer's.
//
// SendKey is deliberately the one place the four real ncinput decoding
// gotchas KeyTranslation.cpp documents (pre-uppercased Ctrl letters, the
// legacy `alt` bool Notcurses never syncs into `modifiers`, kitty-protocol
// RELEASE events, kitty-protocol bare-modifier presses) are re-handled for
// the forward-to-a-terminal case -- it maps to vterm_keyboard_unichar/
// vterm_keyboard_key and lets libvterm do all escape-sequence encoding
// rather than hand-encoding sequences here.
//

#ifndef NED_EDITOR_TERMINAL_EMULATOR_H
#define NED_EDITOR_TERMINAL_EMULATOR_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <notcurses/notcurses.h>

#include "UI/Widget.h"

namespace ned::editor::terminal {

// One emulated screen cell, already converted to this codebase's own
// UI-facing vocabulary (ui::Color, the same trait bools ui::Cell carries) so
// TerminalPanel's Paint() is a field-by-field copy. `width` is libvterm's
// own column width for the cell (2 for the lead cell of a double-width
// glyph); a cell in the shadow of a wide predecessor comes back with an
// empty `character`.
struct Cell {
    std::string character = " ";
    ui::Color   foreground;
    ui::Color   background;
    bool        bold          = false;
    bool        italic        = false;
    bool        underlined    = false;
    bool        strikethrough = false;
    bool        inverted      = false;
    int         width         = 1;
};

class Emulator {
  public:
    // Scrollback ring capacity (lines). The ring exists for correctness as
    // well as viewing: libvterm's sb_popline pulls lines back onto the
    // screen when the terminal regrows, so even a hypothetical no-viewing
    // build would still need it.
    static constexpr int kScrollbackLines = 2000;

    Emulator(int rows, int cols);
    ~Emulator();

    Emulator(Emulator&& other) noexcept;
    Emulator& operator=(Emulator&& other) noexcept;
    Emulator(const Emulator&)            = delete;
    Emulator& operator=(const Emulator&) = delete;

    [[nodiscard]] int Rows() const;
    [[nodiscard]] int Cols() const;

    // Feeds raw pty output bytes through the emulator and flushes damage so
    // CellAt/Cursor reflect them immediately.
    void Feed(std::string_view bytes);

    void Resize(int rows, int cols);

    [[nodiscard]] Cell CellAt(int row, int col) const;

    // Cursor position as {x = col, y = row}, or nullopt while the
    // application has hidden it (DECTCEM).
    [[nodiscard]] std::optional<ui::Point> Cursor() const;

    // Encodes one key press for the pty (retrieved via TakeOutput). Returns
    // false -- with nothing emitted -- for input that carries no terminal
    // meaning (mouse, RELEASE events, bare modifier presses, unmapped
    // synthesized keys).
    bool SendKey(const ncinput& input);

    // Mouse reporting the running application has asked for (DECSET
    // 1000/1002/1003, libvterm's VTERM_PROP_MOUSE). None -- the default,
    // and what a plain shell leaves it at -- is what lets a host panel keep
    // the wheel and click-drag for its own scrollback and selection.
    enum class MouseMode { None,
                           Click,
                           Drag,
                           Move };

    [[nodiscard]] MouseMode MouseReporting() const;

    // Encodes one mouse event for the pty (retrieved via TakeOutput);
    // `mouse.at` is a cell on the live screen (x = column, y = row).
    // Returns false -- with nothing emitted -- while the application wants
    // no reporting at all. A true return means "consumed", not "bytes were
    // produced": libvterm itself drops what the *active* mode doesn't want
    // (motion outside drag/move modes), which is exactly the filtering a
    // caller would otherwise duplicate.
    bool SendMouse(const ui::MouseEvent& mouse);

    // The window title the application last set (OSC 0/2). Empty until one
    // is set; only ever replaced by a newer title.
    [[nodiscard]] const std::string& Title() const;

    // Drains a completed OSC 52 clipboard-set, already base64-decoded by
    // libvterm. Last-writer-wins -- only the most recent completed set
    // survives to the next drain, which is what a clipboard means anyway.
    // An OSC 52 *query* is deliberately never answered: replying would hand
    // the user's clipboard to any process running in the terminal, and
    // xterm defaults the same way.
    [[nodiscard]] std::optional<std::string> TakeClipboardText();

    // Drains the pty-bound bytes SendKey (and libvterm's own query replies)
    // encoded since the last call.
    [[nodiscard]] std::string TakeOutput();

    [[nodiscard]] int ScrollbackSize() const;
    // line 0 is the oldest retained scrollback line.
    [[nodiscard]] Cell ScrollbackCellAt(int line, int col) const;

    // All state libvterm's C callbacks need lives behind one stable heap
    // allocation registered as their user pointer -- what keeps Emulator
    // itself freely movable without re-registering callbacks. Public only
    // so Emulator.cpp's file-local callback functions can name it (the
    // SetBox_ "internal seam, not real API" precedent); defined in the
    // .cpp, opaque here.
    struct State;

  private:
    std::unique_ptr<State> state_;
};

} // namespace ned::editor::terminal

#endif // NED_EDITOR_TERMINAL_EMULATOR_H
