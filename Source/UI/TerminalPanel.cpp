#include "TerminalPanel.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "Border.h"
#include "Editor/Clipboard.h"
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
    // [/] at width-13, [▼] at width-10, [▲] at width-7, [×] at width-4.
    // '/' rather than a magnifying-glass glyph deliberately -- vi/less's
    // own "search" key, and guaranteed single-column (an emoji glyph would
    // misalign the fixed 3-column bracket layout every other button uses).
    constexpr char32_t kSearchIcon     = U'/';
    constexpr int      kSearchOffset   = 13;
    constexpr int      kMinimizeOffset = 10;
    constexpr int      kMaximizeOffset = 7;
    constexpr int      kCloseOffset    = 4;

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
    if (inButton(kSearchOffset)) {
        return TitleButton::Search;
    }
    return TitleButton::None;
}

std::vector<std::string> TerminalPanel::CellCharsForLine(int lineIndex) const {
    const int                scrollbackSize = emulator_.ScrollbackSize();
    const int                cols           = ContentCols();
    std::vector<std::string> cells;
    cells.reserve(static_cast<std::size_t>(std::max(0, cols)));
    for (int col = 0; col < cols; ++col) {
        const editor::terminal::Cell cell =
            lineIndex < scrollbackSize ? emulator_.ScrollbackCellAt(lineIndex, col) : emulator_.CellAt(lineIndex - scrollbackSize, col);
        cells.push_back(cell.character);
    }
    return cells;
}

