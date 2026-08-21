#include "Emulator.h"

#include <cstring>
#include <deque>
#include <vector>

#include <vterm.h>

#include "Text/Utf8.h"

namespace ned::editor::terminal {

namespace {

    bool IsBareModifierKey(std::uint32_t id) {
        return id >= NCKEY_LSHIFT && id <= NCKEY_L5SHIFT;
    }

    // The named-key subset a terminal application can actually receive --
    // everything else synthesized (media keys, ...) is dropped by SendKey.
    std::optional<VTermKey> VTermKeyFor(std::uint32_t id) {
        switch (id) {
            case NCKEY_ENTER:
                return VTERM_KEY_ENTER;
            case NCKEY_TAB:
                return VTERM_KEY_TAB;
            case NCKEY_BACKSPACE:
                return VTERM_KEY_BACKSPACE;
            case NCKEY_ESC:
                return VTERM_KEY_ESCAPE;
            case NCKEY_UP:
                return VTERM_KEY_UP;
            case NCKEY_DOWN:
                return VTERM_KEY_DOWN;
            case NCKEY_LEFT:
                return VTERM_KEY_LEFT;
            case NCKEY_RIGHT:
                return VTERM_KEY_RIGHT;
            case NCKEY_INS:
                return VTERM_KEY_INS;
            case NCKEY_DEL:
                return VTERM_KEY_DEL;
            case NCKEY_HOME:
                return VTERM_KEY_HOME;
            case NCKEY_END:
                return VTERM_KEY_END;
            case NCKEY_PGUP:
                return VTERM_KEY_PAGEUP;
            case NCKEY_PGDOWN:
                return VTERM_KEY_PAGEDOWN;
            default:
                if (id >= NCKEY_F00 && id <= NCKEY_F12) {
                    return static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + (id - NCKEY_F00));
                }
                return std::nullopt;
        }
    }

} // namespace

struct Emulator::State {
    VTerm*       vt     = nullptr;
    VTermScreen* screen = nullptr;

    VTermPos cursor{.row = 0, .col = 0};
    bool     cursorVisible = true;

    std::deque<std::vector<VTermScreenCell>> scrollback;
    std::string                              pendingOutput;
};

namespace {

    // libvterm callback bodies. Return 1 for "handled" throughout -- the
    // library treats 0 as "fall through to any default handling."

    int OnMoveCursor(VTermPos pos, VTermPos /*oldpos*/, int visible, void* user) {
        auto* state          = static_cast<Emulator::State*>(user);
        state->cursor        = pos;
        state->cursorVisible = visible != 0;
        return 1;
    }

    int OnSetTermProp(VTermProp prop, VTermValue* value, void* user) {
        auto* state = static_cast<Emulator::State*>(user);
        if (prop == VTERM_PROP_CURSORVISIBLE) {
            state->cursorVisible = value->boolean;
        }
        return 1;
    }

    int OnScrollbackPushLine(int cols, const VTermScreenCell* cells, void* user) {
        auto* state = static_cast<Emulator::State*>(user);
        state->scrollback.emplace_back(cells, cells + cols);
        while (state->scrollback.size() > static_cast<std::size_t>(Emulator::kScrollbackLines)) {
            state->scrollback.pop_front();
        }
        return 1;
    }

    int OnScrollbackPopLine(int cols, VTermScreenCell* cells, void* user) {
        auto* state = static_cast<Emulator::State*>(user);
        if (state->scrollback.empty()) {
            return 0;
        }
        const std::vector<VTermScreenCell>& line = state->scrollback.back();
        for (int col = 0; col < cols; ++col) {
            if (col < static_cast<int>(line.size())) {
                cells[col] = line[static_cast<std::size_t>(col)];
            }
            else {
                std::memset(&cells[col], 0, sizeof(VTermScreenCell));
                cells[col].width   = 1;
                cells[col].fg.type = VTERM_COLOR_DEFAULT_FG;
                cells[col].bg.type = VTERM_COLOR_DEFAULT_BG;
            }
        }
        state->scrollback.pop_back();
        return 1;
    }

