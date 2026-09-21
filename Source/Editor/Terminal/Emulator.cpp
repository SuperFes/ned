#include "Emulator.h"

#include <array>
#include <cstring>
#include <deque>
#include <optional>
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

    Emulator::MouseMode mouseMode = Emulator::MouseMode::None;

    // libvterm hands string-valued properties and selections over in
    // fragments, so each has a pending accumulator alongside its completed
    // value.
    std::string                title;
    std::string                pendingTitle;
    std::string                pendingSelection;
    std::optional<std::string> clipboardText;

    // Scratch libvterm base64-decodes an OSC 52 payload into before handing
    // it to the selection callback; a payload larger than this simply
    // arrives as several fragments.
    std::array<char, 16384> selectionBuffer{};
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

    // Appends one libvterm string fragment to `pending`, returning true
    // once the value is complete (and left whole in `pending`).
    bool AppendFragment(std::string& pending, const VTermStringFragment& fragment) {
        if (fragment.initial != 0) {
            pending.clear();
        }
        pending.append(fragment.str, fragment.len);
        return fragment.final != 0;
    }

    Emulator::MouseMode MouseModeFor(int property) {
        switch (property) {
            case VTERM_PROP_MOUSE_CLICK:
                return Emulator::MouseMode::Click;
            case VTERM_PROP_MOUSE_DRAG:
                return Emulator::MouseMode::Drag;
            case VTERM_PROP_MOUSE_MOVE:
                return Emulator::MouseMode::Move;
            default:
                return Emulator::MouseMode::None;
        }
    }

    int OnSetTermProp(VTermProp prop, VTermValue* value, void* user) {
        auto* state = static_cast<Emulator::State*>(user);
        if (prop == VTERM_PROP_CURSORVISIBLE) {
            state->cursorVisible = value->boolean;
        }
        else if (prop == VTERM_PROP_MOUSE) {
            state->mouseMode = MouseModeFor(value->number);
        }
        else if (prop == VTERM_PROP_TITLE) {
            if (AppendFragment(state->pendingTitle, value->string)) {
                state->title = std::move(state->pendingTitle);
                state->pendingTitle.clear();
            }
        }
        return 1;
    }

    int OnSelectionSet(VTermSelectionMask /*mask*/, VTermStringFragment fragment, void* user) {
        auto* state = static_cast<Emulator::State*>(user);
        if (AppendFragment(state->pendingSelection, fragment)) {
            state->clipboardText = std::move(state->pendingSelection);
            state->pendingSelection.clear();
        }
        return 1;
    }

    // Left unanswered on purpose -- see Emulator::TakeClipboardText's own
    // doc comment. 0 is libvterm's "not handled."
    int OnSelectionQuery(VTermSelectionMask /*mask*/, void* /*user*/) {
        return 0;
    }

    constexpr VTermSelectionCallbacks kSelectionCallbacks{
        .set   = &OnSelectionSet,
        .query = &OnSelectionQuery,
    };

    std::optional<int> VTermButtonFor(ui::MouseEvent::Button button) {
        switch (button) {
            case ui::MouseEvent::Button::Left:
                return 1;
            case ui::MouseEvent::Button::Middle:
                return 2;
            case ui::MouseEvent::Button::Right:
                return 3;
            // Wheel notches are xterm's device group 4-7, which libvterm
            // re-encodes into the 64-and-up report codes itself.
            case ui::MouseEvent::Button::WheelUp:
                return 4;
            case ui::MouseEvent::Button::WheelDown:
                return 5;
            case ui::MouseEvent::Button::WheelLeft:
                return 6;
            case ui::MouseEvent::Button::WheelRight:
                return 7;
            case ui::MouseEvent::Button::None:
                break;
        }
        return std::nullopt;
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
    // OSC 52: libvterm parses and base64-decodes the payload itself once a
    // selection buffer is registered, so nothing here touches base64.
    vterm_state_set_selection_callbacks(vterm_obtain_state(state_->vt), &kSelectionCallbacks, state_.get(),
                                        state_->selectionBuffer.data(), state_->selectionBuffer.size());
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

Emulator::MouseMode Emulator::MouseReporting() const {
    return state_->mouseMode;
}

bool Emulator::SendMouse(const ui::MouseEvent& mouse) {
    if (state_->mouseMode == MouseMode::None) {
        return false;
    }

    unsigned modifier = VTERM_MOD_NONE;
    if (mouse.shift) {
        modifier |= VTERM_MOD_SHIFT;
    }
    if (mouse.meta) {
        modifier |= VTERM_MOD_ALT;
    }
    if (mouse.control) {
        modifier |= VTERM_MOD_CTRL;
    }
    const auto vtermModifier = static_cast<VTermModifier>(modifier);

    // Always first: libvterm records the position unconditionally and
    // encodes it into the button report that follows, while only *emitting*
    // a motion report in the modes that asked for one.
    vterm_mouse_move(state_->vt, mouse.at.y, mouse.at.x, vtermModifier);

    const std::optional<int> button = VTermButtonFor(mouse.button);
    if (!button.has_value() || mouse.motion == ui::MouseEvent::Motion::Moved) {
        return true;
    }
    if (*button >= 4) {
        // A wheel notch has no release half in any reporting mode (xterm's
        // convention) -- an application that saw one would scroll twice.
        if (mouse.motion == ui::MouseEvent::Motion::Pressed) {
            vterm_mouse_button(state_->vt, *button, true, vtermModifier);
        }
        return true;
    }
    vterm_mouse_button(state_->vt, *button, mouse.motion == ui::MouseEvent::Motion::Pressed, vtermModifier);
    return true;
}

const std::string& Emulator::Title() const {
    return state_->title;
}

std::optional<std::string> Emulator::TakeClipboardText() {
    std::optional<std::string> text = std::move(state_->clipboardText);
    state_->clipboardText.reset();
    return text;
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