std::string TerminalPanel::TextForLine(int lineIndex) const {
    std::string text;
    for (const std::string& ch : CellCharsForLine(lineIndex)) {
        text += ch;
    }
    // Trailing padding cells are literal ASCII spaces -- trimming them
    // byte-wise from the end is safe regardless of multibyte UTF-8 content
    // earlier in the line, and matches every real terminal's own
    // copy-to-clipboard convention (a screen row is padded to full width,
    // and that padding was never real content).
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string TerminalPanel::LineRangeText(int lineIndex, int fromCol, int toColInclusive) const {
    const std::vector<std::string> cells = CellCharsForLine(lineIndex);
    const int                      cols  = static_cast<int>(cells.size());
    const int                      from  = std::clamp(fromCol, 0, cols);
    const int                      to    = std::clamp(toColInclusive + 1, 0, cols);
    std::string                    text;
    for (int col = from; col < to; ++col) {
        text += cells[static_cast<std::size_t>(col)];
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

TerminalPanel::SelectionPoint TerminalPanel::PointForLocal(Point local) const {
    const int contentRow     = std::max(0, local.y - 1); // -1: past the title row, CursorPosition's own offset
    const int scrollbackSize = emulator_.ScrollbackSize();
    return SelectionPoint{
        .line = scrollbackSize - scrollbackOffset_ + contentRow,
        .col  = std::clamp(local.x, 0, std::max(0, ContentCols() - 1)),
    };
}

bool TerminalPanel::InSelection(int lineIndex, int col) const {
    if (!selectionAnchor_ || !selectionEnd_) {
        return false;
    }
    SelectionPoint start = *selectionAnchor_;
    SelectionPoint end   = *selectionEnd_;
    if (start.line > end.line || (start.line == end.line && start.col > end.col)) {
        std::swap(start, end);
    }
    if (lineIndex < start.line || lineIndex > end.line) {
        return false;
    }
    if (start.line == end.line) {
        return col >= start.col && col <= end.col;
    }
    if (lineIndex == start.line) {
        return col >= start.col;
    }
    if (lineIndex == end.line) {
        return col <= end.col;
    }
    return true; // a fully spanned middle line
}

std::string TerminalPanel::SelectedText() const {
    if (!selectionAnchor_ || !selectionEnd_) {
        return {};
    }
    SelectionPoint start = *selectionAnchor_;
    SelectionPoint end   = *selectionEnd_;
    if (start.line > end.line || (start.line == end.line && start.col > end.col)) {
        std::swap(start, end);
    }
    if (start.line == end.line) {
        return LineRangeText(start.line, start.col, end.col);
    }
    std::string result = LineRangeText(start.line, start.col, ContentCols() - 1);
    for (int line = start.line + 1; line < end.line; ++line) {
        result += "\n";
        result += TextForLine(line);
    }
    result += "\n";
    result += LineRangeText(end.line, 0, end.col);
    return result;
}

void TerminalPanel::ClearSelection() {
    selectionAnchor_.reset();
    selectionEnd_.reset();
}

void TerminalPanel::EnterSearch() {
    ClearSelection();
    searchOriginalScrollback_ = scrollbackOffset_;
    const int scrollbackSize  = emulator_.ScrollbackSize();
    const int combinedCount   = scrollbackSize + ContentRows();
    searchLines_.clear();
    searchLines_.reserve(static_cast<std::size_t>(std::max(0, combinedCount)));
    for (int i = 0; i < combinedCount; ++i) {
        searchLines_.push_back(TextForLine(i));
    }
    const std::size_t startIndex =
        combinedCount <= 0 ? 0 : static_cast<std::size_t>(std::clamp(combinedCount - 1 - scrollbackOffset_, 0, combinedCount - 1));
    // Backward: scrollback search's overwhelmingly common use is "find
    // something I saw earlier," i.e. searching up from here -- C-r inside
    // the session (see HandleSearchKey) still reverses to Forward on
    // demand, DebugConsolePanel's own convention.
    search_.emplace(searchLines_, editor::LineListSearch::Direction::Backward, startIndex);
}

void TerminalPanel::ScrollToShowLine(int lineIndex) {
    const int scrollbackSize = emulator_.ScrollbackSize();
    const int contentRows    = ContentRows();
    const int target         = scrollbackSize + contentRows - 1 - lineIndex;
    scrollbackOffset_        = std::clamp(target, 0, scrollbackSize);
}

bool TerminalPanel::HandleSearchKey(const editor::KeyChord& chord) {
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
    else if (!chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0) {
        search_->AppendChar(chord.Codepoint);
    }
    // Anything else (arrow keys, unrelated control combos) is ignored mid-search.

    if (const std::optional<std::size_t> index = search_->CurrentIndex()) {
        ScrollToShowLine(static_cast<int>(*index));
    }
    return true;
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
    else if (search_) {
        title += "  " + search_->StatusText();
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
        drawButton(kSearchOffset, kSearchIcon);
        drawButton(kMinimizeOffset, kMinimizeIcon);
        drawButton(kMaximizeOffset, kMaximizeIcon);
        drawButton(kCloseOffset, kCloseIcon);
    }

    // Content rows: a window over ring + live screen, offset lines up from
    // the bottom (offset 0 shows exactly the live screen).
    const int contentRows    = height - 1;
    const int scrollbackSize = emulator_.ScrollbackSize();
    const std::optional<std::size_t> searchMatchLine = search_ ? search_->CurrentIndex() : std::nullopt;
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
            // scrollback-search-and-selection follow-up: search match (a
            // whole line -- LineListSearch has no in-line match position,
            // DebugConsolePanel's own precedent) wins over a plain
            // selection when both apply.
            if (searchMatchLine && *searchMatchLine == static_cast<std::size_t>(lineIndex)) {
                cell.background_color = theme_.isearchMatchBackground;
            }
            else if (InSelection(lineIndex, col)) {
                cell.background_color = theme_.selectionBackground;
            }
        }
    }
}

bool TerminalPanel::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        if (const std::optional<MouseEvent> mouse = LocalMouseEvent(event)) {
            if (mouse->button == MouseEvent::Button::WheelUp) {
                ScrollBy(3);
                return true;
            }
            if (mouse->button == MouseEvent::Button::WheelDown) {
                ScrollBy(-3);
                return true;
            }
            if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
                // A fresh click always ends an active search session (its
                // own Escape-equivalent) before doing anything else --
                // BufferView's isearch-positional-click-exits precedent.
                if (search_) {
                    search_->Cancel();
                    scrollbackOffset_ = searchOriginalScrollback_;
                    search_.reset();
                }
                switch (TitleButtonAt(mouse->at)) {
                    case TitleButton::Search:
                        EnterSearch();
                        return true;
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
                // scrollback-search-and-selection follow-up: a press on a
                // content row starts a drag-select (real-terminal
                // click-and-drag-to-copy convention); a press on the title
                // row itself (no button hit) is a plain focus click,
                // unchanged.
                ClearSelection();
                if (mouse->at.y >= 1) {
                    selecting_       = true;
                    selectionAnchor_ = PointForLocal(mouse->at);
                    selectionEnd_    = selectionAnchor_;
                }
                return true;
            }
            if (mouse->motion == MouseEvent::Motion::Moved && selecting_) {
                selectionEnd_ = PointForLocal(mouse->at);
                return true;
            }
        }
        if (event.mouse().motion == MouseEvent::Motion::Released && selecting_) {
            selecting_          = false;
            const bool hasRange = selectionAnchor_ && selectionEnd_ &&
                                  (selectionAnchor_->line != selectionEnd_->line || selectionAnchor_->col != selectionEnd_->col);
            if (hasRange) {
                const std::string text = SelectedText();
                if (!text.empty()) {
                    editor::CopyToSystemClipboard(text);
                }
            }
            else {
                ClearSelection(); // a plain click with no drag: nothing to keep highlighted
            }
            return true;
        }
        return false;
    }

    const std::optional<editor::KeyChord> chord = TranslateKey(event);
    if (search_) {
        // Fully modal, same precedent as the exited_ branch below -- every
        // key is consumed here rather than forwarded, which is what makes
        // reusing Emacs isearch's own C-s/C-r/Enter/Escape convention safe
        // (see this file's header comment).
        if (chord.has_value()) {
            return HandleSearchKey(*chord);
        }
        return true;
    }
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
        ClearSelection();      // typing deselects, matching every real terminal
        ForwardPendingOutput();
        return true;
    }
    return false;
}

} // namespace ned::ui
