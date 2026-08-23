#include "AcpPanel.h"

#include <algorithm>

#include "Border.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Mirrors BufferView.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, it's a one-line predicate private to each consumer there
    // too.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    constexpr int      kMinWidthForCloseButton = 8;
    constexpr int      kCloseOffset            = 4;    // column of '[' counted back from width, matches TerminalPanel's own offset
    constexpr char32_t kCloseIcon              = U'×'; // safe: one whole encoded glyph placed in exactly one Cell, not byte-indexed

    std::string StateLabel(editor::acp::AcpManager::SessionState state) {
        switch (state) {
            case editor::acp::AcpManager::SessionState::Starting:
                return "starting";
            case editor::acp::AcpManager::SessionState::Active:
                return "active";
            case editor::acp::AcpManager::SessionState::Inactive:
                return "inactive";
        }
        return "inactive";
    }

} // namespace

AcpPanel::AcpPanel(const Theme& theme) : theme_(theme), prompt_("Prompt: ") {
}

void AcpPanel::SetAcpManager(editor::acp::AcpManager* acpManager) {
    acpManager_ = acpManager;
}

void AcpPanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

Brush AcpPanel::BrushForStyle(DisplayStyle style) const {
    switch (style) {
        case DisplayStyle::Dim:
            return Brush{.background = theme_.background, .foreground = theme_.commentForeground};
        case DisplayStyle::Warning:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticWarning};
        case DisplayStyle::Plain:
            break;
    }
    return Brush{.background = theme_.background, .foreground = theme_.defaultForeground};
}

std::vector<AcpPanel::DisplayLine> AcpPanel::FormatTranscript() const {
    std::vector<DisplayLine> lines;
    if (!acpManager_) {
        return lines;
    }

    const auto& pending = acpManager_->PendingPermissionPrompt();

    for (const auto& entry : acpManager_->Transcript()) {
        using Kind = editor::acp::AcpManager::TranscriptEntry::Kind;
        switch (entry.kind) {
            case Kind::UserMessage: {
                lines.push_back({"> " + entry.text, DisplayStyle::Plain});
                break;
            }
            case Kind::AgentText: {
                // No word-wrap in v1 -- split only on literal newlines the
                // agent itself sent.
                std::size_t start = 0;
                while (start <= entry.text.size()) {
                    const std::size_t newlinePos = entry.text.find('\n', start);
                    const std::string line =
                        newlinePos == std::string::npos ? entry.text.substr(start) : entry.text.substr(start, newlinePos - start);
                    lines.push_back({line, DisplayStyle::Plain});
                    if (newlinePos == std::string::npos) {
                        break;
                    }
                    start = newlinePos + 1;
                }
                break;
            }
            case Kind::ToolCall: {
                // Plain ASCII marker, not a Unicode glyph -- the content-row
                // paint loop below places one *byte* per Cell (DrawBorderTitle's
                // own long-standing assumption), so a multi-byte glyph here
                // would corrupt column alignment for the rest of the line.
                std::string text = "* " + entry.text;
                if (!entry.status.empty()) {
                    text += " (" + entry.status + ")";
                }
                lines.push_back({text, DisplayStyle::Dim});
                break;
            }
            case Kind::Plan: {
                for (const std::string& step : entry.planSteps) {
                    lines.push_back({step, DisplayStyle::Plain});
                }
                break;
            }
            case Kind::Permission: {
                lines.push_back({"! " + entry.text, DisplayStyle::Warning});
                if (pending && pending->description == entry.text) {
                    std::string options;
                    for (std::size_t i = 0; i < pending->options.size(); ++i) {
                        if (i > 0) {
                            options += "  ";
                        }
                        options += "[" + std::to_string(i + 1) + "] " + pending->options[i].name;
                    }
                    if (!options.empty()) {
                        lines.push_back({"  " + options, DisplayStyle::Warning});
                    }
                }
                break;
            }
            case Kind::SessionEvent: {
                lines.push_back({"-- " + entry.text + " --", DisplayStyle::Dim});
                break;
            }
        }
    }
    return lines;
}

bool AcpPanel::CloseButtonAt(Point local) const {
    const int width = size().width;
    if (local.y != 0 || width < kMinWidthForCloseButton) {
        return false;
    }
    return local.x >= width - kCloseOffset && local.x <= width - kCloseOffset + 2;
}

void AcpPanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Opaque fill first -- this panel floats over BufferView the same way
    // TerminalPanel does, so every cell in its Box must be painted
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
    const std::string agentName = acpManager_ && !acpManager_->AgentName().empty() ? acpManager_->AgentName() : std::string("ACP agent");
    const std::string title     = agentName + " [" + StateLabel(acpManager_ ? acpManager_->State() : editor::acp::AcpManager::SessionState::Inactive) + "]";
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

    // Content rows: the tail of the formatted transcript that fits, top-
    // aligned within the window (i.e. the window itself is anchored to the
    // most recent lines) -- no scrollback in v1, see header comment.
    const int contentRows = height - 2;
    if (contentRows > 0) {
        const std::vector<DisplayLine> lines = FormatTranscript();
        const int                      start = std::max(0, static_cast<int>(lines.size()) - contentRows);
        for (int row = 0; row < contentRows; ++row) {
            const std::size_t lineIndex = static_cast<std::size_t>(start + row);
            if (lineIndex >= lines.size()) {
                continue;
            }
            const DisplayLine& line  = lines[lineIndex];
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

bool AcpPanel::OnEvent(const Event& event) {
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
        if (acpManager_ && !prompt_.Text().empty()) {
            acpManager_->SendPrompt(prompt_.Text());
            prompt_.SetText("");
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
