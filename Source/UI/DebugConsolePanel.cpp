#include "DebugConsolePanel.h"

#include <algorithm>

#include "Border.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Mirrors AcpPanel.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, it's a one-line predicate private to each consumer there
    // too.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    constexpr int      kMinWidthForCloseButton = 8;
    constexpr int      kCloseOffset            = 4; // column of '[' counted back from width, matches TerminalPanel/AcpPanel's own offset
    constexpr char32_t kCloseIcon              = U'×';
    constexpr std::size_t kMaxHistoryLines      = 500; // TerminalPanel's own scrollback-cap precedent, applied here too

    std::string StateLabel(editor::dap::DapManager::SessionState state) {
        switch (state) {
            case editor::dap::DapManager::SessionState::Starting:
                return "starting";
            case editor::dap::DapManager::SessionState::Running:
                return "running";
            case editor::dap::DapManager::SessionState::Stopped:
                return "stopped";
            case editor::dap::DapManager::SessionState::Inactive:
                return "inactive";
        }
        return "inactive";
    }

} // namespace

DebugConsolePanel::DebugConsolePanel(const Theme& theme) : theme_(theme), prompt_("debug> ") {
}

void DebugConsolePanel::SetDapManager(editor::dap::DapManager* dapManager) {
    dapManager_ = dapManager;
}

void DebugConsolePanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

Brush DebugConsolePanel::BrushForStyle(DisplayStyle style) const {
    switch (style) {
        case DisplayStyle::Dim:
            return Brush{.background = theme_.background, .foreground = theme_.commentForeground};
        case DisplayStyle::Error:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticError};
        case DisplayStyle::Plain:
            break;
    }
    return Brush{.background = theme_.background, .foreground = theme_.defaultForeground};
}

bool DebugConsolePanel::CloseButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForCloseButton) {
        return false;
    }
    return local.x >= width - kCloseOffset && local.x <= width - kCloseOffset + 2;
}

void DebugConsolePanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Opaque fill first -- this panel floats over BufferView the same way
    // TerminalPanel/AcpPanel do, so every cell in its Box must be painted
    // regardless of content, or the buffer beneath would show through.
    const Brush plainBrush = BrushForStyle(DisplayStyle::Plain);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            plainBrush.ApplyTo(cell);
        }
    }

    // Title/divider row.
    const Brush&      frameBrush = Focused() ? theme_.borderAccent : theme_.border;
    const std::string horizontal = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = 0}];
        cell.character = horizontal;
        frameBrush.ApplyTo(cell);
    }
    const std::string title =
        "Debug console [" + StateLabel(dapManager_ ? dapManager_->State() : editor::dap::DapManager::SessionState::Inactive) + "]";
    DrawBorderTitle(canvas, title, frameBrush);
    if (width >= kMinWidthForCloseButton) {
        const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(kCloseIcon), "]"};
        for (int i = 0; i < 3; ++i) {
            Cell& cell     = canvas[{.x = width - kCloseOffset + i, .y = 0}];
            cell.character = glyphs[i];
            frameBrush.ApplyTo(cell);
        }
    }

    if (height < 2) {
        return;
    }

    // Content rows: the tail of history_ that fits, top-aligned within the
    // window (i.e. the window itself is anchored to the most recent lines)
    // -- no scrollback beyond that in v1, same cut TerminalPanel/AcpPanel
    // both carry.
    const int contentRows = height - 2;
    if (contentRows > 0) {
        const int start = std::max(0, static_cast<int>(history_.size()) - contentRows);
        for (int row = 0; row < contentRows; ++row) {
            const std::size_t lineIndex = static_cast<std::size_t>(start + row);
            if (lineIndex >= history_.size()) {
                continue;
            }
            const DisplayLine& line  = history_[lineIndex];
            const Brush        brush = BrushForStyle(line.style);
            for (int col = 0; col < width && col < static_cast<int>(line.text.size()); ++col) {
                Cell& cell     = canvas[{.x = col, .y = row + 1}];
                cell.character = std::string(1, line.text[static_cast<std::size_t>(col)]);
                brush.ApplyTo(cell);
            }
        }
    }

    // Input row.
    const int         inputRow = height - 1;
    const std::string text     = prompt_.StatusText();
    for (int col = 0; col < width && col < static_cast<int>(text.size()); ++col) {
        Cell& cell     = canvas[{.x = col, .y = inputRow}];
        cell.character = std::string(1, text[static_cast<std::size_t>(col)]);
        plainBrush.ApplyTo(cell);
    }
    const int caretCol = static_cast<int>(text.size());
    if (caretCol < width) {
        Cell& cell     = canvas[{.x = caretCol, .y = inputRow}];
        cell.character = " ";
        cell.inverted  = true;
    }
}

bool DebugConsolePanel::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false;
        }
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            if (CloseButtonAt(mouse->at)) {
                if (onToggleRequest_) {
                    onToggleRequest_();
                }
                return true;
            }
            TakeFocus();
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (!chord) {
        return false;
    }
    if (chord->Special == editor::SpecialKey::Escape) {
        if (onToggleRequest_) {
            onToggleRequest_();
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Backspace) {
        prompt_.DeleteChar();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Enter) {
        const std::string expression = prompt_.Text();
        if (!expression.empty()) {
            prompt_.SetText("");
            if (!dapManager_) {
                history_.push_back({"No debugger available.", DisplayStyle::Error});
            }
            else {
                history_.push_back({"> " + expression, DisplayStyle::Plain});
                dapManager_->Evaluate(expression, [this, expression](bool success, std::string text) {
                    history_.push_back({success ? text : ("Error: " + text), success ? DisplayStyle::Plain : DisplayStyle::Error});
                    if (history_.size() > kMaxHistoryLines) {
                        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(history_.size() - kMaxHistoryLines));
                    }
                });
            }
        }
        return true;
    }
    if (IsPlainCharacter(*chord)) {
        prompt_.AppendChar(chord->Codepoint);
        return true;
    }
    return false;
}

} // namespace ned::ui