    int OnScrollbackClear(void* user) {
        static_cast<Emulator::State*>(user)->scrollback.clear();
        return 1;
    }

    void OnOutput(const char* bytes, std::size_t length, void* user) {
        static_cast<Emulator::State*>(user)->pendingOutput.append(bytes, length);
    }

    constexpr VTermScreenCallbacks kScreenCallbacks{
        .damage      = nullptr,
        .moverect    = nullptr,
        .movecursor  = &OnMoveCursor,
        .settermprop = &OnSetTermProp,
        .bell        = nullptr,
        .resize      = nullptr,
        .sb_pushline = &OnScrollbackPushLine,
        .sb_popline  = &OnScrollbackPopLine,
        .sb_clear    = &OnScrollbackClear,
    };

    ui::Color ColorFrom(const VTermScreen* screen, VTermColor color, bool isForeground) {
        if ((isForeground && VTERM_COLOR_IS_DEFAULT_FG(&color)) || (!isForeground && VTERM_COLOR_IS_DEFAULT_BG(&color))) {
            return ui::Color::Default;
        }
        if (VTERM_COLOR_IS_INDEXED(&color)) {
            if (color.indexed.idx < 16) {
                return ui::Color::Palette(color.indexed.idx);
            }
            // 256-palette entries past the classic 16 have a well-defined
            // RGB value libvterm can compute; ui::Color has no 256-palette
            // kind, so convert.
            vterm_screen_convert_color_to_rgb(screen, &color);
        }
        return ui::Color::RGB(color.rgb.red, color.rgb.green, color.rgb.blue);
    }

    Cell ConvertCell(const VTermScreen* screen, const VTermScreenCell& raw) {
        Cell cell;
        cell.character.clear();
        for (std::size_t i = 0; i < VTERM_MAX_CHARS_PER_CELL && raw.chars[i] != 0; ++i) {
            cell.character += text::EncodeCodepointUtf8(static_cast<char32_t>(raw.chars[i]));
        }
        if (cell.character.empty() && raw.width >= 1) {
            cell.character = " ";
        }
        cell.foreground    = ColorFrom(screen, raw.fg, true);
        cell.background    = ColorFrom(screen, raw.bg, false);
        cell.bold          = raw.attrs.bold != 0;
        cell.italic        = raw.attrs.italic != 0;
        cell.underlined    = raw.attrs.underline != VTERM_UNDERLINE_OFF;
        cell.strikethrough = raw.attrs.strike != 0;
        cell.inverted      = raw.attrs.reverse != 0;
        cell.width         = raw.width;
        return cell;
    }

} // namespace

Emulator::Emulator(int rows, int cols) : state_(std::make_unique<State>()) {
    state_->vt = vterm_new(std::max(1, rows), std::max(1, cols));
    vterm_set_utf8(state_->vt, 1);
    vterm_output_set_callback(state_->vt, &OnOutput, state_.get());

    state_->screen = vterm_obtain_screen(state_->vt);
    vterm_screen_set_callbacks(state_->screen, &kScreenCallbacks, state_.get());
    vterm_screen_enable_altscreen(state_->screen, 1);
    vterm_screen_reset(state_->screen, 1);
}

Emulator::~Emulator() {
    if (state_ && state_->vt != nullptr) {
        vterm_free(state_->vt);
    }
}

Emulator::Emulator(Emulator&& other) noexcept = default;

