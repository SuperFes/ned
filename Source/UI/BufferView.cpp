#include "BufferView.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <utility>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "Editor/FuzzyMatch.h"
#include "Editor/ProjectFileOps.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/Rectangle.h"
#include "Editor/ScratchPad.h"
#include "Editor/TabWidth.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Plain, non-modifier printable input: the only kind of chord that should
    // feed into a query string during isearch/query-replace/prompt text entry.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // Window-splitting requests forward to WindowManager (see
    // BufferView::StartInteractiveSession's own switch) and can
    // synchronously destroy the BufferView that's currently handling the
    // very keypress that triggered them -- delete-window/delete-other-windows
    // on the pane running this call, or a split/other-window reshaping the
    // tree out from under it. Confirmed by a real SIGSEGV in
    // WindowManagerTest.cpp, not assumed. Callers use this to decide
    // whether it's safe to touch `this` again after dispatching a command
    // that might have set one of these -- see RunCommandAndHandleOutcome's
    // and ReplayMacro's own doc comments.
    bool IsWindowManagementRequest(editor::InteractiveRequest request) {
        using editor::InteractiveRequest;
        return request == InteractiveRequest::SplitBelow || request == InteractiveRequest::SplitRight ||
               request == InteractiveRequest::DeleteWindow || request == InteractiveRequest::DeleteOtherWindows ||
               request == InteractiveRequest::OtherWindow;
    }

    // Gutter selection highlighting (gutter-highlight follow-up): whether a
    // line is untouched, partially, or fully covered by the current region.
    // lineEndExclusive is the offset just past the line's own newline (or
    // ByteLength() for the last line) -- deliberately *including* the
    // newline, unlike Paint()'s content-rendering lineEnd, so that selecting
    // through to the start of the next line still counts this one as fully
    // selected, matching how selecting a whole line normally feels.
    enum class GutterSelection { None,
                                 Partial,
                                 Full };

    GutterSelection ClassifyGutterSelection(const text::Buffer& buffer, std::size_t lineStart,
                                            std::size_t lineEndExclusive) {
        if (!buffer.HasMark()) {
            return GutterSelection::None;
        }
        const auto [start, end] = buffer.Region();
        if (start >= end || !(start < lineEndExclusive && end > lineStart)) {
            return GutterSelection::None;
        }
        return (start <= lineStart && lineEndExclusive <= end) ? GutterSelection::Full : GutterSelection::Partial;
    }

    // Byte-wise longest common prefix -- fine for paths/buffer names, same
    // "ASCII-ish" simplification ModeLine's own name rendering already makes.
    std::string LongestCommonPrefix(const std::vector<std::string>& strings) {
        if (strings.empty()) {
            return {};
        }

        std::string prefix = strings.front();
        for (const std::string& s : strings) {
            std::size_t i = 0;
            while (i < prefix.size() && i < s.size() && prefix[i] == s[i]) {
                ++i;
            }
            prefix.resize(i);
        }
        return prefix;
    }

    std::string JoinCandidates(const std::vector<std::string>& candidates) {
        std::string joined;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (i > 0) {
                joined += ' ';
            }
            joined += candidates[i];
        }
        return joined;
    }

    // execute-extended-command follow-up: M-x's own version of
    // JoinCandidates -- marks the currently-selected entry with a leading
    // '*' (there's no floating widget to highlight a row in, so the marker
    // has to live inside the same one echo-area line everything else here
    // already renders through), and caps how many of the ranked list are
    // actually shown: dumping dozens of command names into one
    // terminal-width line is unreadable. Only a window of up to
    // kMaxVisibleCandidates is shown, scrolled to keep the selected entry
    // visible, with a "+K more" suffix for whatever's left out -- arrow keys
    // still reach every ranked candidate regardless of what's currently
    // visible. kMaxVisibleCandidates=6 is a judgment call: this codebase's
    // command names run roughly 10-25 characters, and 6 of them plus
    // markers/spaces/braces comfortably fits an 80-column terminal alongside
    // "M-x " and the typed query.
    constexpr std::size_t kMaxVisibleCandidates = 6;

    std::string FormatExecuteCommandCandidates(const std::vector<std::string>& ranked, std::size_t selected) {
        if (ranked.empty()) {
            return {};
        }

        const std::size_t windowStart =
            (selected < kMaxVisibleCandidates) ? 0 : selected - kMaxVisibleCandidates + 1;
        const std::size_t windowEnd = std::min(ranked.size(), windowStart + kMaxVisibleCandidates);

        std::string joined;
        for (std::size_t i = windowStart; i < windowEnd; ++i) {
            if (i > windowStart) {
                joined += ' ';
            }
            if (i == selected) {
                joined += '*';
            }
            joined += ranked[i];
        }

        const std::size_t hidden = ranked.size() - (windowEnd - windowStart);
        if (hidden > 0) {
            joined += " +" + std::to_string(hidden) + " more";
        }
        return joined;
    }

    // Binary-rendering follow-up: a raw control byte (C0 control range, plus
    // DEL) sent straight to a real terminal isn't "print one glyph and
    // advance" -- some of them are actual terminal control codes (cursor
    // moves, and a raw ESC byte can be misread as the start of a whole new
    // escape sequence), which is the exact same class of terminal-diff
    // corruption tab bytes used to cause (see editor::TabWidth's own header
    // comment) before being expanded to literal spaces instead of sent raw.
    // Tab (handled separately, below) and newline (never appears mid-line --
    // lines are split on it by LineToByteOffset) are excluded; everything
    // else in this range renders as a 4-column hex placeholder instead of
    // ever reaching the terminal as its own raw byte.
    bool IsUnprintableControl(char32_t cp) {
        return (cp <= 0x1F && cp != U'\t') || cp == 0x7F;
    }

    // U+25C1/U+25B7 WHITE LEFT/RIGHT-POINTING TRIANGLE -- same proven-safe
    // BMP "Geometric Shapes" family as every other chrome glyph in this
    // codebase (ScrollArrowButton's ▲▼, ProjectSidebar's ▸▾), chosen
    // specifically distinct from SidebarToggle's «»/TabBar's ‹› so a binary
    // placeholder never reads as one of those instead.
    constexpr char32_t kBinaryOpen  = U'◁';
    constexpr char32_t kBinaryClose = U'▷';

    char32_t HexDigit(char32_t nibble) {
        return (nibble < 10) ? (U'0' + nibble) : (U'A' + (nibble - 10));
    }

    // Columns a single codepoint occupies when rendered: editor::TabWidth()
    // for a tab, 4 (open bracket + 2 hex digits + close bracket) for a
    // binary placeholder, 1 for every ordinary glyph. Shared by Paint()'s
    // render loop and VisualColumn below so the two can never disagree
    // about column math.
    int CodepointColumns(char32_t cp) {
        if (cp == U'\t') {
            return editor::TabWidth();
        }
        if (IsUnprintableControl(cp)) {
            return 4;
        }
        return 1;
    }

    // Visual column (0-indexed, not counting the gutter) that byteOffset
    // renders at within the line starting at lineStart -- see
    // CodepointColumns for why this can't be a plain codepoint count.
    // Returns nullopt once the column would reach maxColumns before
    // byteOffset does, matching the horizontal-scroll cutoff Paint()
    // already applies to the cursor (there's no point computing an exact
    // value for a column that won't be shown anyway) -- critically, this
    // bound is also what keeps the scan O(maxColumns) instead of
    // O(byteOffset - lineStart): point can be millions of bytes into a
    // single pathologically long line while still being nowhere near the
    // visible viewport width, and this must not re-scan that whole distance
    // on every Paint() call to find out.
    std::optional<int> VisualColumn(const text::Rope& content, std::size_t lineStart, std::size_t byteOffset,
                                    int maxColumns) {
        int         col    = 0;
        std::size_t offset = lineStart;
        while (offset < byteOffset) {
            if (col >= maxColumns) {
                return std::nullopt;
            }
            const auto decoded = content.CodepointAt(offset);
            col += CodepointColumns(decoded.codepoint);
            offset += decoded.byteLength;
        }
        return col;
    }

    // Filters mode_.highlight's whole-buffer HighlightSpan list down to just
    // the spans overlapping [lineStart, lineEnd) -- called once per visible
    // row from Paint(), *not* once per rendered codepoint, so ClassAtOffset
    // below only ever scans a small, per-line list rather than the whole
    // file's spans on every single codepoint.
    std::vector<editor::HighlightSpan> SpansForLine(const std::vector<editor::HighlightSpan>& spans,
                                                    std::size_t lineStart, std::size_t lineEnd) {
        std::vector<editor::HighlightSpan> lineSpans;
        for (const editor::HighlightSpan& span : spans) {
            if (span.endByte > lineStart && span.startByte < lineEnd) {
                lineSpans.push_back(span);
            }
        }
        return lineSpans;
    }

    // Finds the SyntaxClass applying at byteOffset from a (typically small,
    // already line-filtered -- see SpansForLine) HighlightSpan list, Default
    // if none covers it. Spans overlapping the same byte resolve in `spans`'
    // own order, later wins -- see HighlightSpan's own doc comment in Mode.h
    // for why.
    editor::SyntaxClass ClassAtOffset(const std::vector<editor::HighlightSpan>& spans, std::size_t byteOffset) {
        editor::SyntaxClass cls = editor::SyntaxClass::Default;
        for (const editor::HighlightSpan& span : spans) {
            if (span.startByte <= byteOffset && byteOffset < span.endByte) {
                cls = span.syntaxClass;
            }
        }
        return cls;
    }

    // Tag string for LogMouseEvent, derived from the raw event rather than
    // passed in separately at each call site (was four distinct
    // mouse_press/mouse_move/mouse_release/mouse_wheel overrides, now one
    // unified OnMouseEvent).
    std::string_view MouseEventTag(const ftxui::Mouse& mouse) {
        if (mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) {
            return "wheel";
        }
        switch (mouse.motion) {
            case ftxui::Mouse::Pressed:
                return "press";
            case ftxui::Mouse::Released:
                return "release";
            case ftxui::Mouse::Moved:
            default:
                return "move";
        }
    }

} // namespace

