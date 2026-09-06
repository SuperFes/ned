#include "JanetReplPanel.h"

#include <janet.h>

#include <algorithm>
#include <sstream>

#include "Border.h"
#include "Janet/Environment.h"
#include "KeyTranslation.h"

namespace ned::ui {

namespace {

    // DebugConsolePanel.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, a one-line predicate private to each consumer.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    constexpr std::size_t kMaxHistoryLines = 500; // TerminalPanel/DebugConsolePanel's own scrollback-cap precedent

    // Janet's own printed representation of a successfully evaluated value --
    // what the janet CLI REPL itself prints, and Environment.cpp's own
    // DoStringCapturingStacktrace uses for the no-stderr-captured error
    // fallback (see that file for the exact same reinterpret_cast/
    // janet_string_length pairing). janet_to_string(nil) itself returns an
    // empty string rather than the text "nil" -- confirmed live: a void-
    // returning call (e.g. ned/set-repl-command, or the bare literal `nil`)
    // rendered as a blank transcript line, reading as "nothing happened"
    // when the call had actually succeeded. Special-cased here rather than
    // relying on the raw C API's own nil formatting.
    std::string DescribeJanetValue(Janet value) {
        if (janet_checktype(value, JANET_NIL)) {
            return "nil";
        }
        const JanetString description = janet_to_string(value);
        return std::string(reinterpret_cast<const char*>(description), static_cast<std::size_t>(janet_string_length(description)));
    }

} // namespace

JanetReplPanel::JanetReplPanel(const Theme& theme) : theme_(theme), prompt_("janet> ") {
}

void JanetReplPanel::SetEnv(JanetTable* env) {
    env_ = env;
}

void JanetReplPanel::SetPromptHistory(editor::PromptHistory* promptHistory) {
    promptHistory_ = promptHistory;
}

void JanetReplPanel::SetOnToggleRequest(std::function<void()> onToggle) {
    onToggleRequest_ = std::move(onToggle);
}

Brush JanetReplPanel::BrushForStyle(DisplayStyle style) const {
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

std::string JanetReplPanel::TitleText() const {
    std::string title = "Janet REPL";
    if (search_) {
        title += "  " + search_->StatusText();
    }
    else if (scrollbackOffset_ > 0) {
        title += " (scrollback)"; // TerminalPanel/DebugConsolePanel's own title-suffix convention
    }
    return title;
}

int JanetReplPanel::ContentRows() const {
    return size().height - 1; // the input row is the only other row -- PanelDock.h owns the title row
}

void JanetReplPanel::ScrollBy(int deltaLines) {
    const int maxOffset = std::max(0, static_cast<int>(history_.size()) - std::max(0, ContentRows()));
    scrollbackOffset_   = std::clamp(scrollbackOffset_ + deltaLines, 0, maxOffset);
}

void JanetReplPanel::ScrollToShowIndex(std::size_t index) {
    if (history_.empty()) {
        scrollbackOffset_ = 0;
        return;
    }
    const int maxOffset = std::max(0, static_cast<int>(history_.size()) - std::max(0, ContentRows()));
    const int target    = static_cast<int>(history_.size()) - 1 - static_cast<int>(index);
    scrollbackOffset_   = std::clamp(target, 0, maxOffset);
}

bool JanetReplPanel::TryNavigateHistory(const editor::KeyChord& chord) {
    if (!chord.Meta || chord.Control) {
        return false;
    }
    if (chord.Codepoint != U'p' && chord.Codepoint != U'n') {
        return false;
    }
    if (!promptHistory_) {
        return true; // consumed -- just nothing to navigate
    }

    const std::vector<std::string>& entries = promptHistory_->Entries("janet-repl");

    if (chord.Codepoint == U'p') { // older
        if (historyIndex_ == kNoHistoryIndex) {
            if (entries.empty()) {
                return true;
            }
            historyStash_ = prompt_.Text();
            historyIndex_ = 0;
        }
        else if (historyIndex_ + 1 < entries.size()) {
            ++historyIndex_;
        }
        else {
            return true; // already at the oldest entry
        }
        prompt_.SetText(entries[historyIndex_]);
        return true;
    }

    // 'n', newer
    if (historyIndex_ == kNoHistoryIndex) {
        return true; // already at the live edit -- nothing to do
    }
    if (historyIndex_ > 0) {
        --historyIndex_;
        prompt_.SetText(entries[historyIndex_]);
    }
    else {
        prompt_.SetText(historyStash_);
        historyIndex_ = kNoHistoryIndex;
    }
    return true;
}

bool JanetReplPanel::HandleSearchKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        search_->Accept();
        search_.reset();
        return true;
    }
    if (chord.Special == editor::SpecialKey::Escape) {
        search_->Cancel();
        scrollbackOffset_ = searchOriginalScrollback_;
        search_.reset();
        return true;
    }

