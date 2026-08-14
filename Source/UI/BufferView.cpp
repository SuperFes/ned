#include "BufferView.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <regex>
#include <utility>
#include <vector>

#include "Editor/ProjectFileOps.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/ScratchPad.h"
#include "Editor/TabWidth.h"
#include "KeyTranslation.h"

namespace ned::ui {

namespace {

    // Auto-saved-scratch-pads follow-up: frequent enough that a scratch note
    // never sits unsaved for long, infrequent enough that it's just a cheap
    // periodic sweep (checking Modified() on each open buffer) rather than
    // anything that needs to be debounced against every keystroke -- there's
    // no per-buffer "on modified" hook in this codebase to debounce against
    // anyway, see Text/Buffer.h.
    constexpr std::chrono::milliseconds kScratchAutoSaveInterval{5000};

    // Plain, non-modifier printable input: the only kind of chord that should
    // feed into a query string during isearch/query-replace/prompt text entry.
    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // Gutter selection highlighting (gutter-highlight follow-up): whether a
    // line is untouched, partially, or fully covered by the current region.
    // lineEndExclusive is the offset just past the line's own newline (or
    // ByteLength() for the last line) -- deliberately *including* the
    // newline, unlike paint()'s content-rendering lineEnd, so that selecting
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
    // binary placeholder, 1 for every ordinary glyph. Shared by paint()'s
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
    // byteOffset does, matching the horizontal-scroll cutoff paint()
    // already applies to the cursor (there's no point computing an exact
    // value for a column that won't be shown anyway) -- critically, this
    // bound is also what keeps the scan O(maxColumns) instead of
    // O(byteOffset - lineStart): point can be millions of bytes into a
    // single pathologically long line while still being nowhere near the
    // visible viewport width, and this must not re-scan that whole distance
    // on every paint() call to find out.
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
    // row from paint(), *not* once per rendered codepoint, so ClassAtOffset
    // below only ever scans a small, per-line list rather than the whole
    // file's spans on every single codepoint. An earlier version had
    // ClassAtOffset scan the full (unfiltered) span list per codepoint
    // directly; correct, but regressed a large-JSON [Performance] test to
    // ~44ms/paint() call (~8,000 spans x up to 1,920 rendered codepoints per
    // frame) even after the per-paint caching fix below it was written
    // alongside -- caught by that same test before shipping, the fix
    // narrowed further to this two-tier filter.
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
    // for why. Deliberately not a precomputed per-line array indexed by byte
    // offset: an earlier version built one sized to the whole line
    // regardless of how much of it is actually visible, which regressed the
    // "pathologically long single line" [Performance] test to multiple
    // seconds (a 5-million-entry allocation-and-fill per row, per paint()
    // call) -- the exact same class of bug VisualColumn's own unbounded scan
    // was before it was bounded to the viewport, caught here the same way:
    // by that pre-existing test, before shipping.
    editor::SyntaxClass ClassAtOffset(const std::vector<editor::HighlightSpan>& spans, std::size_t byteOffset) {
        editor::SyntaxClass cls = editor::SyntaxClass::Default;
        for (const editor::HighlightSpan& span : spans) {
            if (span.startByte <= byteOffset && byteOffset < span.endByte) {
                cls = span.syntaxClass;
            }
        }
        return cls;
    }

} // namespace

BufferView::BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, text::BufferList& bufferList,
                       editor::Dispatcher& dispatcher, std::string& statusMessage, const editor::Mode& mode,
                       const Theme& theme) : Widget{ox::FocusPolicy::Strong, ox::SizePolicy::flex()},
                                             activeBuffer_(activeBuffer),
                                             killRing_(killRing),
                                             bufferList_(bufferList),
                                             dispatcher_(dispatcher),
                                             statusMessage_(statusMessage),
                                             mode_(mode),
                                             theme_(theme),
                                             autoSaveTimer_(*this, kScratchAutoSaveInterval) {
    if (const char* path = std::getenv("NED_DEBUG_MOUSE"); path && *path) {
        debugMouseLogPath_ = path;
    }
}