BufferView::BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
                       text::BufferList& bufferList, editor::Dispatcher& dispatcher, std::string& statusMessage,
                       const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), killRing_(killRing), registers_(registers), bufferList_(bufferList),
                                                                       dispatcher_(dispatcher), statusMessage_(statusMessage), mode_(mode), theme_(theme) {
    if (const char* path = std::getenv("NED_DEBUG_MOUSE"); path && *path) {
        debugMouseLogPath_ = path;
    }
}

editor::CommandContext BufferView::MakeContext() {
    return editor::CommandContext{activeBuffer_.Get(), killRing_, bufferList_, editor::KeyChord{}, &statusMessage_};
}

void BufferView::SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler) {
    onWindowRequest_ = std::move(handler);
}

void BufferView::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void BufferView::Paint(Canvas c) {
    text::Buffer&     buffer     = activeBuffer_.Get();
    const text::Rope& content    = buffer.Content();
    const std::size_t totalLines = content.LineCount();
    const Brush       emptyBrush = theme_.BrushFor(editor::SyntaxClass::Default);
    const std::size_t point      = buffer.Point();
    const std::size_t pointLine  = content.ByteOffsetToLine(point);

    // narrow-to-region/widen follow-up: caps which rows actually get
    // painted -- deliberately a *separate* value from totalLines above,
    // which stays the real, whole-buffer line count. lineEnd/
    // lineEndWithNewline below still need to know whether `line` is the
    // buffer's own real last line (to fall back to content.ByteLength())
    // regardless of narrowing -- the narrowed range's own last line is
    // usually not the buffer's real last line at all, just the last one
    // currently visible, and still needs its real next-line boundary looked
    // up correctly.
    const std::size_t renderEndLine = NarrowedLineRange().second;

    if (scrollBar_ != nullptr) {
        // scrollable_length is fed as MaxTopLine() + 1, not totalLines: the
        // scroll bar internally clamps a user-driven drag/click's target
        // position to [0, scrollable_length - 1], so this is what makes its
        // own built-in range match ours exactly -- dragging all the way
        // down actually reaches true end-of-file, not one line short of it.
        scrollBar_->scrollable_length  = static_cast<int>(MaxTopLine()) + 1;
        scrollBar_->position           = static_cast<int>(topLine_);
        scrollBar_->item_visual_length = 1; // one buffer line per canvas row
    }
    if (scrollUpArrow_ != nullptr) {
        scrollUpArrow_->SetEnabled(topLine_ > 0);
    }
    if (scrollDownArrow_ != nullptr) {
        scrollDownArrow_->SetEnabled(topLine_ < MaxTopLine());
    }

    const std::size_t gutterWidth  = GutterWidth();
    const std::size_t gutterDigits = gutterWidth - 1; // trailing column is a separating space

    // Recomputed only when the active buffer or its content has actually
    // changed since the last Paint() call -- see highlightCacheBuffer_'s own
    // doc comment in BufferView.h for why this caching exists at all (a real,
    // measured perf fix, not a preemptive one).
    if (!mode_.highlight) {
        highlightCacheBuffer_ = nullptr;
        highlightCacheSpans_.clear();
    }
    else if (highlightCacheBuffer_ != &buffer || highlightCacheGeneration_ != buffer.ContentGeneration()) {
        highlightCacheSpans_      = mode_.highlight(buffer.Text());
        highlightCacheBuffer_     = &buffer;
        highlightCacheGeneration_ = buffer.ContentGeneration();
    }
    const std::vector<editor::HighlightSpan>& highlightSpans = highlightCacheSpans_;

    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            ftxui::Cell& cell = c[{.x = col, .y = row}];
            cell.character    = " ";
            emptyBrush.ApplyTo(cell);
        }

        const std::size_t line = topLine_ + static_cast<std::size_t>(row);
        if (line >= renderEndLine) {
            continue;
        }

        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        // Includes the line's own newline (unlike lineEnd above), so a
        // region selected through to the start of the next line still
        // counts this one as fully selected -- see ClassifyGutterSelection.
        const std::size_t     lineEndWithNewline = (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) : content.ByteLength();
        const GutterSelection gutterSelection    = ClassifyGutterSelection(buffer, lineStart, lineEndWithNewline);

        const Color gutterForeground =
            (line == pointLine) ? theme_.currentLineNumberForeground : theme_.lineNumberForeground;
        // Digits+padding get the full selection background only when the
        // whole line is covered; the one-column gap after them gets it for
        // Partial too, so a partially-selected line still shows a thin
        // highlighted edge instead of no indication at all.
        const Brush gutterBrush{
            .background = (gutterSelection == GutterSelection::Full) ? theme_.selectionBackground : theme_.background,
            .foreground = gutterForeground,
        };
        const Brush gutterGapBrush{
            .background = (gutterSelection != GutterSelection::None) ? theme_.selectionBackground : theme_.background,
            .foreground = gutterForeground,
        };
        const std::string number  = std::to_string(line + 1); // 1-indexed, matches ModeLine's L/C convention
        const std::size_t padding = gutterDigits > number.size() ? gutterDigits - number.size() : 0;
        for (std::size_t i = 0; i < padding && static_cast<int>(i) < c.size().width; ++i) {
            ftxui::Cell& cell = c[{.x = static_cast<int>(i), .y = row}];
            cell.character    = " ";
            gutterBrush.ApplyTo(cell);
        }
        for (std::size_t i = 0; i < number.size() && static_cast<int>(padding + i) < c.size().width; ++i) {
            ftxui::Cell& cell = c[{.x = static_cast<int>(padding + i), .y = row}];
            cell.character    = std::string(1, number[i]);
            gutterBrush.ApplyTo(cell);
        }
        if (static_cast<int>(gutterDigits) < c.size().width) {
            ftxui::Cell& cell = c[{.x = static_cast<int>(gutterDigits), .y = row}];
            cell.character    = " ";
            gutterGapBrush.ApplyTo(cell);
        }

        const std::vector<editor::HighlightSpan> lineSpans = SpansForLine(highlightSpans, lineStart, lineEnd);

        std::size_t offset = lineStart;
        int         col    = static_cast<int>(gutterWidth);
        while (offset < lineEnd && col < c.size().width) {
            const auto decoded = content.CodepointAt(offset);

            const editor::SyntaxClass cls   = ClassAtOffset(lineSpans, offset);
            Brush                     brush = theme_.BrushFor(cls);
            if (InIsearchMatch(offset)) {
                brush.background = theme_.isearchMatchBackground;
            }
            else if (InSelection(offset)) {
                brush.background = theme_.selectionBackground;
            }

            if (decoded.codepoint == U'\t') {
                // A real terminal treats a raw tab byte as "jump to the next
                // tab stop" (consuming several columns), not "print one
                // glyph and advance by one" -- sending it through unexpanded
                // desyncs the terminal's actual cursor position from what
                // the terminal library's own per-cell diff bookkeeping
                // believes was written, which then corrupts unrelated cells
                // on later frames. Expanding to literal space glyphs keeps
                // this widget's one-codepoint-per-column model -- and the
                // real terminal's actual column count -- in agreement.
                // editor::TabWidth() is a *display* setting only; the
                // buffer's real tab byte is untouched.
                const int tabWidth = editor::TabWidth();
                for (int i = 0; i < tabWidth && col < c.size().width; ++i) {
                    ftxui::Cell& cell = c[{.x = col, .y = row}];
                    cell.character    = " ";
                    brush.ApplyTo(cell);
                    ++col;
                }
            }
            else if (IsUnprintableControl(decoded.codepoint)) {
                // Same reasoning as the tab case above: a raw control byte
                // (some of them genuine terminal control codes -- a bare ESC
                // is the sharpest example) must never reach the terminal as
                // itself. Rendered as a 4-column "◁XX▷" hex placeholder
                // instead -- entirely safe, printable characters -- with a
                // dedicated foreground so it reads as "this is escaped data",
                // not literal text; whatever background isearch/selection
                // already chose above is kept so an active highlight still
                // shows through it.
                Brush binaryBrush        = brush;
                binaryBrush.foreground   = theme_.binaryForeground;
                const char32_t glyphs[4] = {kBinaryOpen, HexDigit((decoded.codepoint >> 4) & 0xF),
                                            HexDigit(decoded.codepoint & 0xF), kBinaryClose};
                for (const char32_t glyph : glyphs) {
                    if (col >= c.size().width) {
                        break;
                    }
                    ftxui::Cell& cell = c[{.x = col, .y = row}];
                    cell.character    = text::EncodeCodepointUtf8(glyph);
                    binaryBrush.ApplyTo(cell);
                    ++col;
                }
            }
            else {
                ftxui::Cell& cell = c[{.x = col, .y = row}];
                cell.character    = text::EncodeCodepointUtf8(decoded.codepoint);
                brush.ApplyTo(cell);
                ++col;
            }

            offset += decoded.byteLength;
        }
    }
}