    if (chord.Special == editor::SpecialKey::Backspace) {
        search_->DeleteChar();
    }
    else if (chord.Control && chord.Codepoint == U's') {
        if (search_->CurrentDirection() == editor::LineListSearch::Direction::Backward) {
            search_->ReverseDirection();
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (chord.Control && chord.Codepoint == U'r') {
        if (search_->CurrentDirection() == editor::LineListSearch::Direction::Forward) {
            search_->ReverseDirection();
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (IsPlainCharacter(chord)) {
        search_->AppendChar(chord.Codepoint);
    }

    if (const std::optional<std::size_t> index = search_->CurrentIndex()) {
        ScrollToShowIndex(*index);
    }
    return true;
}

void JanetReplPanel::Evaluate(const std::string& code) {
    history_.push_back({"> " + code, DisplayStyle::Plain});

    if (!env_) {
        history_.push_back({"No Janet environment available.", DisplayStyle::Error});
    }
    else {
        Janet       out;
        std::string capturedError;
        const int   signal = janet::DoStringCapturingStacktrace(env_, code, "ned-repl", &out, &capturedError);
        if (signal != 0) {
            // Error text can be multi-line (a real Janet stacktrace) --
            // split into one DisplayLine per line so scrollback/search see
            // real lines, not one giant embedded-newline row.
            std::istringstream stream(capturedError);
            std::string        line;
            while (std::getline(stream, line)) {
                history_.push_back({line, DisplayStyle::Error});
            }
        }
        else {
            history_.push_back({DescribeJanetValue(out), DisplayStyle::Dim});
        }
    }

    if (history_.size() > kMaxHistoryLines) {
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(history_.size() - kMaxHistoryLines));
    }
}

void JanetReplPanel::EvaluateForTesting(std::string_view code) {
    Evaluate(std::string(code));
}

void JanetReplPanel::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Opaque fill first -- this panel floats over BufferView the same way
    // TerminalPanel/AcpPanel/DebugConsolePanel do, so every cell in its Box
    // must be painted regardless of content.
    const Brush plainBrush = BrushForStyle(DisplayStyle::Plain);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell     = canvas[{.x = x, .y = y}];
            cell.character = " ";
            plainBrush.ApplyTo(cell);
        }
    }

    const int contentRows = height - 1;
    if (contentRows > 0) {
        const int start = std::max(0, static_cast<int>(history_.size()) - contentRows - scrollbackOffset_);
        for (int row = 0; row < contentRows; ++row) {
            const std::size_t lineIndex = static_cast<std::size_t>(start + row);
            if (lineIndex >= history_.size()) {
                continue;
            }
            const DisplayLine& line  = history_[lineIndex];
            Brush              brush = BrushForStyle(line.style);
            if (search_ && search_->CurrentIndex() == lineIndex) {
                brush.background = theme_.isearchMatchBackground;
            }
            PaintUtf8Row(canvas, 0, row, line.text, brush, width);
        }
    }

    const int         inputRow = height - 1;
    const std::string text     = prompt_.StatusText();
    PaintUtf8Row(canvas, 0, inputRow, text, plainBrush, width);
    const int caretCol = prompt_.CursorDisplayColumn();
    if (caretCol < width) {
        Cell& cell = canvas[{.x = caretCol, .y = inputRow}];
        Brush{.background = theme_.echoArea.foreground, .foreground = Color::Black}.ApplyTo(cell);
    }
}

bool JanetReplPanel::OnEvent(const Event& event) {
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
            TakeFocus();
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (!chord) {
        return false;
    }

    if (search_) {
        return HandleSearchKey(*chord);
    }
    if (chord->Control && !chord->Meta && (chord->Codepoint == U's' || chord->Codepoint == U'r')) {
        searchOriginalScrollback_ = scrollbackOffset_;
        searchLines_.clear();
        searchLines_.reserve(history_.size());
        for (const DisplayLine& line : history_) {
            searchLines_.push_back(line.text);
        }
        const std::size_t startIndex =
            history_.empty() ? 0
                             : history_.size() - 1 - std::min<std::size_t>(static_cast<std::size_t>(scrollbackOffset_), history_.size() - 1);
        search_.emplace(searchLines_,
                        chord->Codepoint == U's' ? editor::LineListSearch::Direction::Forward : editor::LineListSearch::Direction::Backward,
                        startIndex);
        return true;
    }

    if (chord->Special == editor::SpecialKey::Escape) {
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
    if (chord->Special == editor::SpecialKey::PageUp && chord->Shift) {
        ScrollBy(std::max(1, ContentRows() - 1));
        return true;
    }
    if (chord->Special == editor::SpecialKey::PageDown && chord->Shift) {
        ScrollBy(-std::max(1, ContentRows() - 1));
        return true;
    }
    if (chord->Special == editor::SpecialKey::Enter) {
        const std::string code = prompt_.Text();
        if (!code.empty()) {
            prompt_.SetText("");
            scrollbackOffset_ = 0; // submitting is this panel's "the user acted, snap to live"
            historyIndex_     = kNoHistoryIndex;
            historyStash_.clear();
            if (promptHistory_) {
                promptHistory_->Record("janet-repl", code);
            }
            Evaluate(code);
        }
        return true;
    }
    if (TryNavigateHistory(*chord)) {
        return true;
    }
    if (IsPlainCharacter(*chord)) {
        prompt_.InsertChar(chord->Codepoint);
        return true;
    }
    return false;
}

} // namespace ned::ui