editor::CommandContext BufferView::MakeContext() {
    return editor::CommandContext{activeBuffer_.Get(), killRing_, bufferList_, editor::KeyChord{}, &statusMessage_};
}

void BufferView::StartAutoSaveTimer() {
    autoSaveTimer_.start();
}

void BufferView::timer() {
    editor::AutoSaveScratchBuffers(bufferList_);
}

void BufferView::paint(ox::Canvas c) {
    text::Buffer&     buffer     = activeBuffer_.Get();
    const text::Rope& content    = buffer.Content();
    const std::size_t totalLines = content.LineCount();
    const ox::Brush   emptyBrush = theme_.BrushFor(editor::SyntaxClass::Default);
    const std::size_t point      = buffer.Point();
    const std::size_t pointLine  = content.ByteOffsetToLine(point);

    if (scrollBar_ != nullptr) {
        // scrollable_length is fed as MaxTopLine() + 1, not totalLines: ox::ScrollBar
        // internally clamps a user-driven drag/wheel's target position to
        // [0, scrollable_length - 1], so this is what makes the bar's own
        // built-in range match ours exactly -- dragging all the way down
        // actually reaches true end-of-file, not one line short of it.
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
    // changed since the last paint() call -- see highlightCacheBuffer_'s own
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

    for (int row = 0; row < c.size.height; ++row) {
        for (int col = 0; col < c.size.width; ++col) {
            c[{.x = col, .y = row}] = ox::Glyph{.brush = emptyBrush};
        }

        const std::size_t line = topLine_ + static_cast<std::size_t>(row);
        if (line >= totalLines) {
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

        const ox::Color gutterForeground =
            (line == pointLine) ? theme_.currentLineNumberForeground : theme_.lineNumberForeground;
        // Digits+padding get the full selection background only when the
        // whole line is covered; the one-column gap after them gets it for
        // Partial too, so a partially-selected line still shows a thin
        // highlighted edge instead of no indication at all.
        const ox::Brush gutterBrush{
            .background = (gutterSelection == GutterSelection::Full) ? theme_.selectionBackground : theme_.background,
            .foreground = gutterForeground,
        };
        const ox::Brush gutterGapBrush{
            .background = (gutterSelection != GutterSelection::None) ? theme_.selectionBackground : theme_.background,
            .foreground = gutterForeground,
        };
        const std::string number  = std::to_string(line + 1); // 1-indexed, matches ModeLine's L/C convention
        const std::size_t padding = gutterDigits > number.size() ? gutterDigits - number.size() : 0;
        for (std::size_t i = 0; i < padding && static_cast<int>(i) < c.size.width; ++i) {
            c[{.x = static_cast<int>(i), .y = row}] = ox::Glyph{.symbol = U' ', .brush = gutterBrush};
        }
        for (std::size_t i = 0; i < number.size() && static_cast<int>(padding + i) < c.size.width; ++i) {
            c[{.x = static_cast<int>(padding + i), .y = row}] = ox::Glyph{.symbol = static_cast<char32_t>(number[i]), .brush = gutterBrush};
        }
        if (static_cast<int>(gutterDigits) < c.size.width) {
            c[{.x = static_cast<int>(gutterDigits), .y = row}] = ox::Glyph{.symbol = U' ', .brush = gutterGapBrush};
        }

        const std::vector<editor::HighlightSpan> lineSpans = SpansForLine(highlightSpans, lineStart, lineEnd);

        std::size_t offset = lineStart;
        int         col    = static_cast<int>(gutterWidth);
        while (offset < lineEnd && col < c.size.width) {
            const auto decoded = content.CodepointAt(offset);

            const editor::SyntaxClass cls   = ClassAtOffset(lineSpans, offset);
            ox::Brush                 brush = theme_.BrushFor(cls);
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
                // Terminal::commit_changes()'s own per-cell diff bookkeeping
                // believes was written, which then corrupts unrelated cells
                // on later frames (stale content from an earlier scroll
                // position "shows through" because the diff thinks those
                // cells are already correct). Expanding to literal space
                // glyphs keeps this widget's one-codepoint-per-column model
                // -- and the real terminal's actual column count -- in
                // agreement. editor::TabWidth() is a *display* setting only;
                // the buffer's real tab byte is untouched.
                const int tabWidth = editor::TabWidth();
                for (int i = 0; i < tabWidth && col < c.size.width; ++i) {
                    c[{.x = col, .y = row}] = ox::Glyph{.symbol = U' ', .brush = brush};
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
                ox::Brush binaryBrush    = brush;
                binaryBrush.foreground   = theme_.binaryForeground;
                const char32_t glyphs[4] = {kBinaryOpen, HexDigit((decoded.codepoint >> 4) & 0xF),
                                            HexDigit(decoded.codepoint & 0xF), kBinaryClose};
                for (const char32_t glyph : glyphs) {
                    if (col >= c.size.width) {
                        break;
                    }
                    c[{.x = col, .y = row}] = ox::Glyph{.symbol = glyph, .brush = binaryBrush};
                    ++col;
                }
            }
            else {
                ox::Glyph glyph;
                glyph.symbol            = decoded.codepoint;
                glyph.brush             = brush;
                c[{.x = col, .y = row}] = glyph;
                ++col;
            }

            offset += decoded.byteLength;
        }
    }

    if (pointLine >= topLine_ && pointLine - topLine_ < static_cast<std::size_t>(c.size.height)) {
        const std::size_t        lineStart = content.LineToByteOffset(pointLine);
        const std::optional<int> visualCol = VisualColumn(content, lineStart, point, c.size.width - static_cast<int>(gutterWidth));
        const std::size_t        col       = visualCol ? gutterWidth + static_cast<std::size_t>(*visualCol) : 0;

        if (visualCol && col < static_cast<std::size_t>(c.size.width)) {
            this->cursor = ox::Point{.x = static_cast<int>(col), .y = static_cast<int>(pointLine - topLine_)};
        }
        else {
            this->cursor = std::nullopt; // scrolled off horizontally; no horizontal scroll in v1
        }
    }
    else {
        this->cursor = std::nullopt;
    }
}

void BufferView::key_press(ox::Key key) {
    if (inputMode_ == InputMode::IsearchForward || inputMode_ == InputMode::IsearchBackward) {
        HandleSearchKey(key);
        return;
    }
    if (inputMode_ == InputMode::QueryReplace) {
        HandleQueryReplaceKey(key);
        return;
    }
    if (inputMode_ == InputMode::ProjectReplace) {
        HandleProjectReplaceKey(key);
        return;
    }
    if (inputMode_ == InputMode::ConfirmQuit) {
        HandleConfirmQuitKey(key);
        return;
    }
    if (inputMode_ == InputMode::ConfirmCloseBuffer) {
        HandleConfirmCloseBufferKey(key);
        return;
    }
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::SwitchToBuffer ||
        inputMode_ == InputMode::ProjectSearch || inputMode_ == InputMode::CreateDirectory ||
        inputMode_ == InputMode::FindScratch) {
        HandlePromptKey(key);
        return;
    }
    if (inputMode_ == InputMode::DeleteFile) {
        HandleDeleteFileKey(key);
        return;
    }
    if (inputMode_ == InputMode::RenameFile) {
        HandleRenameFileKey(key);
        return;
    }

    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    editor::CommandContext context = MakeContext();
    context.viewportHeight         = this->size.height > 0 ? static_cast<std::size_t>(this->size.height) : 0;

    try {
        dispatcher_.Feed(*chord, context);
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }

    if (context.quit) {
        ox::Application::quit(0);
        return;
    }

    if (context.interactiveRequest != editor::InteractiveRequest::None) {
        StartInteractiveSession(context.interactiveRequest);
        return;
    }

    ScrollToShowPoint();
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
            if (projectSidebar_ != nullptr) {
                projectSidebar_->active = !projectSidebar_->active;
                if (sidebarRow_ != nullptr) {
                    sidebarRow_->resize(sidebarRow_->size); // see SetSidebarRow -- active alone doesn't reflow
                }
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
        case editor::InteractiveRequest::None:
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
    ScrollToShowPoint();
}

void BufferView::HandleSearchKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (chord->Special == editor::SpecialKey::Enter) {
        search_->Accept();
        EndInteractiveSession();
        return;
    }
    if (IsQuit(*chord)) {
        search_->Cancel();
        EndInteractiveSession();
        return;
    }

    if (chord->Special == editor::SpecialKey::Backspace) {
        search_->DeleteChar();
    }
    else if (chord->Control && chord->Codepoint == U's' && inputMode_ == InputMode::IsearchForward) {
        search_->RepeatSearch();
    }
    else if (chord->Control && chord->Codepoint == U'r' && inputMode_ == InputMode::IsearchBackward) {
        search_->RepeatSearch();
    }
    else if (IsPlainCharacter(*chord)) {
        search_->AppendChar(chord->Codepoint);
    }
    // Anything else (arrow keys, unrelated control combos) is ignored mid-search.

    statusMessage_ = search_->StatusText();
    ScrollToShowPoint();
}

void BufferView::HandleQueryReplaceKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    const auto stage = queryReplace_->CurrentStage();

    if (stage == editor::QueryReplace::Stage::EnteringPattern || stage == editor::QueryReplace::Stage::EnteringReplacement) {
        if (chord->Special == editor::SpecialKey::Enter) {
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
        else if (IsQuit(*chord)) {
            queryReplace_->Cancel();
        }
        else if (chord->Special == editor::SpecialKey::Backspace) {
            queryReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(*chord)) {
            queryReplace_->AppendChar(chord->Codepoint);
        }
    }
    else if (stage == editor::QueryReplace::Stage::Confirming) {
        if (chord->Codepoint == U'y') {
            queryReplace_->ReplaceAndNext();
        }
        else if (chord->Codepoint == U'n') {
            queryReplace_->SkipAndNext();
        }
        else if (chord->Codepoint == U'!') {
            queryReplace_->ReplaceAll();
        }
        else if (chord->Codepoint == U'q' || IsQuit(*chord)) {
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

void BufferView::HandlePromptKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (chord->Special == editor::SpecialKey::Enter) {
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
    if (IsQuit(*chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }
    if (chord->Special == editor::SpecialKey::Tab && inputMode_ != InputMode::ProjectSearch &&
        inputMode_ != InputMode::CreateDirectory) {
        CompletePrompt();
        return;
    }

    if (chord->Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
    }
    else if (IsPlainCharacter(*chord)) {
        prompt_->AppendChar(chord->Codepoint);
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

void BufferView::HandleProjectReplaceKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (IsQuit(*chord)) {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
        return;
    }

    const auto stage = projectReplace_->CurrentStage();

    if (stage == editor::ProjectReplace::Stage::EnteringPattern ||
        stage == editor::ProjectReplace::Stage::EnteringReplacement) {
        if (chord->Special == editor::SpecialKey::Enter) {
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
        else if (chord->Special == editor::SpecialKey::Backspace) {
            projectReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(*chord)) {
            projectReplace_->AppendChar(chord->Codepoint);
        }

        statusMessage_ = projectReplace_->StatusText();

        if (projectReplace_->CurrentStage() == editor::ProjectReplace::Stage::Done) {
            EndInteractiveSession(); // ConfirmReplacement found no matches -- StatusText() already said so
        }
        return;
    }

    // Confirming: a single whole-batch y/n, not QueryReplace's per-match y/n/!/q.
    if (chord->Codepoint == U'y') {
        const editor::ReplaceSummary summary = projectReplace_->Confirm();
        statusMessage_                       = "Replaced " + std::to_string(summary.replacementCount) + " occurrence" +
                                               (summary.replacementCount == 1 ? "" : "s") + " in " + std::to_string(summary.filesChanged) +
                                               " file" + (summary.filesChanged == 1 ? "" : "s") + ".";
        EndInteractiveSession();
    }
    else if (chord->Codepoint == U'n') {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
    }
    // Anything else is ignored -- stay in Confirming.
}

std::size_t BufferView::ByteOffsetForMouse(ox::Mouse mouse) const {
    const std::size_t line        = topLine_ + static_cast<std::size_t>(std::max(mouse.at.y, 0));
    const std::size_t x           = static_cast<std::size_t>(std::max(mouse.at.x, 0));
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

void BufferView::mouse_press(ox::Mouse mouse) {
    LogMouseEvent("press", mouse);

    if (inputMode_ != InputMode::Normal || mouse.button != ox::Mouse::Button::Left) {
        return;
    }

    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t offset = ByteOffsetForMouse(mouse);
    buffer.ClearMark();
    buffer.SetPoint(offset);
    dragAnchor_ = offset;
}

void BufferView::mouse_release(ox::Mouse mouse) {
    LogMouseEvent("release", mouse);

    // A growing sidebar-resize drag (round-2 sidebar follow-up) can end with
    // the cursor over BufferView, not ProjectSidebar itself -- see
    // ProjectSidebar's own header comment for why (no mouse-capture in
    // TermOx). Ending it here regardless of inputMode_/button takes
    // priority over BufferView's own release handling, which currently has
    // none anyway.
    if (projectSidebar_ != nullptr && projectSidebar_->IsResizing()) {
        projectSidebar_->EndResize();
        return;
    }
}

void BufferView::mouse_move(ox::Mouse mouse) {
    LogMouseEvent("move", mouse);

    // Same handoff as mouse_release above: a growing resize drag delivers
    // its move events here once the cursor crosses out of ProjectSidebar's
    // bounds. Checked before the InputMode/button gate below since a resize
    // in progress always takes priority over normal selection handling.
    if (projectSidebar_ != nullptr && projectSidebar_->IsResizing()) {
        projectSidebar_->UpdateResize(this->at.x + mouse.at.x);
        return;
    }

    if (inputMode_ != InputMode::Normal || mouse.button != ox::Mouse::Button::Left) {
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.HasMark()) {
        buffer.SetMark(dragAnchor_);
    }
    buffer.SetPoint(ByteOffsetForMouse(mouse));
    ScrollToShowPoint();
}

void BufferView::mouse_wheel(ox::Mouse mouse) {
    LogMouseEvent("wheel", mouse);

    constexpr std::size_t kWheelScrollLines = 3;

    if (mouse.button == ox::Mouse::Button::ScrollUp) {
        SetTopLine((topLine_ > kWheelScrollLines) ? topLine_ - kWheelScrollLines : 0);
    }
    else if (mouse.button == ox::Mouse::Button::ScrollDown) {
        SetTopLine(topLine_ + kWheelScrollLines);
    }
}

void BufferView::LogMouseEvent(std::string_view event, ox::Mouse mouse) const {
    if (!debugMouseLogPath_) {
        return;
    }

    std::ofstream log(*debugMouseLogPath_, std::ios::app);
    if (!log) {
        return;
    }

    const text::Buffer& buffer = activeBuffer_.Get();
    log << event << " at=(" << mouse.at.x << ',' << mouse.at.y << ')' << " button=" << static_cast<int>(mouse.button)
        << " inputMode=" << static_cast<int>(inputMode_) << " point=" << buffer.Point()
        << " mark=" << (buffer.HasMark() ? static_cast<long long>(buffer.Mark()) : -1LL) << " topLine=" << topLine_
        << " size=" << this->size.width << 'x' << this->size.height << '\n';
}

void BufferView::HandleConfirmQuitKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (chord->Codepoint == U'y' || chord->Codepoint == U'Y') {
        ox::Application::quit(0);
        return;
    }
    if (chord->Codepoint == U'n' || chord->Codepoint == U'N' || IsQuit(*chord)) {
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

void BufferView::HandleConfirmCloseBufferKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (chord->Codepoint == U'y' || chord->Codepoint == U'Y') {
        text::Buffer* buffer = pendingClose_;
        EndInteractiveSession(); // clears pendingClose_ and inputMode_ before CloseBufferNow touches activeBuffer_
        if (buffer != nullptr) {
            CloseBufferNow(*buffer);
        }
        return;
    }
    if (chord->Codepoint == U'n' || chord->Codepoint == U'N' || IsQuit(*chord)) {
        statusMessage_ = "Close cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::HandleDeleteFileKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (deleteStage_ == DeleteFileStage::EnteringPath) {
        if (chord->Special == editor::SpecialKey::Enter) {
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
        if (IsQuit(*chord)) {
            statusMessage_.clear();
            EndInteractiveSession();
            return;
        }
        if (chord->Special == editor::SpecialKey::Backspace) {
            prompt_->DeleteChar();
        }
        else if (IsPlainCharacter(*chord)) {
            prompt_->AppendChar(chord->Codepoint);
        }
        statusMessage_ = prompt_->StatusText();
        return;
    }

    // Confirming
    if (chord->Codepoint == U'y' || chord->Codepoint == U'Y') {
        try {
            editor::DeleteProjectPath(deleteTarget_);
            statusMessage_ = "Deleted " + deleteTarget_.string();
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        EndInteractiveSession();
        return;
    }
    if (chord->Codepoint == U'n' || chord->Codepoint == U'N' || IsQuit(*chord)) {
        statusMessage_ = "Delete cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::HandleRenameFileKey(ox::Key key) {
    const auto chord = TranslateKey(key);
    if (!chord) {
        return;
    }

    if (chord->Special == editor::SpecialKey::Enter) {
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
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        EndInteractiveSession();
        return;
    }
    if (IsQuit(*chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }
    if (chord->Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
    }
    else if (IsPlainCharacter(*chord)) {
        prompt_->AppendChar(chord->Codepoint);
    }
    statusMessage_ = prompt_->StatusText();
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
    else if (this->size.height > 0) {
        const auto visibleLines = static_cast<std::size_t>(this->size.height);
        if (pointLine >= topLine_ + visibleLines) {
            topLine_ = pointLine - visibleLines + 1;
        }
    }
}

std::size_t BufferView::TopLine() const {
    return topLine_;
}

void BufferView::SetTopLine(std::size_t line) {
    topLine_ = std::min(line, MaxTopLine());
}

std::size_t BufferView::MaxTopLine() const {
    const std::size_t totalLines   = activeBuffer_.Get().Content().LineCount();
    const auto        visibleLines = this->size.height > 0 ? static_cast<std::size_t>(this->size.height) : 0;
    return (totalLines > visibleLines) ? totalLines - visibleLines : 0;
}

void BufferView::SetScrollBar(ox::ScrollBar* scrollBar) {
    scrollBar_ = scrollBar;
}

void BufferView::SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down) {
    scrollUpArrow_   = up;
    scrollDownArrow_ = down;
}

void BufferView::SetProjectSidebar(ProjectSidebar* sidebar) {
    projectSidebar_ = sidebar;
}

void BufferView::SetSidebarRow(ox::Widget* sidebarRow) {
    sidebarRow_ = sidebarRow;
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
