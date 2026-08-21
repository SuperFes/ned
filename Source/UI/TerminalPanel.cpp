#include "TerminalPanel.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "Border.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"
#include "UI/EventLoop.h"

namespace ned::ui {

namespace {

    // Close matches TabBar's per-tab icon; the triangles match
    // ScrollArrowButton's (already established there as portable without a
    // Nerd Font). Each is drawn bracketed -- [▼][▲][×] -- so the buttons
    // read as buttons against the plain title line rather than stray
    // glyphs, right-aligned ending one column short of the row's edge.
    constexpr char32_t kMinimizeIcon = U'▼';
    constexpr char32_t kMaximizeIcon = U'▲';
    constexpr char32_t kCloseIcon    = U'×';

    // Column of each button's opening bracket, counted back from width:
    // [▼] at width-10, [▲] at width-7, [×] at width-4.
    constexpr int kMinimizeOffset = 10;
    constexpr int kMaximizeOffset = 7;
    constexpr int kCloseOffset    = 4;

    std::vector<std::string> ShellArgv() {
        // The user's own login shell -- the same trust boundary every other
        // user-configured command in this codebase crosses.
        const char* shell = std::getenv("SHELL");
        return {(shell != nullptr && *shell != '\0') ? shell : "/bin/sh"};
    }

} // namespace

const editor::KeyChord TerminalPanel::kToggleChord{.Control = true, .Codepoint = U'`'};

TerminalPanel::TerminalPanel(const Theme& theme) : theme_(theme), emulator_(24, 80) {
}

void TerminalPanel::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
}

void TerminalPanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

void TerminalPanel::EnsureStarted() {
    if (ShellRunning() || eventLoop_ == nullptr) {
        return;
    }
    // A dead PtyProcess parked here since HandleExit is only now safe to
    // destroy (no callback of its own is on the stack anymore).
    pty_.reset();
    if (exited_) {
        // Fresh emulator for the fresh shell -- the exited session's output
        // stays readable until this respawn, not beyond it.
        emulator_ = editor::terminal::Emulator(ContentRows(), ContentCols());
        exited_   = false;
    }
    pty_ = std::make_unique<editor::terminal::PtyProcess>(
        ShellArgv(), ContentRows(), ContentCols(), *eventLoop_, [this](std::string_view chunk) { Feed(chunk); },
        [this](std::optional<int>) { HandleExit(); });
    writeSink_ = [this](std::string_view data) { pty_->Write(data); };
}

bool TerminalPanel::ShellRunning() const {
    return pty_ != nullptr && !exited_;
}

void TerminalPanel::Feed(std::string_view bytes) {
    emulator_.Feed(bytes);
    // Feeding can *generate* pty-bound bytes, not just consume them:
    // libvterm answers terminal queries (Primary Device Attributes `\e[c`,
    // cursor-position `\e[6n`, ...) by queueing the reply in its output
    // buffer. Forward immediately -- draining only after a keypress left
    // fish's startup DA query unanswered for its whole 10-second timeout, a
    // real, screenshot-confirmed bug ("could not read response to Primary
    // Device Attribute query"), not a hypothetical.
    ForwardPendingOutput();
    // Output never yanks a scrolled-back view to the bottom (see the
    // scrollbackOffset_ comment), but the ring it indexes into may just
    // have grown/shrunk; keep the offset valid.
    scrollbackOffset_ = std::clamp(scrollbackOffset_, 0, emulator_.ScrollbackSize());
}

void TerminalPanel::SetWriteSinkForTesting(std::function<void(std::string_view)> sink) {
    writeSink_ = std::move(sink);
}

void TerminalPanel::HandleExit() {
    // Note what must NOT happen here: pty_.reset(). This runs inside the
    // dead PtyProcess's own Post-marshaled onExit callback -- destroying it
    // would tear down the std::function currently executing. It stays
    // parked until EnsureStarted.
    writeSink_ = nullptr;
    Feed("\r\n[process exited]\r\n");
    exited_ = true;
}

int TerminalPanel::ContentRows() const {
    return std::max(1, size().height - 1);
}

int TerminalPanel::ContentCols() const {
    return std::max(1, size().width);
}

void TerminalPanel::OnResize(Size /*previous*/) {
    emulator_.Resize(ContentRows(), ContentCols());
    if (ShellRunning()) {
        pty_->Resize(ContentRows(), ContentCols());
    }
}

void TerminalPanel::ForwardPendingOutput() {
    const std::string output = emulator_.TakeOutput();
    if (!output.empty() && writeSink_) {
        writeSink_(output);
    }
}

void TerminalPanel::ScrollBy(int deltaLines) {
    scrollbackOffset_ = std::clamp(scrollbackOffset_ + deltaLines, 0, emulator_.ScrollbackSize());
}

TerminalPanel::TitleButton TerminalPanel::TitleButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForTitleButtons) {
        return TitleButton::None;
    }
    const auto inButton = [&](int bracketColumn) {
        return local.x >= width - bracketColumn && local.x <= width - bracketColumn + 2;
    };
    if (inButton(kCloseOffset)) {
        return TitleButton::Close;
    }
    if (inButton(kMaximizeOffset)) {
        return TitleButton::Maximize;
    }
    if (inButton(kMinimizeOffset)) {
        return TitleButton::Minimize;
    }
    return TitleButton::None;
}

void TerminalPanel::SetOnLayoutChange(std::function<void()> onLayoutChange) {
    onLayoutChange_ = std::move(onLayoutChange);
}