std::optional<Point> BufferView::CursorPosition() const {
    // A pure, independently-callable query, deliberately NOT a value cached
    // as a Paint() side effect (a real, reported bug fixed here: FTXUI's
    // Node lifecycle always calls ComputeRequirement/SetBox -- which is what
    // reads this -- *before* Render (which calls Paint()) on every single
    // frame, so a Paint()-time cache would always be exactly one frame
    // stale, showing where point was during the *previous* frame's Paint()
    // call rather than where it is now; felt live as "press Right and
    // nothing happens, press it again and the cursor jumps to where the
    // first press should have gone"). Cheap enough to recompute on every
    // call -- buffer/content access, one GutterWidth() call, one
    // ByteOffsetToLine/LineToByteOffset pair, one bounded VisualColumn scan
    // -- nowhere near Paint()'s own per-visible-row cost.
    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::Rope&   content     = buffer.Content();
    const std::size_t   point       = buffer.Point();
    const std::size_t   pointLine   = content.ByteOffsetToLine(point);
    const std::size_t   gutterWidth = GutterWidth();

    if (pointLine < topLine_) {
        return std::nullopt;
    }

    // size() -- inherited from Widget, persisted on this long-lived object
    // rather than the transient per-frame PaintNode -- is still its
    // default-constructed {0,0} the very first time this is ever called:
    // FTXUI always calls ComputeRequirement (which is what reads
    // CursorPosition) before SetBox has run even once, on every frame, and
    // for every frame after the first, size() happens to already hold the
    // *previous* frame's real value (close enough in practice -- viewport
    // dimensions essentially never change mid-session outside a terminal
    // resize). Frame one has no previous frame to fall back on, so treating
    // an unknown ({0,0}) size as "don't bound at all" rather than "assume
    // everything is off-screen" is what makes the cursor visible starting
    // on the very first rendered frame, not only once some later event
    // triggers a second Draw() call -- a real, reported bug (no terminal
    // ever sent a "show cursor" escape sequence in its very first frame,
    // confirmed via a raw pty capture, not assumed) that otherwise made the
    // cursor (and, per Focused()'s own aggregation depending on this same
    // enabled flag, arguably the sense that the editor was "ready" at all)
    // appear to not exist until the user did something.
    const Size sizeNow     = size();
    const bool sizeIsKnown = sizeNow.height > 0 && sizeNow.width > 0;
    if (sizeIsKnown && pointLine - topLine_ >= static_cast<std::size_t>(sizeNow.height)) {
        return std::nullopt;
    }

    const std::size_t lineStart = content.LineToByteOffset(pointLine);
    const int         maxColumns =
        sizeIsKnown ? sizeNow.width - static_cast<int>(gutterWidth) : std::numeric_limits<int>::max();
    const std::optional<int> visualCol = VisualColumn(content, lineStart, point, maxColumns);
    if (!visualCol) {
        return std::nullopt;
    }

    const std::size_t col = gutterWidth + static_cast<std::size_t>(*visualCol);
    if (sizeIsKnown && col >= static_cast<std::size_t>(sizeNow.width)) {
        return std::nullopt; // scrolled off horizontally; no horizontal scroll in v1
    }
    return Point{.x = static_cast<int>(col), .y = static_cast<int>(pointLine - topLine_)};
}

bool BufferView::Focusable() const {
    return true; // was FocusPolicy::Strong
}

bool BufferView::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        return OnMouseEvent(event);
    }
    return OnKeyEvent(event);
}

bool BufferView::OnKeyEvent(ftxui::Event event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return false;
    }

    if (inputMode_ == InputMode::IsearchForward || inputMode_ == InputMode::IsearchBackward) {
        HandleSearchKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::QueryReplace) {
        HandleQueryReplaceKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ProjectReplace) {
        HandleProjectReplaceKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ConfirmQuit) {
        HandleConfirmQuitKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ConfirmCloseBuffer) {
        HandleConfirmCloseBufferKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::SwitchToBuffer ||
        inputMode_ == InputMode::ProjectSearch || inputMode_ == InputMode::CreateDirectory ||
        inputMode_ == InputMode::FindScratch || inputMode_ == InputMode::StringRectangle) {
        HandlePromptKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::DeleteFile) {
        HandleDeleteFileKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::RenameFile) {
        HandleRenameFileKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ExecuteCommand) {
        // No ClampPointToNarrowing() here: HandleExecuteCommandKey's own
        // Enter branch already routes through RunCommandAndHandleOutcome
        // internally (M-x invoking a command by name), which handles the
        // clamp itself -- see that method's own doc comment for why it has
        // to be the one doing it, not a caller after the fact.
        HandleExecuteCommandKey(*chord);
        return true;
    }
    if (inputMode_ == InputMode::PointToRegister || inputMode_ == InputMode::JumpToRegister ||
        inputMode_ == InputMode::CopyToRegister || inputMode_ == InputMode::InsertRegister) {
        HandleRegisterKey(*chord);
        ClampPointToNarrowing();
        return true;
    }

    // No ClampPointToNarrowing() here either: RunCommandAndHandleOutcome
    // handles it internally now (see its own doc comment) -- required,
    // not just tidier, since a dispatched command can be split-window/
    // delete-window/other-window, which synchronously destroys *this*
    // BufferView via StartInteractiveSession's own window-forwarding path;
    // a clamp call made from out here, after the dispatch returns, would be
    // touching an already-destroyed object in exactly that case (a real,
    // SIGSEGV-confirmed bug caught by this session's own WindowManagerTest.cpp
    // suite, not a hypothetical one).
    editor::CommandContext context = MakeContext();
    context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    RunCommandAndHandleOutcome(context, [&] { dispatcher_.Feed(*chord, context); });
    return true;
}

