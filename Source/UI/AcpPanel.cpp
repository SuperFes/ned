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
    constexpr int       kMaxInputRows          = 6;    // cap how far the composer grows before it starts scrolling internally

    // One physical row produced by wrapping a string to `width` columns.
    // startColumn/columnCount are codepoint offsets into the *original*
    // string (PaintUtf8Row's own one-column-per-codepoint convention) --
    // every codepoint lands in exactly one row and none are dropped, so a
    // flat cursor column can always be mapped back to (row, columnInRow) by
    // finding which row's [startColumn, startColumn+columnCount] it falls in.
    struct WrappedRow {
        std::string text;
        int         startColumn;
        int         columnCount;
    };

    // Greedy word-wrap: breaks before whichever codepoint would push a row
    // past `width` columns, preferring to break after the most recent space
    // in the current row, falling back to a hard mid-word break only when a
    // single word alone exceeds `width`. Always returns at least one row
    // (possibly empty), so an empty logical line still occupies one physical
    // row.
    std::vector<WrappedRow> WordWrap(std::string_view text, int width) {
        std::vector<WrappedRow> rows;
        if (width <= 0) {
            rows.push_back({std::string(text), 0, 0});
            return rows;
        }
        std::size_t rowStartByte  = 0;
        int         rowStartCol   = 0;
        int         col           = 0;
        std::size_t lastSpaceByte = std::string::npos; // byte just after the last space seen in this row
        int         lastSpaceCol  = 0;                 // col value at that point

        std::size_t pos = 0;
        while (pos < text.size()) {
            if (col >= width) {
                if (lastSpaceByte != std::string::npos && lastSpaceByte > rowStartByte) {
                    rows.push_back({std::string(text.substr(rowStartByte, lastSpaceByte - rowStartByte)), rowStartCol, lastSpaceCol});
                    rowStartCol += lastSpaceCol;
                    rowStartByte = lastSpaceByte;
                    col -= lastSpaceCol;
                }
                else {
                    rows.push_back({std::string(text.substr(rowStartByte, pos - rowStartByte)), rowStartCol, col});
                    rowStartCol += col;
                    rowStartByte = pos;
                    col = 0;
                }
                lastSpaceByte = std::string::npos;
            }
            const std::size_t next = text::NextCodepointBoundary(text, pos);
            if (text[pos] == ' ') { // safe at byte level: UTF-8 continuation/lead bytes are always >= 0x80
                lastSpaceByte = next;
                lastSpaceCol  = col + 1;
            }
            ++col;
            pos = next;
        }
        rows.push_back({std::string(text.substr(rowStartByte)), rowStartCol, col});
        return rows;
    }

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

    // The composer grows to however many wrapped rows its text needs (capped
    // at kMaxInputRows, beyond which it scrolls internally like the content
    // area does) -- smart-wrapping follow-up: previously a fixed single row,
    // so a prompt longer than the panel's width just ran off-screen with no
    // way to see or correct the hidden part.
    const std::string             statusText = prompt_.StatusText();
    const std::vector<WrappedRow> inputRows  = WordWrap(statusText, width);
    const int                     totalInputCols =
        inputRows.empty() ? 0 : inputRows.back().startColumn + inputRows.back().columnCount;
    const int caretFlat = std::min(prompt_.CursorDisplayColumn(), totalInputCols);
    int       caretRow = 0, caretColInRow = 0;
    for (std::size_t i = 0; i < inputRows.size(); ++i) {
        const WrappedRow& row = inputRows[i];
        if (caretFlat <= row.startColumn + row.columnCount) {
            caretRow      = static_cast<int>(i);
            caretColInRow = caretFlat - row.startColumn;
            break;
        }
    }

    const int allottedInputRows =
        std::max(1, std::min({static_cast<int>(inputRows.size()), kMaxInputRows, std::max(1, height - 1)}));
    int inputWindowStart = 0;
    if (static_cast<int>(inputRows.size()) > allottedInputRows) {
        // Scroll the visible window to always include the caret's row --
        // the only way the cursor can leave the allotted rows is Left/Home
        // inside a prompt long enough to be capped.
        inputWindowStart = std::clamp(caretRow - allottedInputRows + 1, 0, static_cast<int>(inputRows.size()) - allottedInputRows);
    }

    // Content rows: the tail of the formatted, word-wrapped transcript that
    // fits, top-aligned within the window (i.e. the window itself is
    // anchored to the most recent lines) -- no scrollback in v1, see header
    // comment.
    const int contentRows = std::max(0, height - 1 - allottedInputRows);
    if (contentRows > 0) {
        std::vector<DisplayLine> lines;
        for (const DisplayLine& logical : FormatTranscript()) {
            for (const WrappedRow& row : WordWrap(logical.text, width)) {
                lines.push_back({row.text, logical.style});
            }
        }
        const int start = std::max(0, static_cast<int>(lines.size()) - contentRows);
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

    // Input rows. Painted with theme_.echoArea rather than plainBrush -- the
    // same "you're being prompted, type here" brush every other prompt in
    // the editor (find-file, M-x, goto-line, ...) already uses via EchoArea,
    // so the composer reads as a distinct input field instead of blending
    // into the transcript above it (reported live as barely
    // visible/unreadable when painted the same as the rest of the panel --
    // see this panel's own ROADMAP.md entry; the underlying cursor-position
    // editing gap that entry also describes is unaffected by this, only the
    // row's legibility).
    const Brush inputBrush = theme_.echoArea;
    for (int j = 0; j < allottedInputRows; ++j) {
        const int screenRow = height - allottedInputRows + j;
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = screenRow}];
            cell.character = " ";
            inputBrush.ApplyTo(cell);
        }
        const std::size_t rowIndex = static_cast<std::size_t>(inputWindowStart + j);
        if (rowIndex < inputRows.size()) {
            PaintUtf8Row(canvas, 0, screenRow, inputRows[rowIndex].text, inputBrush, width);
        }
    }
    // minibuffer-composer-cursor-editing follow-up: the caret sits at the
    // prompt's real cursor position now, not always at the end of the typed
    // text -- CursorDisplayColumn() (one column per codepoint, matching
    // PaintUtf8Row's own convention) is what stays in sync with
    // InsertChar/DeleteBackward/DeleteForward/Move* below, mapped through
    // the wrap above into (caretRow, caretColInRow) so it still lands on the
    // right glyph once the composer spans multiple rows. A real solid block
    // cursor, not a video-invert: character is left untouched (so whatever's
    // already there -- a real typed character, or the row fill's blank when
    // the cursor sits past the end of the text -- stays visible through it)
    // and only recolored, explicitly, to a fixed high-contrast pair rather
    // than swapping whatever foreground happened to be underneath --
    // inverting inputBrush's own default-ish foreground left the character
    // under the caret nearly unreadable against the yellow block (reported
    // live after moving the cursor back over typed text).
    if (caretRow >= inputWindowStart && caretRow < inputWindowStart + allottedInputRows && caretColInRow < width) {
        const int screenRow = height - allottedInputRows + (caretRow - inputWindowStart);
        Cell&     cell      = canvas[{.x = caretColInRow, .y = screenRow}];
        Brush{.background = inputBrush.foreground, .foreground = Color::Black}.ApplyTo(cell);
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
        // chat-feel follow-up: interrupt a still-streaming reply first,
        // keeping whatever partial output already arrived (Claude Code's
        // own single-Esc-to-interrupt) -- a second Escape once nothing is
        // in flight falls through to the pre-existing close-panel behavior.
        if (acpManager_ && acpManager_->PromptInFlight()) {
            acpManager_->CancelPrompt();
            return true;
        }
        if (onToggleRequest_) {
            onToggleRequest_();
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Backspace) {
        prompt_.DeleteBackward();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Delete) {
        prompt_.DeleteForward();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Left) {
        prompt_.MoveCursorLeft();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right) {
        prompt_.MoveCursorRight();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Home) {
        prompt_.MoveCursorToStart();
        return true;
    }
    if (chord->Special == editor::SpecialKey::End) {
        prompt_.MoveCursorToEnd();
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
        prompt_.InsertChar(chord->Codepoint);
        return true;
    }
    return false;
}

} // namespace ned::ui
