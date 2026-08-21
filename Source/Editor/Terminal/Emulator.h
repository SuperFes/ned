//
// RAII wrapper around a libvterm VTerm/VTermScreen pair (terminal-panel
// follow-up) -- the same "wrap the C library behind an idiomatic C++ layer"
// approach Source/Editor/TreeSitter/ and Source/Janet/ already established.
// This is the pure emulation state: bytes from a pty go in via Feed(), a
// queryable cell grid (plus scrollback ring) comes out, and keyboard input
// goes in via SendKey() with the encoded pty-bound reply drained via
// TakeOutput(). No UI, no pty, no threads -- Source/UI/TerminalPanel is what
// composes this with a PtyProcess and paints the result.
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