void BufferView::RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<void()>& invoke) {
    try {
        invoke();
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }

    if (context.quit) {
        // Active() is nullptr outside a live ScreenInteractive::Loop() --
        // every unit test, and any other headless use of BufferView. It is
        // never null during real, running-editor usage (main.cpp's own
        // screen.Loop(head) is what drives every key_press that could ever
        // set context.quit in the first place), but skipping the null check
        // entirely crashed the whole process the instant a test exercised
        // `quit` -- confirmed via a real SIGSEGV while porting this file's
        // own test suite off TermOx, not assumed.
        if (ftxui::ScreenInteractive* screen = ftxui::ScreenInteractive::Active()) {
            screen->Exit();
        }
        return;
    }

    if (context.interactiveRequest != editor::InteractiveRequest::None) {
        // context is a reference to the *caller's* own local CommandContext
        // (never a member of this), so reading context.interactiveRequest
        // is always safe regardless of what StartInteractiveSession just
        // did -- but calling ClampPointToNarrowing() (which touches
        // activeBuffer_, a real member) afterward is not, for exactly the
        // IsWindowManagementRequest cases (see that function's own doc
        // comment). Skip it there; every other request is a normal,
        // still-alive-*this* interactive session (isearch, a prompt, ...).
        const bool destroysThisPane = IsWindowManagementRequest(context.interactiveRequest);
        StartInteractiveSession(context.interactiveRequest);
        if (!destroysThisPane) {
            ClampPointToNarrowing();
        }
        return;
    }

    ClampPointToNarrowing();
    ScrollToShowPoint();
}

void BufferView::ReplayMacro() {
    if (replayingMacro_) {
        return;
    }

    const std::vector<editor::KeyChord> macro = dispatcher_.LastMacro();
    if (macro.empty()) {
        statusMessage_ = "No keyboard macro has been recorded yet.";
        return;
    }

    replayingMacro_ = true;
    for (const editor::KeyChord& chord : macro) {
        editor::CommandContext context = MakeContext();
        context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
        RunCommandAndHandleOutcome(context, [&] { dispatcher_.Feed(chord, context); });

        // context.quit/interactiveRequest are the caller's own local copies
        // (see RunCommandAndHandleOutcome's own doc comment), always safe to
        // read even if *this* was just destroyed -- checked first and
        // exclusively in that case. A recorded macro can perfectly well
        // contain a delete-window/split/other-window step, and touching
        // inputMode_ below afterward would otherwise be a real, if narrow,
        // use-after-free (this bug predates narrowing -- found and fixed
        // alongside it, not introduced by it).
        if (context.quit || IsWindowManagementRequest(context.interactiveRequest)) {
            break;
        }
        if (inputMode_ != InputMode::Normal) {
            break;
        }
    }
    replayingMacro_ = false;
}

void BufferView::ClampPointToNarrowing() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.IsNarrowed()) {
        return;
    }
    const auto [start, end] = buffer.NarrowedRange();
    // end is exclusive (the excluded next line's own start byte, or
    // ByteLength() if there is none) -- allowing point to sit exactly at
    // end would let it rest at that excluded line's own start, which
    // Content().ByteOffsetToLine (and so the mode line's own L: indicator)
    // correctly, if confusingly, reports as *being on* the excluded line --
    // a real, confirmed-via-manual-pty-testing bug, not a hypothetical one.
    // The largest valid point is one byte before end: the end of the
    // narrowed range's own last line, right before its trailing newline,
    // matching Buffer::NarrowToRegion's own identical clamp.
    const std::size_t maxPoint = end > start ? end - 1 : start;
    const std::size_t point    = buffer.Point();
    if (point < start || point > maxPoint) {
        buffer.SetPoint(std::clamp(point, start, maxPoint));
    }
}