Emulator& Emulator::operator=(Emulator&& other) noexcept {
    if (this != &other) {
        if (state_ && state_->vt != nullptr) {
            vterm_free(state_->vt);
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

int Emulator::Rows() const {
    int rows = 0;
    int cols = 0;
    vterm_get_size(state_->vt, &rows, &cols);
    return rows;
}

int Emulator::Cols() const {
    int rows = 0;
    int cols = 0;
    vterm_get_size(state_->vt, &rows, &cols);
    return cols;
}

void Emulator::Feed(std::string_view bytes) {
    vterm_input_write(state_->vt, bytes.data(), bytes.size());
    vterm_screen_flush_damage(state_->screen);
}

void Emulator::Resize(int rows, int cols) {
    vterm_set_size(state_->vt, std::max(1, rows), std::max(1, cols));
}

Cell Emulator::CellAt(int row, int col) const {
    VTermScreenCell raw;
    std::memset(&raw, 0, sizeof raw);
    if (vterm_screen_get_cell(state_->screen, VTermPos{.row = row, .col = col}, &raw) == 0) {
        return Cell{};
    }
    return ConvertCell(state_->screen, raw);
}

std::optional<ui::Point> Emulator::Cursor() const {
    if (!state_->cursorVisible) {
        return std::nullopt;
    }
    return ui::Point{.x = state_->cursor.col, .y = state_->cursor.row};
}

bool Emulator::SendKey(const ncinput& input) {
    // A held key's release (kitty keyboard protocol only) carries no
    // terminal meaning; forwarding it would double every keystroke.
    if (input.evtype == NCTYPE_RELEASE) {
        return false;
    }
    if (IsBareModifierKey(input.id)) {
        return false;
    }

    unsigned modifier = VTERM_MOD_NONE;
    if (ncinput_shift_p(&input)) {
        modifier |= VTERM_MOD_SHIFT;
    }
    // Legacy Alt lives only in the deprecated bool -- see
    // KeyTranslation.cpp's confirmed Notcurses 3.0.14 bug note.
    if (ncinput_alt_p(&input) || input.alt) {
        modifier |= VTERM_MOD_ALT;
    }
    if (ncinput_ctrl_p(&input)) {
        modifier |= VTERM_MOD_CTRL;
    }

    if (const std::optional<VTermKey> key = VTermKeyFor(input.id)) {
        vterm_keyboard_key(state_->vt, *key, static_cast<VTermModifier>(modifier));
        return true;
    }

    if (input.id == 0 || nckey_synthesized_p(input.id)) {
        return false;
    }

    std::uint32_t codepoint = input.id;
    if ((modifier & VTERM_MOD_CTRL) != 0 && codepoint >= 'A' && codepoint <= 'Z' && !ncinput_shift_p(&input)) {
        // Notcurses pre-normalizes a raw C0 control byte to the uppercase
        // letter (see KeyTranslation.cpp's DecodeBaseKey comment);
        // libvterm's own Ctrl encoding (`c &= 0x1f`) expects the lowercase
        // form, and CSIu-capable encodings would otherwise report a
        // spurious Shift.
        codepoint = codepoint - 'A' + 'a';
    }
    else if (modifier == VTERM_MOD_NONE && codepoint >= 1 && codepoint <= 26) {
        // Defensive fallback mirroring DecodeBaseKey's: a terminal that
        // sends the bare control byte without ever setting the modifier
        // bit. Forward as the Ctrl'd letter so libvterm re-derives the
        // same byte.
        modifier  = VTERM_MOD_CTRL;
        codepoint = 'a' + codepoint - 1;
    }

    vterm_keyboard_unichar(state_->vt, codepoint, static_cast<VTermModifier>(modifier));
    return true;
}

std::string Emulator::TakeOutput() {
    std::string output = std::move(state_->pendingOutput);
    state_->pendingOutput.clear();
    return output;
}

int Emulator::ScrollbackSize() const {
    return static_cast<int>(state_->scrollback.size());
}

Cell Emulator::ScrollbackCellAt(int line, int col) const {
    if (line < 0 || line >= ScrollbackSize()) {
        return Cell{};
    }
    const std::vector<VTermScreenCell>& cells = state_->scrollback[static_cast<std::size_t>(line)];
    if (col < 0 || col >= static_cast<int>(cells.size())) {
        return Cell{};
    }
    return ConvertCell(state_->screen, cells[static_cast<std::size_t>(col)]);
}

} // namespace ned::editor::terminal
