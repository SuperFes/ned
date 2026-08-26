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

    // ACP round-1-live-validation follow-up: a bare line count, not a real
    // diff -- see FormatTranscript's Kind::ToolCall case for why a full
    // diff view (unified-diff-style +/- lines) is deliberately not attempted
    // here yet. Empty text counts as zero lines, not one.
    int CountLines(const std::string& text) {
        if (text.empty()) {
            return 0;
        }
        int count = 1;
        for (const char ch : text) {
            if (ch == '\n') {
                ++count;
            }
        }
        return count;
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
        case DisplayStyle::Accent:
            return Brush{.background = theme_.background, .foreground = theme_.borderAccent.foreground};
        case DisplayStyle::Hint:
            return Brush{.background = theme_.background, .foreground = theme_.diagnosticHint};
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
                    lines.push_back({line, DisplayStyle::Accent});
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
                // A real diff view (actual +/- lines) is deliberately not
                // attempted here -- this codebase has no reusable line-diff
                // utility yet (ThreeWayMerge.h's LCS diff is a private
                // implementation detail, not an exposed API), and building
                // one from scratch is a bigger, separate piece of work. A
                // line-count delta is still a concrete improvement over a
                // bare status word -- confirms *something* changed and
                // roughly how much, without a bare "(completed)".
                if (entry.diffOldText && entry.diffNewText) {
                    const int oldLines = CountLines(*entry.diffOldText);
                    const int newLines = CountLines(*entry.diffNewText);
                    lines.push_back({"  (" + std::to_string(oldLines) + " -> " + std::to_string(newLines) + " lines)", DisplayStyle::Dim});
                }
                break;
            }
            case Kind::Plan: {
                for (const std::string& step : entry.planSteps) {
                    lines.push_back({step, DisplayStyle::Hint});
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
            PaintUtf8Row(canvas, 0, row + 1, line.text, brush, width);
        }
    }

    // Input row. Painted with theme_.echoArea rather than plainBrush -- the
    // same "you're being prompted, type here" brush every other prompt in
    // the editor (find-file, M-x, goto-line, ...) already uses via EchoArea,
    // so the composer row reads as a distinct input field instead of
    // blending into the transcript above it (reported live as barely
    // visible/unreadable when painted the same as the rest of the panel --
    // see this panel's own ROADMAP.md entry; the underlying cursor-position
    // editing gap that entry also describes is unaffected by this, only the
    // row's legibility).
    const int         inputRow   = height - 1;
    const std::string text       = prompt_.StatusText();
    const Brush       inputBrush = theme_.echoArea;
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = inputRow}];
        cell.character = " ";
        inputBrush.ApplyTo(cell);
    }
    const int caretCol = PaintUtf8Row(canvas, 0, inputRow, text, inputBrush, width);
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

    // ACP round-1-live-validation follow-up: while a permission prompt is
    // pending, this panel resolves it directly rather than leaving
    // resolution to BufferView's separate echo-area InputMode::
    // AcpPermissionPrompt flow -- the "deliberate v1 cut" this class's own
    // header comment used to document. WindowManager's SetAcpPanelFocusChecker
    // wiring skips routing a new request to the focused pane's echo area
    // whenever this panel has focus, so this is the only place such a
    // keystroke lands in that case. Digit keys only, matching the numbered
    // list FormatTranscript already renders -- no moving selection cursor
    // the way BufferView's own flow has, so Enter is deliberately left alone
    // (no obvious default option to pick without one).
    if (acpManager_ && acpManager_->PendingPermissionPrompt()) {
        const editor::acp::AcpManager::PermissionPrompt& pending = *acpManager_->PendingPermissionPrompt();
        if (chord->Special == editor::SpecialKey::Escape) {
            acpManager_->CancelPermissionPrompt();
            return true;
        }
        if (IsPlainCharacter(*chord) && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
            const std::size_t index = static_cast<std::size_t>(chord->Codepoint - U'1');
            if (index < pending.options.size()) {
                acpManager_->ResolvePermissionPrompt(pending.options[index].optionId);
            }
            return true; // out-of-range digit: stay put, same as BufferView's own flow
        }
        // Anything else (typing into the composer, non-digit keys) falls
        // through unhandled by this block.
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