void BufferView::StartInteractiveSession(editor::InteractiveRequest request) {
    switch (request) {
        case editor::InteractiveRequest::IsearchForward:
            inputMode_ = InputMode::IsearchForward;
            search_.emplace(activeBuffer_.Get(), editor::IncrementalSearch::Direction::Forward);
            break;
        case editor::InteractiveRequest::IsearchBackward:
            inputMode_ = InputMode::IsearchBackward;
            search_.emplace(activeBuffer_.Get(), editor::IncrementalSearch::Direction::Backward);
            break;
        case editor::InteractiveRequest::QueryReplace:
            inputMode_ = InputMode::QueryReplace;
            queryReplace_.emplace(activeBuffer_.Get());
            break;
        case editor::InteractiveRequest::ConfirmQuit: {
            inputMode_ = InputMode::ConfirmQuit;
            std::string names;
            for (const auto& buffer : bufferList_.Buffers()) {
                if (buffer->Modified()) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += buffer->Name();
                }
            }
            statusMessage_ = "Unsaved changes in: " + names + " -- quit anyway? (y/n)";
            return;
        }
        case editor::InteractiveRequest::FindFile:
            inputMode_ = InputMode::FindFile;
            prompt_.emplace("Find file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::SwitchToBuffer:
            inputMode_ = InputMode::SwitchToBuffer;
            prompt_.emplace("Switch to buffer: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::ProjectSearch:
            inputMode_ = InputMode::ProjectSearch;
            prompt_.emplace("Project search: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::VisitSearchResult:
            VisitSearchResult();
            return;
        case editor::InteractiveRequest::ProjectReplace:
            inputMode_ = InputMode::ProjectReplace;
            projectReplace_.emplace(editor::ProjectRoot());
            statusMessage_ = projectReplace_->StatusText();
            return;
        case editor::InteractiveRequest::ToggleProjectSidebar:
            // Flipping .active alone is sufficient -- unlike the
            // pre-migration version, no forced-reflow equivalent is needed
            // (see BufferView.h's own comment on SetProjectSidebar).
            if (projectSidebar_ != nullptr) {
                projectSidebar_->active = !projectSidebar_->active;
            }
            return;
        case editor::InteractiveRequest::CreateDirectory:
            inputMode_ = InputMode::CreateDirectory;
            prompt_.emplace("Create directory: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DeleteFile:
            inputMode_   = InputMode::DeleteFile;
            deleteStage_ = DeleteFileStage::EnteringPath;
            prompt_.emplace("Delete file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::RenameFile:
            inputMode_   = InputMode::RenameFile;
            renameStage_ = RenameFileStage::EnteringSource;
            prompt_.emplace("Rename file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::FindScratch:
            inputMode_ = InputMode::FindScratch;
            prompt_.emplace("Find scratch: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::ExecuteCommand:
            // Deviates from the other cases' bare-label shape: an
            // immediately-visible, browsable candidate list is central to
            // this feature (unlike FindFile/SwitchToBuffer/etc., where
            // there's no meaningful "top" completion to show before any
            // input, or it'd mean an eager filesystem scan) -- so this
            // populates the ranked list (empty query -> every registered
            // command, alphabetical) right away via RefreshExecuteCommandStatus
            // rather than just prompt_->StatusText().
            inputMode_ = InputMode::ExecuteCommand;
            prompt_.emplace("M-x ");
            executeCommandSelection_ = 0;
            RefreshExecuteCommandStatus();
            return;
        // kmacro-start-macro/kmacro-end-or-call-macro follow-up: one-shot
        // direct actions, same shape as ToggleProjectSidebar -- inputMode_
        // stays Normal, no prompt session. The actual recording state lives
        // on dispatcher_ (Dispatcher::StartRecording/StopRecording/
        // IsRecording/LastMacro), not here.
        case editor::InteractiveRequest::StartKbdMacro:
            dispatcher_.StartRecording();
            statusMessage_ = "Recording keyboard macro...";
            return;
        case editor::InteractiveRequest::EndOrCallKbdMacro:
            if (dispatcher_.IsRecording()) {
                // This very keypress's own chord(s) were just appended to
                // the in-progress recording by Feed (recording_ was still
                // true throughout that call) -- strip them back out before
                // finalizing, so the macro's own terminator never ends up
                // inside it. See Dispatcher::DiscardMostRecentlyRecordedChords's
                // own doc comment for why this has to happen here, not
                // inside Dispatcher::Feed/StopRecording themselves.
                dispatcher_.DiscardMostRecentlyRecordedChords();
                dispatcher_.StopRecording();
                statusMessage_ =
                    "Keyboard macro recorded (" + std::to_string(dispatcher_.LastMacro().size()) + " keys).";
            }
            else {
                ReplayMacro();
            }
            return;
        // point-to-register/jump-to-register/copy-to-register/insert-register
        // follow-up: each just waits for one more character (the register
        // name) via the shared HandleRegisterKey -- no MinibufferPrompt,
        // there's nothing to accumulate, just a bare label like every other
        // prompt-shaped case here uses.
        case editor::InteractiveRequest::PointToRegister:
            inputMode_     = InputMode::PointToRegister;
            statusMessage_ = "Point to register: ";
            return;
        case editor::InteractiveRequest::JumpToRegister:
            inputMode_     = InputMode::JumpToRegister;
            statusMessage_ = "Jump to register: ";
            return;
        case editor::InteractiveRequest::CopyToRegister:
            inputMode_     = InputMode::CopyToRegister;
            statusMessage_ = "Copy to register: ";
            return;
        case editor::InteractiveRequest::InsertRegister:
            inputMode_     = InputMode::InsertRegister;
            statusMessage_ = "Insert register: ";
            return;
        // kill-rectangle/delete-rectangle/yank-rectangle follow-up: one-shot
        // direct actions, same shape as ToggleProjectSidebar -- inputMode_
        // stays Normal, no prompt session. See Editor/Rectangle.h for where
        // the actual operations live.
        case editor::InteractiveRequest::KillRectangle:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No rectangle region selected.";
            }
            else {
                editor::KillRectangle(activeBuffer_.Get(), editor::TabWidth());
                statusMessage_.clear();
            }
            return;
        case editor::InteractiveRequest::DeleteRectangle:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No rectangle region selected.";
            }
            else {
                editor::DeleteRectangle(activeBuffer_.Get(), editor::TabWidth());
                statusMessage_.clear();
            }
            return;
        case editor::InteractiveRequest::YankRectangle:
            if (editor::GlobalRectangleClipboard().Empty()) {
                statusMessage_ = "No rectangle to yank.";
            }
            else {
                editor::YankRectangle(activeBuffer_.Get(), editor::TabWidth());
                statusMessage_.clear();
            }
            return;
        // string-rectangle follow-up: the one rectangle command that's a
        // real prompt session (needs one line of typed replacement text) --
        // HasMark() is checked here, before ever opening the prompt, so
        // there's nothing to cancel out of if there's no region at all.
        case editor::InteractiveRequest::StringRectangle:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No rectangle region selected.";
            }
            else {
                inputMode_ = InputMode::StringRectangle;
                prompt_.emplace("String rectangle: ");
                statusMessage_ = prompt_->StatusText();
            }
            return;
        // narrow-to-region/widen follow-up: one-shot direct actions, same
        // shape as ToggleProjectSidebar -- inputMode_ stays Normal, no
        // prompt session for either.
        case editor::InteractiveRequest::NarrowToRegion:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No region to narrow to.";
            }
            else {
                const auto [start, end] = activeBuffer_.Get().Region();
                activeBuffer_.Get().NarrowToRegion(start, end);
                const std::size_t narrowedStart = activeBuffer_.Get().NarrowedRange().first;
                SetTopLine(activeBuffer_.Get().Content().ByteOffsetToLine(narrowedStart));
                statusMessage_.clear();
            }
            return;
        case editor::InteractiveRequest::Widen:
            activeBuffer_.Get().Widen();
            statusMessage_.clear();
            return;
        case editor::InteractiveRequest::None:
            return;
        // Window-splitting follow-up: structural, operate above the level
        // of a single BufferView -- just forward to whoever registered
        // SetOnWindowRequest (WindowManager), the same "signal intent, host
        // acts on it" shape every InteractiveRequest already uses. inputMode_
        // deliberately stays Normal -- these aren't interactive sessions.
        case editor::InteractiveRequest::SplitBelow:
        case editor::InteractiveRequest::SplitRight:
        case editor::InteractiveRequest::DeleteWindow:
        case editor::InteractiveRequest::DeleteOtherWindows:
        case editor::InteractiveRequest::OtherWindow:
            if (onWindowRequest_) {
                onWindowRequest_(request);
            }
            return;
    }

    statusMessage_ = (inputMode_ == InputMode::QueryReplace) ? queryReplace_->StatusText() : search_->StatusText();
}

void BufferView::EndInteractiveSession() {
    inputMode_ = InputMode::Normal;
    search_.reset();
    queryReplace_.reset();
    prompt_.reset();
    projectReplace_.reset();
    pendingClose_ = nullptr;
    deleteStage_  = DeleteFileStage::EnteringPath;
    deleteTarget_.clear();
    renameStage_ = RenameFileStage::EnteringSource;
    renameSource_.clear();
    executeCommandSelection_ = 0;
    ScrollToShowPoint();
}

void BufferView::HandleSearchKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        search_->Accept();
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        search_->Cancel();
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Backspace) {
        search_->DeleteChar();
    }
    else if (chord.Control && chord.Codepoint == U's' && inputMode_ == InputMode::IsearchForward) {
        search_->RepeatSearch();
    }
    else if (chord.Control && chord.Codepoint == U'r' && inputMode_ == InputMode::IsearchBackward) {
        search_->RepeatSearch();
    }
    else if (IsPlainCharacter(chord)) {
        search_->AppendChar(chord.Codepoint);
    }
    // Anything else (arrow keys, unrelated control combos) is ignored mid-search.

    statusMessage_ = search_->StatusText();
    ScrollToShowPoint();
}

void BufferView::HandleQueryReplaceKey(const editor::KeyChord& chord) {
    const auto stage = queryReplace_->CurrentStage();

    if (stage == editor::QueryReplace::Stage::EnteringPattern || stage == editor::QueryReplace::Stage::EnteringReplacement) {
        if (chord.Special == editor::SpecialKey::Enter) {
            if (stage == editor::QueryReplace::Stage::EnteringPattern) {
                try {
                    queryReplace_->ConfirmPattern();
                }
                catch (const std::regex_error& e) {
                    statusMessage_ = std::string("Invalid regex: ") + e.what();
                    return; // stays in EnteringPattern; don't overwrite the message below
                }
            }
            else {
                queryReplace_->ConfirmReplacement();
            }
        }
        else if (IsQuit(chord)) {
            queryReplace_->Cancel();
        }
        else if (chord.Special == editor::SpecialKey::Backspace) {
            queryReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(chord)) {
            queryReplace_->AppendChar(chord.Codepoint);
        }
    }
    else if (stage == editor::QueryReplace::Stage::Confirming) {
        if (chord.Codepoint == U'y') {
            queryReplace_->ReplaceAndNext();
        }
        else if (chord.Codepoint == U'n') {
            queryReplace_->SkipAndNext();
        }
        else if (chord.Codepoint == U'!') {
            queryReplace_->ReplaceAll();
        }
        else if (chord.Codepoint == U'q' || IsQuit(chord)) {
            queryReplace_->Finish();
        }
    }

    statusMessage_ = queryReplace_->StatusText();

    if (queryReplace_->CurrentStage() == editor::QueryReplace::Stage::Done) {
        EndInteractiveSession();
        return;
    }

    ScrollToShowPoint();
}