void TerminalPanel::CloseSession() {
    // Safe context to destroy the pty: this is only ever reached from
    // OnEvent (a click), never from inside one of the pty's own callbacks
    // (the HandleExit constraint doesn't apply).
    pty_.reset();
    writeSink_        = nullptr;
    exited_           = false;
    scrollbackOffset_ = 0;
    emulator_         = editor::terminal::Emulator(ContentRows(), ContentCols());
}

std::optional<Point> TerminalPanel::CursorPosition() const {
    if (exited_ || scrollbackOffset_ > 0) {
        return std::nullopt;
    }
    const std::optional<Point> cursor = emulator_.Cursor();
    if (!cursor || cursor->x >= ContentCols() || cursor->y >= ContentRows()) {
        return std::nullopt;
    }
    return Point{.x = cursor->x, .y = cursor->y + 1}; // +1: past the title row
}

void TerminalPanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Title/divider row -- theme.border normally, borderAccent while
    // focused, the ProjectSidebar convention.
    const Brush&      frameBrush = Focused() ? theme_.borderAccent : theme_.border;
    const std::string horizontal = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = 0}];
        cell.character = horizontal;
        frameBrush.ApplyTo(cell);
    }
    std::string title = "Terminal";
    if (exited_) {
        title += " (exited)";
    }
    else if (scrollbackOffset_ > 0) {
        title += " (scrollback)";
    }
    DrawBorderTitle(canvas, title, frameBrush);
    // Title-row buttons -- the mouse escape hatches that work on every
    // terminal (C-` needs the kitty keyboard protocol; see the header
    // comment). TitleButtonAt is the matching hit test, sharing the same
    // offset constants so a click can never disagree with where this drew.
    if (width >= kMinWidthForTitleButtons) {
        const auto drawButton = [&](int bracketColumn, char32_t icon) {
            const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(icon), "]"};
            for (int i = 0; i < 3; ++i) {
                Cell& cell     = canvas[{.x = width - bracketColumn + i, .y = 0}];
                cell.character = glyphs[i];
                frameBrush.ApplyTo(cell);
            }
        };
        drawButton(kMinimizeOffset, kMinimizeIcon);
        drawButton(kMaximizeOffset, kMaximizeIcon);
        drawButton(kCloseOffset, kCloseIcon);
    }

    // Content rows: a window over ring + live screen, offset lines up from
    // the bottom (offset 0 shows exactly the live screen).
    const int contentRows    = height - 1;
    const int scrollbackSize = emulator_.ScrollbackSize();
    for (int row = 0; row < contentRows; ++row) {
        const int lineIndex = scrollbackSize - scrollbackOffset_ + row;
        for (int col = 0; col < width; ++col) {
            const editor::terminal::Cell source =
                lineIndex < scrollbackSize ? emulator_.ScrollbackCellAt(lineIndex, col) : emulator_.CellAt(lineIndex - scrollbackSize, col);

            Cell& cell         = canvas[{.x = col, .y = row + 1}];
            cell.character     = source.character;
            cell.bold          = source.bold;
            cell.italic        = source.italic;
            cell.underlined    = source.underlined;
            cell.strikethrough = source.strikethrough;
            cell.inverted      = source.inverted;
            // Terminal-default colors resolve to ned's own theme, which is
            // also what makes the drawer opaque over the buffer beneath.
            cell.foreground_color = source.foreground == Color::Default ? theme_.defaultForeground : source.foreground;
            cell.background_color = source.background == Color::Default ? theme_.background : source.background;
        }
    }
}

bool TerminalPanel::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false;
        }
        if (mouse->button == MouseEvent::Button::WheelUp) {
            ScrollBy(3);
            return true;
        }
        if (mouse->button == MouseEvent::Button::WheelDown) {
            ScrollBy(-3);
            return true;
        }
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            switch (TitleButtonAt(mouse->at)) {
                case TitleButton::Minimize:
                    // Hide, shell kept alive -- identical to toggle-hide.
                    if (onToggleRequest_) {
                        onToggleRequest_();
                    }
                    return true;
                case TitleButton::Maximize:
                    maximized_ = !maximized_;
                    if (onLayoutChange_) {
                        onLayoutChange_();
                    }
                    return true;
                case TitleButton::Close:
                    // Kill the shell outright, then hide; the next show
                    // spawns a fresh one.
                    CloseSession();
                    if (onToggleRequest_) {
                        onToggleRequest_();
                    }
                    return true;
                case TitleButton::None:
                    break;
            }
            TakeFocus();
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (chord == kToggleChord) {
        if (onToggleRequest_) {
            onToggleRequest_();
        }
        return true;
    }
    if (chord.has_value() && chord->Special == editor::SpecialKey::PageUp && chord->Shift) {
        ScrollBy(std::max(1, ContentRows() - 1));
        return true;
    }
    if (chord.has_value() && chord->Special == editor::SpecialKey::PageDown && chord->Shift) {
        ScrollBy(-std::max(1, ContentRows() - 1));
        return true;
    }
    if (exited_) {
        if (chord.has_value() && chord->Special == editor::SpecialKey::Enter) {
            EnsureStarted();
        }
        return true; // an exited panel swallows keys -- nothing to forward to
    }

    if (emulator_.SendKey(event.raw())) {
        scrollbackOffset_ = 0; // typing snaps back to the live view
        ForwardPendingOutput();
        return true;
    }
    return false;
}

} // namespace ned::ui
