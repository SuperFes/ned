#include "DebugConsolePanel.h"

#include <algorithm>
#include <cctype>

#include "Border.h"
#include "KeyTranslation.h"

namespace ned::ui {

namespace {

    // Mirrors AcpPanel.cpp's own anonymous-namespace IsPlainCharacter --
    // not shared, it's a one-line predicate private to each consumer there
    // too.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    constexpr std::size_t kMaxHistoryLines = 500; // TerminalPanel's own scrollback-cap precedent, applied here too

    std::string StateLabel(editor::dap::Manager::SessionState state) {
        switch (state) {
            case editor::dap::Manager::SessionState::Starting:
                return "starting";
            case editor::dap::Manager::SessionState::Running:
                return "running";
            case editor::dap::Manager::SessionState::Stopped:
                return "stopped";
            case editor::dap::Manager::SessionState::Inactive:
                return "inactive";
        }
        return "inactive";
    }

} // namespace

DebugConsolePanel::DebugConsolePanel(const Theme& theme) : theme_(theme), prompt_("debug> ") {
}

void DebugConsolePanel::SetDapManager(editor::dap::Manager* dapManager) {
    dapManager_ = dapManager;
}

void DebugConsolePanel::SetPromptHistory(editor::PromptHistory* promptHistory) {
    promptHistory_ = promptHistory;
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

std::string DebugConsolePanel::TitleText() const {
    std::string title =
        "Debug console [" + StateLabel(dapManager_ ? dapManager_->State() : editor::dap::Manager::SessionState::Inactive) + "]";
    if (search_) {
        title += "  " + search_->StatusText();
    }
    else if (scrollbackOffset_ > 0) {
        title += " (scrollback)"; // TerminalPanel's own title-suffix convention
    }
    return title;
}

int DebugConsolePanel::ContentRows() const {
    return size().height - 1; // the input row is the only other row now -- PanelDock.h owns the title row
}

void DebugConsolePanel::ScrollBy(int deltaLines) {
    const int maxOffset = std::max(0, static_cast<int>(history_.size()) - std::max(0, ContentRows()));
    scrollbackOffset_   = std::clamp(scrollbackOffset_ + deltaLines, 0, maxOffset);
}

void DebugConsolePanel::ScrollToShowIndex(std::size_t index) {
    if (history_.empty()) {
        scrollbackOffset_ = 0;
        return;
    }
    const int maxOffset = std::max(0, static_cast<int>(history_.size()) - std::max(0, ContentRows()));
    const int target    = static_cast<int>(history_.size()) - 1 - static_cast<int>(index);
    scrollbackOffset_   = std::clamp(target, 0, maxOffset);
}

bool DebugConsolePanel::TryNavigateHistory(const editor::KeyChord& chord) {
    if (!chord.Meta || chord.Control) {
        return false;
    }
    if (chord.Codepoint != U'p' && chord.Codepoint != U'n') {
        return false;
    }
    if (!promptHistory_) {
        return true; // consumed -- just nothing to navigate
    }

    const std::vector<std::string>& entries = promptHistory_->Entries("debug-console");

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

bool DebugConsolePanel::HandleSearchKey(const editor::KeyChord& chord) {
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
        // Same Emacs isearch convention BufferView::HandleSearchKey uses:
        // C-s while already searching backward reverses instead of
        // repeating forward past a match the user hasn't seen yet.
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
    // Anything else (arrow keys, unrelated control combos) is ignored mid-search.

    if (const std::optional<std::size_t> index = search_->CurrentIndex()) {
        ScrollToShowIndex(*index);
    }
    return true;
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

    if (height < 1) {
        return;
    }

    // Content rows: a window over history_, offset lines up from the bottom
    // (scrollbackOffset_ 0 shows exactly the live tail) -- TerminalPanel's
    // own windowing shape. No title row of its own -- PanelDock.h's shared
    // tab strip owns that chrome now.
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

    // Input row.
    const int         inputRow = height - 1;
    const std::string text     = prompt_.StatusText();
    PaintUtf8Row(canvas, 0, inputRow, text, plainBrush, width);
    // minibuffer-composer-cursor-editing follow-up: the caret sits at the
    // prompt's real cursor position now, not always at the end of the typed
    // text -- see AcpPanel::Paint's own comment on CursorDisplayColumn() and
    // on why this is a real recolored block (character left alone, fixed
    // background/foreground pair) rather than a video-invert of whatever
    // was underneath. theme_.echoArea.foreground (not plainBrush's own,
    // near-black in LightTheme -- unreadable paired with black text) is the
    // one color in every bundled theme already curated to read well as a
    // distinct "you're being prompted" accent.
    const int caretCol = prompt_.CursorDisplayColumn();
    if (caretCol < width) {
        Cell& cell = canvas[{.x = caretCol, .y = inputRow}];
        Brush{.background = theme_.echoArea.foreground, .foreground = theme_.background}.ApplyTo(cell);
    }
}

bool DebugConsolePanel::OnEvent(const Event& event) {
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
            history_.empty() ? 0 : history_.size() - 1 - std::min<std::size_t>(static_cast<std::size_t>(scrollbackOffset_), history_.size() - 1);
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
        const std::string expression = prompt_.Text();
        if (!expression.empty()) {
            prompt_.SetText("");
            scrollbackOffset_ = 0; // submitting is this panel's "the user acted, snap to live"
            historyIndex_     = kNoHistoryIndex;
            historyStash_.clear();
            if (promptHistory_) {
                promptHistory_->Record("debug-console", expression);
            }
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
    if (TryComplete(*chord)) {
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

bool DebugConsolePanel::TryComplete(const editor::KeyChord& chord) {
    if (chord.Special != editor::SpecialKey::Tab) {
        return false;
    }
    if (dapManager_ == nullptr) {
        return true; // consumed either way -- a literal tab here would be noise
    }
    const std::string text = prompt_.Text();
    // DAP's column is 1-based and counts from the start of `text`; the
    // prompt's cursor is a byte offset, and the console's input is a single
    // line, so the two differ by exactly one.
    const int column = static_cast<int>(prompt_.CursorByteOffset()) + 1;
    dapManager_->RequestCompletions(text, column, [this, text](std::vector<editor::dap::Manager::Completion> completions) {
        if (completions.empty()) {
            return; // no opinion, or nothing matches -- say nothing rather than guess
        }
        if (completions.size() == 1) {
            ApplyCompletion(completions.front(), text);
            return;
        }
        // The common prefix first, so repeated Tab still makes progress,
        // then the list -- readline's own behaviour.
        std::string shared = completions.front().text;
        for (const editor::dap::Manager::Completion& completion : completions) {
            std::size_t common = 0;
            while (common < shared.size() && common < completion.text.size() && shared[common] == completion.text[common]) {
                ++common;
            }
            shared.resize(common);
        }
        if (!shared.empty()) {
            editor::dap::Manager::Completion prefix = completions.front();
            prefix.text                             = shared;
            ApplyCompletion(prefix, text);
        }
        std::string line;
        for (const editor::dap::Manager::Completion& completion : completions) {
            if (!line.empty()) {
                line += "  ";
            }
            line += completion.label;
        }
        history_.push_back({line, DisplayStyle::Dim});
    });
    return true;
}

void DebugConsolePanel::ApplyCompletion(const editor::dap::Manager::Completion& completion, const std::string& requestText) {
    // The adapter may name the exact span its suggestion replaces; when it
    // doesn't, replace the identifier-ish run ending at the cursor, which
    // is the only span a completion could sensibly mean.
    std::size_t start  = 0;
    std::size_t length = 0;
    if (completion.start >= 0 && completion.length >= 0 &&
        static_cast<std::size_t>(completion.start) + static_cast<std::size_t>(completion.length) <= requestText.size()) {
        start  = static_cast<std::size_t>(completion.start);
        length = static_cast<std::size_t>(completion.length);
    }
    else {
        std::size_t wordStart = requestText.size();
        while (wordStart > 0) {
            const unsigned char character = static_cast<unsigned char>(requestText[wordStart - 1]);
            if (std::isalnum(character) == 0 && character != '_' && character != '$' && (character & 0x80U) == 0) {
                break;
            }
            --wordStart;
        }
        start  = wordStart;
        length = requestText.size() - wordStart;
    }
    // The prompt may have moved on while the request was in flight; only
    // splice into text that still starts the way the request did.
    const std::string current = prompt_.Text();
    if (current.compare(0, start + length, requestText, 0, start + length) != 0) {
        return;
    }
    prompt_.SetText(current.substr(0, start) + completion.text + current.substr(start + length));
}

} // namespace ned::ui