void BufferView::HandlePromptKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();

        if (inputMode_ == InputMode::FindFile) {
            const bool isNewFile = !std::filesystem::exists(input);
            try {
                text::Buffer& opened = bufferList_.OpenOrCreateFile(input);
                activeBuffer_.Set(opened);
                statusMessage_ = isNewFile ? "(New file)" : ("Opened " + opened.Name());
            }
            catch (const std::exception& e) {
                statusMessage_ = e.what();
            }
        }
        else if (inputMode_ == InputMode::SwitchToBuffer) {
            if (text::Buffer* found = bufferList_.Find(input)) {
                activeBuffer_.Set(*found);
                statusMessage_.clear();
            }
            else {
                statusMessage_ = "No buffer named \"" + input + "\"";
            }
        }
        else if (inputMode_ == InputMode::CreateDirectory) {
            try {
                editor::CreateProjectDirectory(input);
                statusMessage_ = "Created directory " + input;
                if (projectSidebar_) {
                    projectSidebar_->InvalidateTree();
                }
            }
            catch (const std::exception& e) {
                statusMessage_ = e.what();
            }
        }
        else if (inputMode_ == InputMode::ProjectSearch) {
            try {
                const std::vector<editor::SearchMatch> matches =
                    editor::SearchDirectory(editor::ProjectRoot(), input);

                if (matches.empty()) {
                    statusMessage_ = "No matches for \"" + input + "\"";
                }
                else {
                    BuildResultsBuffer(matches, "*search results*");
                    statusMessage_ = std::to_string(matches.size()) + " match" + (matches.size() == 1 ? "" : "es") +
                                     " for \"" + input + "\" -- C-c C-v to visit";
                }
            }
            catch (const std::regex_error& e) {
                statusMessage_ = std::string("Invalid regex: ") + e.what();
            }
        }
        else if (inputMode_ == InputMode::StringRectangle) {
            editor::StringRectangle(activeBuffer_.Get(), input, editor::TabWidth());
            statusMessage_.clear();
        }
        else { // FindScratch
            if (!editor::IsValidScratchName(input)) {
                statusMessage_ = "Invalid scratch name: \"" + input + "\"";
            }
            else {
                try {
                    std::filesystem::create_directories(editor::ScratchDirectory());
                    text::Buffer& opened = bufferList_.OpenOrCreateFile(editor::ScratchPathForName(input));
                    activeBuffer_.Set(opened);
                    statusMessage_ = "Scratch: " + input;
                }
                catch (const std::exception& e) {
                    statusMessage_ = e.what();
                }
            }
        }

        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Tab && inputMode_ != InputMode::ProjectSearch &&
        inputMode_ != InputMode::CreateDirectory && inputMode_ != InputMode::StringRectangle) {
        CompletePrompt();
        return;
    }

    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
    }
    else if (IsPlainCharacter(chord)) {
        prompt_->AppendChar(chord.Codepoint);
    }
    // Anything else is ignored -- stay in the prompt.

    statusMessage_ = prompt_->StatusText();
}

void BufferView::CompletePrompt() {
    std::vector<std::string> candidates;
    if (inputMode_ == InputMode::FindFile) {
        candidates = text::CompleteFilePath(prompt_->Text());
    }
    else if (inputMode_ == InputMode::FindScratch) {
        candidates = editor::CompleteScratchNames(prompt_->Text());
    }
    else {
        candidates = text::CompleteBufferNames(bufferList_, prompt_->Text());
    }

    if (candidates.empty()) {
        statusMessage_ = prompt_->StatusText();
        return;
    }

    const std::string commonPrefix = LongestCommonPrefix(candidates);
    if (commonPrefix.size() > prompt_->Text().size()) {
        prompt_->SetText(commonPrefix);
    }

    statusMessage_ = (candidates.size() == 1) ? prompt_->StatusText()
                                              : prompt_->StatusText() + "  {" + JoinCandidates(candidates) + "}";
}

void BufferView::VisitSearchResult() {
    const text::Buffer& buffer    = activeBuffer_.Get();
    const text::Rope&   content   = buffer.Content();
    const std::size_t   point     = buffer.Point();
    const std::size_t   line      = content.ByteOffsetToLine(point);
    const std::size_t   lineStart = content.LineToByteOffset(line);
    const std::size_t   lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // Matches exactly what HandlePromptKey's ProjectSearch branch writes:
    // "<absolute path>:<1-indexed line>: <line text>". Greedy .* correctly
    // handles the rare case of a ':' inside the path itself, by backing off
    // to find the *last* plausible ":<digits>:" split.
    static const std::regex resultLinePattern(R"(^(.*):(\d+):)");

    std::smatch match;
    if (!std::regex_search(lineText, match, resultLinePattern)) {
        return; // not a search-result line -- silent no-op, see the header comment
    }

    const std::filesystem::path path       = match[1].str();
    const std::size_t           targetLine = std::stoul(match[2].str());

    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBuffer_.Set(opened);
        opened.SetPoint(opened.ByteOffsetForLineAndColumn(targetLine - 1, 0)); // 1-indexed -> 0-indexed
        statusMessage_.clear();
        ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
}

void BufferView::BuildResultsBuffer(const std::vector<editor::SearchMatch>& matches, const std::string& name) {
    std::string resultsText;
    for (const editor::SearchMatch& match : matches) {
        resultsText += match.file.string() + ":" + std::to_string(match.lineNumber) + ": " + match.lineText + "\n";
    }

    text::Buffer& results = bufferList_.CreateBuffer(name);
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    activeBuffer_.Set(results);
}

void BufferView::HandleProjectReplaceKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
        return;
    }

    const auto stage = projectReplace_->CurrentStage();

    if (stage == editor::ProjectReplace::Stage::EnteringPattern ||
        stage == editor::ProjectReplace::Stage::EnteringReplacement) {
        if (chord.Special == editor::SpecialKey::Enter) {
            if (stage == editor::ProjectReplace::Stage::EnteringPattern) {
                try {
                    projectReplace_->ConfirmPattern();
                }
                catch (const std::regex_error& e) {
                    statusMessage_ = std::string("Invalid regex: ") + e.what();
                    return; // stays in EnteringPattern; don't overwrite the message below
                }
                // Shown as soon as the pattern is confirmed -- while the
                // replacement text is still being typed and while the final
                // y/n confirmation is pending, not just a terse count once
                // everything's already decided. See ROADMAP.md.
                if (!projectReplace_->Matches().empty()) {
                    BuildResultsBuffer(projectReplace_->Matches(), "*project replace*");
                }
            }
            else {
                projectReplace_->ConfirmReplacement();
            }
        }
        else if (chord.Special == editor::SpecialKey::Backspace) {
            projectReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(chord)) {
            projectReplace_->AppendChar(chord.Codepoint);
        }

        statusMessage_ = projectReplace_->StatusText();

        if (projectReplace_->CurrentStage() == editor::ProjectReplace::Stage::Done) {
            EndInteractiveSession(); // ConfirmReplacement found no matches -- StatusText() already said so
        }
        return;
    }

    // Confirming: a single whole-batch y/n, not QueryReplace's per-match y/n/!/q.
    if (chord.Codepoint == U'y') {
        const editor::ReplaceSummary summary = projectReplace_->Confirm();
        statusMessage_                       = "Replaced " + std::to_string(summary.replacementCount) + " occurrence" +
                                               (summary.replacementCount == 1 ? "" : "s") + " in " + std::to_string(summary.filesChanged) +
                                               " file" + (summary.filesChanged == 1 ? "" : "s") + ".";
        EndInteractiveSession();
    }
    else if (chord.Codepoint == U'n') {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
    }
    // Anything else is ignored -- stay in Confirming.
}

std::size_t BufferView::ByteOffsetForPoint(Point at) const {
    const std::size_t line        = topLine_ + static_cast<std::size_t>(std::max(at.y, 0));
    const std::size_t x           = static_cast<std::size_t>(std::max(at.x, 0));
    const std::size_t gutterWidth = GutterWidth();
    // A click inside the gutter itself lands on that line's first column,
    // same as clicking right at the start of the line's text.
    const std::size_t column = (x > gutterWidth) ? x - gutterWidth : 0;
    return activeBuffer_.Get().ByteOffsetForLineAndColumn(line, column, editor::TabWidth());
}

std::size_t BufferView::GutterWidth() const {
    const std::size_t totalLines = activeBuffer_.Get().Content().LineCount();
    return std::to_string(totalLines).size() + 1; // digits + one separating column
}

bool BufferView::OnMouseEvent(ftxui::Event event) {
    const ftxui::Mouse& rawMouse = event.mouse();
    LogMouseEvent(MouseEventTag(rawMouse), rawMouse);

    // A growing sidebar-resize drag (round-2 sidebar follow-up) can deliver
    // move/release events while the cursor is over BufferView, not
    // ProjectSidebar itself -- checked first, regardless of position (every
    // leaf widget receives every mouse event in FTXUI; see Widget.h's own
    // header comment), taking priority over BufferView's own handling.
    if (projectSidebar_ != nullptr && projectSidebar_->IsResizing()) {
        if (rawMouse.motion == ftxui::Mouse::Moved) {
            projectSidebar_->UpdateResize(rawMouse.x);
            return true;
        }
        if (rawMouse.motion == ftxui::Mouse::Released) {
            projectSidebar_->EndResize();
            return true;
        }
    }

    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    // Wheel scrolls the viewport without moving point, regardless of
    // InputMode -- unlike click/drag below, which only make sense in
    // Normal mode.
    if (mouse->button == ftxui::Mouse::WheelUp || mouse->button == ftxui::Mouse::WheelDown) {
        constexpr std::size_t kWheelScrollLines = 3;
        if (mouse->button == ftxui::Mouse::WheelUp) {
            SetTopLine((topLine_ > kWheelScrollLines) ? topLine_ - kWheelScrollLines : 0);
        }
        else {
            SetTopLine(topLine_ + kWheelScrollLines);
        }
        return true;
    }

    if (inputMode_ != InputMode::Normal || mouse->button != ftxui::Mouse::Left) {
        return false;
    }

    if (mouse->motion == ftxui::Mouse::Pressed) {
        // Window-splitting follow-up: harmless/no-op today (the sole
        // focusable widget already), necessary once multiple BufferViews
        // exist side by side -- a click into a pane is how focus moves
        // there, mirroring real Emacs' own "clicking a window selects it."
        TakeFocus();
        text::Buffer&     buffer = activeBuffer_.Get();
        const std::size_t offset = ByteOffsetForPoint(mouse->at);
        buffer.ClearMark();
        buffer.SetPoint(offset);
        dragAnchor_ = offset;
        return true;
    }
    if (mouse->motion == ftxui::Mouse::Moved) {
        text::Buffer& buffer = activeBuffer_.Get();
        if (!buffer.HasMark()) {
            buffer.SetMark(dragAnchor_);
        }
        buffer.SetPoint(ByteOffsetForPoint(mouse->at));
        ScrollToShowPoint();
        return true;
    }
    return false; // Released -- no behavior beyond the resize handoff above
}

void BufferView::LogMouseEvent(std::string_view event, const ftxui::Mouse& mouse) const {
    if (!debugMouseLogPath_) {
        return;
    }

    std::ofstream log(*debugMouseLogPath_, std::ios::app);
    if (!log) {
        return;
    }

    const text::Buffer& buffer = activeBuffer_.Get();
    // mouse.x/y are absolute (screen-space) coordinates here, unlike the
    // pre-migration version's already-widget-local ox::Mouse::at -- FTXUI
    // doesn't translate mouse coordinates before delivery (see Widget.h's
    // own header comment), and this logs the raw event as received, before
    // LocalMouseEvent's own translation.
    log << event << " at=(" << mouse.x << ',' << mouse.y << ')' << " button=" << static_cast<int>(mouse.button)
        << " inputMode=" << static_cast<int>(inputMode_) << " point=" << buffer.Point()
        << " mark=" << (buffer.HasMark() ? static_cast<long long>(buffer.Mark()) : -1LL) << " topLine=" << topLine_
        << " size=" << size().width << 'x' << size().height << '\n';
}

void BufferView::HandleConfirmQuitKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        // See the identical null check in OnKeyEvent's own context.quit
        // branch for why this is required, not defensive.
        if (ftxui::ScreenInteractive* screen = ftxui::ScreenInteractive::Active()) {
            screen->Exit();
        }
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Quit cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::RequestCloseBuffer(text::Buffer& buffer) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }

    if (!buffer.Modified()) {
        CloseBufferNow(buffer);
        return;
    }

    pendingClose_  = &buffer;
    inputMode_     = InputMode::ConfirmCloseBuffer;
    statusMessage_ = "Buffer \"" + buffer.Name() + "\" has unsaved changes; close anyway? (y/n)";
}

void BufferView::HandleConfirmCloseBufferKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        text::Buffer* buffer = pendingClose_;
        EndInteractiveSession(); // clears pendingClose_ and inputMode_ before CloseBufferNow touches activeBuffer_
        if (buffer != nullptr) {
            CloseBufferNow(*buffer);
        }
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Close cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::HandleDeleteFileKey(const editor::KeyChord& chord) {
    if (deleteStage_ == DeleteFileStage::EnteringPath) {
        if (chord.Special == editor::SpecialKey::Enter) {
            const std::string input = prompt_->Text();
            if (!std::filesystem::exists(input)) {
                statusMessage_ = "No such file or directory: " + input;
                EndInteractiveSession();
                return;
            }
            deleteTarget_ = input;
            deleteStage_  = DeleteFileStage::Confirming;
            prompt_.reset();
            statusMessage_ = "Delete \"" + deleteTarget_.string() + "\"? (y/n)";
            return;
        }
        if (IsQuit(chord)) {
            statusMessage_.clear();
            EndInteractiveSession();
            return;
        }
        if (chord.Special == editor::SpecialKey::Backspace) {
            prompt_->DeleteChar();
        }
        else if (IsPlainCharacter(chord)) {
            prompt_->AppendChar(chord.Codepoint);
        }
        statusMessage_ = prompt_->StatusText();
        return;
    }

    // Confirming
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        try {
            editor::DeleteProjectPath(deleteTarget_);
            statusMessage_ = "Deleted " + deleteTarget_.string();
            if (projectSidebar_) {
                projectSidebar_->InvalidateTree();
            }
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        EndInteractiveSession();
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Delete cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::HandleRenameFileKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();

        if (renameStage_ == RenameFileStage::EnteringSource) {
            if (!std::filesystem::exists(input)) {
                statusMessage_ = "No such file or directory: " + input;
                EndInteractiveSession();
                return;
            }
            renameSource_ = input;
            renameStage_  = RenameFileStage::EnteringDestination;
            prompt_.emplace("Rename \"" + renameSource_.string() + "\" to: ");
            statusMessage_ = prompt_->StatusText();
            return;
        }

        // EnteringDestination
        const std::filesystem::path destination = input;
        try {
            // Snapshot every open buffer's canonical path *before* the actual
            // rename happens on disk -- weakly_canonical needs real ancestors
            // to resolve symlinks through, and once renameSource_ (or an
            // ancestor of a buffer nested inside it, for a directory rename)
            // is gone, there's nothing left on disk at the old location to
            // resolve against.
            std::vector<std::pair<text::Buffer*, std::filesystem::path>> openBuffers;
            for (const auto& candidate : bufferList_.Buffers()) {
                if (candidate->Path().has_value()) {
                    openBuffers.emplace_back(candidate.get(), std::filesystem::weakly_canonical(*candidate->Path()));
                }
            }
            const std::filesystem::path sourceCanonical = std::filesystem::weakly_canonical(renameSource_);

            editor::RenameProjectPath(renameSource_, destination);

            // Any open buffer whose file *was* renameSource_ itself, or was
            // nested inside it (renaming a directory that has open buffers
            // somewhere underneath it), follows to its new location so it
            // doesn't end up silently pointing at a now-nonexistent path.
            // Only the destination's on-disk existence changes here -- a
            // buffer's own Name() only changes for an exact match, since a
            // nested buffer's filename is unaffected by an ancestor
            // directory being renamed.
            for (auto& [buffer, canonicalPath] : openBuffers) {
                if (canonicalPath == sourceCanonical) {
                    buffer->SetPath(destination);
                    buffer->Rename(destination.filename().string());
                }
                else if (const std::filesystem::path relative = canonicalPath.lexically_relative(sourceCanonical);
                         !relative.empty() && *relative.begin() != "..") {
                    buffer->SetPath(destination / relative);
                }
            }

            statusMessage_ = "Renamed to " + destination.string();
            if (projectSidebar_) {
                projectSidebar_->InvalidateTree();
            }
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
    }
    else if (IsPlainCharacter(chord)) {
        prompt_->AppendChar(chord.Codepoint);
    }
    statusMessage_ = prompt_->StatusText();
}

void BufferView::HandleRegisterKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a register name
    }

    const char32_t name = chord.Codepoint;
    switch (inputMode_) {
        case InputMode::PointToRegister:
            registers_.SetPoint(name, activeBuffer_.Get().Name(), activeBuffer_.Get().Point());
            statusMessage_ = "Point stored in register.";
            break;
        case InputMode::JumpToRegister:
            if (const editor::PointRegisterValue* value = registers_.Point(name)) {
                if (text::Buffer* target = bufferList_.Find(value->bufferName)) {
                    activeBuffer_.Set(*target);
                    target->SetPoint(value->byteOffset); // Buffer::SetPoint already clamps out-of-range offsets
                    statusMessage_.clear();
                }
                else {
                    statusMessage_ = "Buffer for that register no longer exists.";
                }
            }
            else {
                statusMessage_ = "Register does not contain a position.";
            }
            break;
        case InputMode::CopyToRegister:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No region to copy.";
            }
            else {
                const auto [start, end] = activeBuffer_.Get().Region();
                registers_.SetText(name, activeBuffer_.Get().Content().Substring(start, end - start));
                statusMessage_ = "Copied to register.";
            }
            break;
        case InputMode::InsertRegister:
            if (const std::string* text = registers_.Text(name)) {
                activeBuffer_.Get().InsertAtPoint(*text);
                statusMessage_.clear();
            }
            else {
                statusMessage_ = "Register does not contain text.";
            }
            break;
        default:
            break; // unreachable -- OnKeyEvent only routes here for the four modes above
    }

    EndInteractiveSession();
}

void BufferView::RefreshExecuteCommandStatus() {
    const std::vector<std::string> ranked =
        editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());
    executeCommandSelection_ = ranked.empty() ? 0 : std::min(executeCommandSelection_, ranked.size() - 1);

    statusMessage_ = ranked.empty() ? prompt_->StatusText()
                                    : prompt_->StatusText() + "  {" +
                                          FormatExecuteCommandCandidates(ranked, executeCommandSelection_) + "}";
}

void BufferView::HandleExecuteCommandKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::vector<std::string> ranked =
            editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());

        if (ranked.empty()) {
            statusMessage_ = "No command matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string name = ranked[std::min(executeCommandSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        editor::CommandContext context = MakeContext();
        context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
        RunCommandAndHandleOutcome(context, [&] { dispatcher_.Registry().Invoke(name, context); });
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down) {
        const std::vector<std::string> ranked =
            editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());
        if (!ranked.empty() && executeCommandSelection_ + 1 < ranked.size()) {
            ++executeCommandSelection_;
        }
        RefreshExecuteCommandStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        if (executeCommandSelection_ > 0) {
            --executeCommandSelection_;
        }
        RefreshExecuteCommandStatus();
        return;
    }

    // Typing always re-snaps the selection back to the top-ranked match
    // (index 0) -- preserving a numeric index across a re-sorted candidate
    // list would silently select an unrelated command, the same footgun
    // VSCode/Sublime-style command palettes avoid by resetting to the top
    // match on every keystroke. Tab is deliberately not bound to anything
    // here: since typing already re-snaps to the top match, Enter with no
    // arrow presses already invokes it directly -- a Tab-jumps-to-top-match
    // affordance would be redundant.
    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
        executeCommandSelection_ = 0;
        RefreshExecuteCommandStatus();
        return;
    }
    if (IsPlainCharacter(chord)) {
        prompt_->AppendChar(chord.Codepoint);
        executeCommandSelection_ = 0;
        RefreshExecuteCommandStatus();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::CloseBufferNow(text::Buffer& buffer) {
    const bool        wasActive = (&activeBuffer_.Get() == &buffer);
    const std::string name      = buffer.Name();

    text::Buffer* replacement = nullptr;
    if (wasActive) {
        for (const auto& candidate : bufferList_.Buffers()) {
            if (candidate.get() != &buffer) {
                replacement = candidate.get();
                break;
            }
        }
        // Closing the only remaining buffer would leave nothing to edit --
        // Emacs itself never lets a frame end up with zero buffers either,
        // it just conjures a fresh *scratch* -- so do the same here rather
        // than refusing outright.
        if (replacement == nullptr) {
            replacement = &bufferList_.CreateBuffer("scratch");
        }
    }

    // Window-splitting follow-up: fired before the actual erase, while
    // `buffer` is still genuinely alive -- so a multi-pane owner can
    // retarget any *other* pane whose own ActiveBuffer also pointed at it.
    // This BufferView's own activeBuffer_ is already handled below,
    // independently of this hook.
    if (onBufferClosed_) {
        onBufferClosed_(buffer);
    }

    bufferList_.Close(name);
    if (wasActive && replacement != nullptr) {
        activeBuffer_.Set(*replacement);
    }
    statusMessage_.clear();
}

void BufferView::ScrollToShowPoint() {
    const text::Rope& content   = activeBuffer_.Get().Content();
    const std::size_t pointLine = content.ByteOffsetToLine(activeBuffer_.Get().Point());

    if (pointLine < topLine_) {
        topLine_ = pointLine;
    }
    else if (size().height > 0) {
        const auto visibleLines = static_cast<std::size_t>(size().height);
        if (pointLine >= topLine_ + visibleLines) {
            topLine_ = pointLine - visibleLines + 1;
        }
    }
}

std::size_t BufferView::TopLine() const {
    return topLine_;
}

void BufferView::SetTopLine(std::size_t line) {
    const std::size_t rangeStart = NarrowedLineRange().first;
    topLine_                     = std::clamp(line, rangeStart, MaxTopLine());
}

std::pair<std::size_t, std::size_t> BufferView::NarrowedLineRange() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.IsNarrowed()) {
        return {0, buffer.Content().LineCount()};
    }
    const auto [narrowedStart, narrowedEnd] = buffer.NarrowedRange();
    const std::size_t startLine             = buffer.Content().ByteOffsetToLine(narrowedStart);
    // narrowedEnd is a byte offset at a line's own start (see
    // Buffer::NarrowToRegion's whole-line-snapping doc comment) -- except
    // when it's the buffer's own real end, which may fall mid-line. Either
    // way, the line *containing* the byte just before it is the narrowed
    // range's own last line; +1 makes this exclusive, matching
    // Content().LineCount()'s own convention.
    const std::size_t endLine = buffer.Content().ByteOffsetToLine(narrowedEnd > 0 ? narrowedEnd - 1 : 0) + 1;
    return {startLine, endLine};
}

std::size_t BufferView::MaxTopLine() const {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    const std::size_t totalLines      = rangeEnd - rangeStart;
    const auto        visibleLines    = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    return rangeStart + ((totalLines > visibleLines) ? totalLines - visibleLines : 0);
}

void BufferView::SetScrollBar(ScrollBar* scrollBar) {
    scrollBar_ = scrollBar;
}

void BufferView::SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down) {
    scrollUpArrow_   = up;
    scrollDownArrow_ = down;
}

void BufferView::SetProjectSidebar(ProjectSidebar* sidebar) {
    projectSidebar_ = sidebar;
}

bool BufferView::InSelection(std::size_t byteOffset) const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.HasMark()) {
        return false;
    }
    const auto [start, end] = buffer.Region();
    return byteOffset >= start && byteOffset < end;
}

bool BufferView::InIsearchMatch(std::size_t byteOffset) const {
    if (!search_ || !search_->Found()) {
        return false;
    }

    const std::string& query = search_->Query();
    if (query.empty()) {
        return false;
    }

    // Forward search leaves point at the match end, backward at the match
    // start (IncrementalSearch's own documented convention) -- recover the
    // full range from point + query length rather than IncrementalSearch
    // exposing it directly, since nothing else needs that.
    std::size_t       matchStart;
    std::size_t       matchEnd;
    const std::size_t point = activeBuffer_.Get().Point();
    if (inputMode_ == InputMode::IsearchBackward) {
        matchStart = point;
        matchEnd   = point + query.size();
    }
    else {
        matchEnd   = point;
        matchStart = (point >= query.size()) ? point - query.size() : 0;
    }

    return byteOffset >= matchStart && byteOffset < matchEnd;
}

} // namespace ned::ui
