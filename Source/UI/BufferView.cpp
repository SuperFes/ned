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

#include "EchoArea.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/FuzzyMatch.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Org.h"
#include "Editor/ProjectAgenda.h"
#include "Editor/ProjectFileOps.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/ProjectTree.h"
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

    // LSP client follow-up: LspServerConfig.h's language keys ("c", "python",
    // ...) are Mode's own name minus its "-mode" suffix -- every bundled
    // *Mode() factory names itself exactly that way (see ModeOverrides.cpp's
    // BundledModeFactories table, e.g. "c-mode"/"python-mode"), so this is a
    // free derivation rather than a second naming table to keep in sync.
    // Modes with no "-mode" suffix (there are none among the bundled ones,
    // but a dynamically-registered one -- Editor/ModeOverrides.h -- could in
    // principle be named anything) are returned unchanged.
    std::string LanguageForMode(const editor::Mode& mode) {
        constexpr std::string_view kSuffix = "-mode";
        if (mode.name.size() > kSuffix.size() && mode.name.ends_with(kSuffix)) {
            return mode.name.substr(0, mode.name.size() - kSuffix.size());
        }
        return mode.name;
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
    // JoinCandidates -- caps how many of the ranked list are actually shown:
    // dumping dozens of command names (or, project-find-file follow-up,
    // project-relative file paths, often much longer) into one
    // terminal-width line is unreadable, and worse, unbounded, would run
    // past the edge of the real terminal and get silently clipped mid-word
    // by EchoArea::Paint. fuzzy-candidate-list-styling follow-up: this used
    // to cap by a fixed *count* (6 candidates, sized for ~10-25-character
    // command names) rather than by the real available width -- fine for
    // M-x, but project-find-file's typically-longer path candidates could
    // still overflow a real terminal's actual width well before 6 items were
    // shown, silently truncating mid-filename with no "+K more" even
    // visible. Caps by *column budget* instead now: grows a window
    // containing `selected` outward (forward first, matching the old
    // window's own forward bias) only as long as the next candidate still
    // fits, reserving kSuffixReserve columns up front for a "+K more" tail
    // so that reservation itself never causes an overflow. Selection still
    // "scrolls" the same way it always did -- the window follows `selected`
    // as arrow keys move it -- just width-aware now instead of count-aware.
    // The selected entry used to get a leading '*', which read as
    // confusing/noisy rather than as a clear grouping marker -- wrapped in
    // real brackets now instead, and bolded (EmphasizeForEchoArea) while
    // every other visible candidate is dimmed (DimForEchoArea) so the
    // selection reads at a glance instead of by scanning for a stray
    // asterisk. See EchoArea.h's own doc comment for how the underlying
    // markup actually reaches the terminal. Renamed from
    // FormatExecuteCommandCandidates once project-find-file showed it was
    // already entirely generic over what's being ranked.
    constexpr std::size_t kSuffixReserve = 12; // room for " +999 more" plus slack, generous but bounded

    std::string FormatFuzzyCandidates(const std::vector<std::string>& ranked, std::size_t selected, std::size_t columnBudget) {
        if (ranked.empty()) {
            return {};
        }
        selected = std::min(selected, ranked.size() - 1);

        const std::size_t usableBudget = columnBudget > kSuffixReserve ? columnBudget - kSuffixReserve : columnBudget;
        auto              displayWidth = [](const std::string& candidate, bool isSelected) {
            return candidate.size() + (isSelected ? 2 : 0); // +2 for the selected entry's own brackets
        };

        std::size_t windowStart = selected;
        std::size_t windowEnd   = selected + 1;
        std::size_t width       = displayWidth(ranked[selected], true);

        bool grew = true;
        while (grew) {
            grew = false;
            if (windowEnd < ranked.size()) {
                const std::size_t next = width + 1 + displayWidth(ranked[windowEnd], false);
                if (next <= usableBudget) {
                    width = next;
                    ++windowEnd;
                    grew = true;
                }
            }
            if (windowStart > 0) {
                const std::size_t next = width + 1 + displayWidth(ranked[windowStart - 1], false);
                if (next <= usableBudget) {
                    width = next;
                    --windowStart;
                    grew = true;
                }
            }
        }

        std::string joined;
        for (std::size_t i = windowStart; i < windowEnd; ++i) {
            if (i > windowStart) {
                joined += ' ';
            }
            joined += (i == selected) ? EmphasizeForEchoArea("[" + ranked[i] + "]") : DimForEchoArea(ranked[i]);
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

    // Org-mode fold/unfold follow-up: painted after a folded headline's own
    // content -- real Org's own visual cue ("...") that there's hidden
    // content below, U+2026 HORIZONTAL ELLIPSIS rather than three literal
    // '.' glyphs (one column instead of three, and distinct from any real
    // "..." a user might have actually typed).
    constexpr char32_t kFoldEllipsis = U'…';

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

    // Links follow-up: sum of CodepointColumns() over text's own codepoints
    // -- the column width a collapsed link's own displayText renders at.
    // Shared by Paint()'s render loop, VisualColumn, and
    // ByteOffsetForColumnInLine below so none of them can disagree about how
    // wide a given displayText actually is on screen. Decodes via a
    // throwaway text::Rope (cheap for the short strings a link's own
    // description/target realistically is) rather than a second, parallel
    // UTF-8 decoder -- Rope::CodepointAt is already this file's single
    // source of truth for "how many bytes/columns does this codepoint take."
    int DisplayColumns(const std::string& text) {
        const text::Rope decoded(text);
        int              columns = 0;
        std::size_t      offset  = 0;
        while (offset < decoded.ByteLength()) {
            const auto cp = decoded.CodepointAt(offset);
            columns += CodepointColumns(cp.codepoint);
            offset += cp.byteLength;
        }
        return columns;
    }

    // Links follow-up: an Org link that should render COLLAPSED on this
    // particular line -- i.e. one whose own [startByte, endByte) does NOT
    // contain point (a link containing point is deliberately excluded here,
    // which is what makes it fall through to every consumer's existing
    // plain-codepoint path instead, rendering its raw markup uncollapsed).
    struct RenderedLink {
        std::size_t startByte;
        std::size_t endByte; // exclusive
        std::string displayText;
    };

    // Filters org::ParseLinks's whole-buffer result down to just the links
    // fully inside [lineStart, lineEnd) that should render collapsed --
    // called once per line, the same "filter once, consult per-codepoint"
    // shape SpansForLine already establishes for syntax highlighting.
    std::vector<RenderedLink> LinksForLine(const std::vector<editor::org::Link>& links, std::size_t lineStart,
                                           std::size_t lineEnd, std::size_t point) {
        std::vector<RenderedLink> rendered;
        for (const editor::org::Link& link : links) {
            if (link.startByte < lineStart || link.endByte > lineEnd) {
                continue; // Org links never span lines, but stay defensive.
            }
            if (point >= link.startByte && point < link.endByte) {
                continue; // point is inside -- render raw, not collapsed.
            }
            rendered.push_back(RenderedLink{
                .startByte   = link.startByte,
                .endByte     = link.endByte,
                .displayText = editor::org::LinkDisplayText(link),
            });
        }
        return rendered;
    }

    // Finds the RenderedLink (if any) starting exactly at offset -- every
    // consumer below only ever needs to check this at each codepoint
    // boundary it visits, links.size() being small (per-line) makes a linear
    // scan the simplest correct option, same as SpansForLine/ClassAtOffset's
    // own approach for highlight spans.
    const RenderedLink* LinkStartingAt(const std::vector<RenderedLink>& links, std::size_t offset) {
        for (const RenderedLink& link : links) {
            if (link.startByte == offset) {
                return &link;
            }
        }
        return nullptr;
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
    // on every Paint() call to find out. lineLinks (links follow-up,
    // defaulted so no call site outside this file needs updating) applies
    // the same "collapse a RenderedLink's own span to DisplayColumns(its
    // displayText)" rule Paint()'s own render loop uses, so the two can
    // never disagree about where point's own column actually lands on a
    // line containing a collapsed link.
    std::optional<int> VisualColumn(const text::Rope& content, std::size_t lineStart, std::size_t byteOffset,
                                    int maxColumns, const std::vector<RenderedLink>& lineLinks = {}) {
        int         col    = 0;
        std::size_t offset = lineStart;
        while (offset < byteOffset) {
            if (col >= maxColumns) {
                return std::nullopt;
            }
            if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                col += DisplayColumns(link->displayText);
                offset = link->endByte;
                continue;
            }
            const auto decoded = content.CodepointAt(offset);
            col += CodepointColumns(decoded.codepoint);
            offset += decoded.byteLength;
        }
        return col;
    }

    // Links follow-up: the click-translation counterpart to VisualColumn --
    // reimplements Buffer::ByteOffsetForLineAndColumn's own tab-aware walk
    // locally (mirroring its algorithm and kMaxTabAwareColumnScan-style
    // bound for the same pathological-long-line safety) rather than
    // extending that method itself, which must stay entirely link-oblivious
    // -- Buffer has zero Org-specific knowledge, a hard, repeated project
    // convention (see e.g. Buffer::FoldMarker's own doc comment). When
    // lineLinks is empty this behaves byte-for-byte identically to
    // Buffer::ByteOffsetForLineAndColumn (verified by a unit test), so
    // ByteOffsetForPoint can call this unconditionally instead of branching
    // on mode name. A click landing within a collapsed link's own column
    // span resolves to that link's startByte -- clicking anywhere on the
    // collapsed text moves point to just before "[[", which naturally
    // un-collapses it on the very next render.
    constexpr std::size_t kMaxTabAwareColumnScan = 512;

    std::size_t ByteOffsetForColumnInLine(const text::Rope& content, std::size_t lineStart, std::size_t lineEnd,
                                          std::size_t targetColumn, int tabWidth,
                                          const std::vector<RenderedLink>& lineLinks) {
        std::size_t offset       = lineStart;
        std::size_t visualColumn = 0;
        std::size_t steps        = 0;
        while (offset < lineEnd && visualColumn < targetColumn) {
            if (steps >= kMaxTabAwareColumnScan) {
                const std::size_t remainingColumns = targetColumn - visualColumn;
                const std::size_t lineEndCodepoint = content.ByteOffsetToCodepointOffset(lineEnd);
                const std::size_t landingCodepoint =
                    std::min(content.ByteOffsetToCodepointOffset(offset) + remainingColumns, lineEndCodepoint);
                return content.CodepointOffsetToByteOffset(landingCodepoint);
            }
            if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                const int linkColumns = DisplayColumns(link->displayText);
                if (targetColumn < visualColumn + static_cast<std::size_t>(linkColumns)) {
                    return link->startByte;
                }
                visualColumn += static_cast<std::size_t>(linkColumns);
                offset = link->endByte;
                ++steps;
                continue;
            }
            const auto decoded = content.CodepointAt(offset);
            visualColumn += (decoded.codepoint == U'\t') ? static_cast<std::size_t>(tabWidth) : 1;
            offset += decoded.byteLength;
            ++steps;
        }
        return offset;
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
    // The buffer active at construction time already has a sensible
    // topLine_ (0, its default) -- seeding this here rather than leaving it
    // nullptr is what makes EnsureTopLineValidForActiveBuffer() correctly
    // distinguish "this is a real switch to a different buffer" from "this
    // is just the very first Paint() call," which would otherwise
    // incorrectly reset topLine_ and discard any scroll adjustment (mouse
    // wheel, scroll bar) made via an event that fires before that first
    // Paint() -- a real regression this exact fix introduced and a test
    // caught before it shipped, not assumed safe.
    topLineValidatedBuffer_ = &activeBuffer_.Get();
}

editor::CommandContext BufferView::MakeContext() {
    editor::CommandContext context{activeBuffer_.Get(), killRing_, bufferList_, editor::KeyChord{}, &statusMessage_};
    context.mode = &mode_;
    return context;
}

void BufferView::SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler) {
    onWindowRequest_ = std::move(handler);
}

void BufferView::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void BufferView::EnsureFoldableBlocksCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!mode_.fold || !editor::CodeFoldingEnabled()) {
        foldableBlocksCache_.clear();
        foldableBlocksCacheBuffer_     = &buffer;
        foldableBlocksCacheGeneration_ = buffer.ContentGeneration();
        return;
    }

    if (foldableBlocksCacheBuffer_ == &buffer && foldableBlocksCacheGeneration_ == buffer.ContentGeneration()) {
        return;
    }

    foldableBlocksCache_           = editor::codefold::FoldableBlocks(mode_, buffer.Text());
    foldableBlocksCacheBuffer_     = &buffer;
    foldableBlocksCacheGeneration_ = buffer.ContentGeneration();
}

void BufferView::EnsureFoldGutterCache() const {
    EnsureFoldableBlocksCache();
    text::Buffer& buffer = activeBuffer_.Get();

    if (foldGutterCacheBuffer_ == &buffer && foldGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        foldGutterCacheFoldGeneration_ == buffer.FoldGeneration()) {
        return;
    }

    foldGutterEntries_.clear();
    for (auto& column : foldGutterLineRangesByColumn_) {
        column.clear();
    }

    if (!foldableBlocksCache_.empty()) {
        const text::Rope& content = buffer.Content();
        const auto        regions = editor::codefold::FoldRegionsWithDepth(foldableBlocksCache_);

        foldGutterEntries_.reserve(regions.size());
        for (const auto& region : regions) {
            const int         column     = std::min(region.depth, kMaxFoldDepthColumns - 1);
            const std::size_t headerLine = content.ByteOffsetToLine(region.startByte);
            const std::size_t closerLine = content.ByteOffsetToLine(region.endByte);
            if (headerLine == closerLine) {
                // A block written entirely on one line (e.g. a one-line
                // function body) has nothing to fold -- collapsing it would
                // hide zero lines (FoldedLineRanges' own [headerLine + 1,
                // closerLine + 1) is empty whenever they're equal), so the
                // gutter shows no ⊞/⊟ for it at all rather than a
                // clickable affordance that visibly does nothing. Purely a
                // rendering/click-target filter -- FoldRegionsWithDepth
                // above still computed this region's real depth, so a
                // *nested* multi-line block still gets its own correct
                // column regardless of a one-line sibling/ancestor skipped
                // here.
                continue;
            }
            foldGutterEntries_.push_back(FoldGutterEntry{
                .headerLine = headerLine,
                .closerLine = closerLine,
                .blockStart = region.startByte,
                .column     = column,
            });
            if (!buffer.FoldMarkerAt(region.startByte).has_value()) {
                // Expanded -- gets a guide line down to (and including) its
                // closer. A collapsed block gets no line at all, only its own
                // ⊞ on its header row -- there's nothing to trace while its
                // body is hidden (an explicit user choice, not an oversight).
                foldGutterLineRangesByColumn_[column].emplace_back(headerLine + 1, closerLine + 1);
            }
        }
    }

    foldGutterCacheBuffer_            = &buffer;
    foldGutterCacheContentGeneration_ = buffer.ContentGeneration();
    foldGutterCacheFoldGeneration_    = buffer.FoldGeneration();
}

void BufferView::EnsureUnsavedChangeCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (unsavedChangeCacheBuffer_ == &buffer && unsavedChangeCacheContentGeneration_ == buffer.ContentGeneration() &&
        unsavedChangeCacheGeneration_ == buffer.UnsavedChangeGeneration()) {
        return;
    }

    unsavedChangeLineRanges_.clear();
    const text::Rope& content = buffer.Content();
    for (const auto& [byteStart, byteEnd] : buffer.UnsavedChangeRanges()) {
        const std::size_t startLine = content.ByteOffsetToLine(byteStart);
        // byteEnd is exclusive and may sit exactly on a line boundary (the
        // byte after the range's own last one) -- back it up by one before
        // converting so a range that ends right at "line N+1, column 0"
        // doesn't get counted as touching line N+1 too.
        const std::size_t endLine = content.ByteOffsetToLine(byteEnd > byteStart ? byteEnd - 1 : byteStart);
        // UnsavedChangeRanges() arrives sorted by byte offset, so startLine
        // here is never less than the previous entry's -- merge with the
        // last pushed range if they touch or overlap, same "already
        // sorted, just coalesce adjacent" approach used elsewhere in this
        // codebase (e.g. Buffer's own MergeUnsavedRange).
        if (!unsavedChangeLineRanges_.empty() && startLine <= unsavedChangeLineRanges_.back().second) {
            unsavedChangeLineRanges_.back().second = std::max(unsavedChangeLineRanges_.back().second, endLine + 1);
        }
        else {
            unsavedChangeLineRanges_.emplace_back(startLine, endLine + 1);
        }
    }

    unsavedChangeCacheBuffer_            = &buffer;
    unsavedChangeCacheContentGeneration_ = buffer.ContentGeneration();
    unsavedChangeCacheGeneration_        = buffer.UnsavedChangeGeneration();
}

namespace {

    // Higher rank = more severe = wins when two diagnostics start on the
    // same line. Matches LSP's own severity ordering (1=Error is the most
    // severe, 4=Hint the least), just inverted to a "bigger number wins"
    // comparison for MostSevere below.
    int DiagnosticSeverityRank(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return 3;
            case text::Buffer::Diagnostic::Severity::Warning:
                return 2;
            case text::Buffer::Diagnostic::Severity::Information:
                return 1;
            case text::Buffer::Diagnostic::Severity::Hint:
                return 0;
        }
        return 0; // unreachable -- silences a "not all enumerators handled" warning on some compilers
    }

} // namespace

void BufferView::EnsureDiagnosticGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (diagnosticGutterCacheBuffer_ == &buffer && diagnosticGutterCacheGeneration_ == buffer.DiagnosticsGeneration()) {
        return;
    }

    diagnosticLineSeverities_.clear();
    const text::Rope& content = buffer.Content();
    // Diagnostics() arrives in whatever order the server reported them, not
    // necessarily sorted by position -- collapse to at most one {line,
    // severity} entry per line (keeping the most severe) via a small local
    // map, then sort by line once at the end for the per-row lower_bound
    // lookup Paint() does.
    std::unordered_map<std::size_t, text::Buffer::Diagnostic::Severity> mostSevereByLine;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const std::size_t line = content.ByteOffsetToLine(diagnostic.startByte);
        const auto        it   = mostSevereByLine.find(line);
        if (it == mostSevereByLine.end() || DiagnosticSeverityRank(diagnostic.severity) > DiagnosticSeverityRank(it->second)) {
            mostSevereByLine[line] = diagnostic.severity;
        }
    }
    diagnosticLineSeverities_.assign(mostSevereByLine.begin(), mostSevereByLine.end());
    std::sort(diagnosticLineSeverities_.begin(), diagnosticLineSeverities_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    diagnosticGutterCacheBuffer_     = &buffer;
    diagnosticGutterCacheGeneration_ = buffer.DiagnosticsGeneration();
}

void BufferView::EnsureHiddenLineRangesCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (buffer.FoldMarkers().empty()) {
        // Fast path: every buffer that's never had org-cycle/code-fold-toggle
        // run at all (i.e. every non-Org, non-folded buffer) takes this
        // branch -- no call into org::FoldedLineRanges/codefold::FoldedLineRanges,
        // no outline re-parse or fold-query call, at all.
        hiddenLineRanges_.clear();
        hiddenLineRangesCacheBuffer_            = &buffer;
        hiddenLineRangesCacheContentGeneration_ = buffer.ContentGeneration();
        hiddenLineRangesCacheFoldGeneration_    = buffer.FoldGeneration();
        return;
    }

    if (hiddenLineRangesCacheBuffer_ == &buffer &&
        hiddenLineRangesCacheContentGeneration_ == buffer.ContentGeneration() &&
        hiddenLineRangesCacheFoldGeneration_ == buffer.FoldGeneration()) {
        return;
    }

    // generic-code-folding follow-up: only a mode with a real fold query
    // goes through the new generic tree-sitter-block path -- every other
    // mode (org-mode included, which has none: it drives FoldMarkers_
    // entirely through org::CycleFoldAtPoint, mode-independently) keeps the
    // original org::FoldedLineRanges path exactly as before. This isn't
    // really "Org-specific" so much as "the only interpretation that
    // existed before this feature" -- preserving it for every mode::fold-
    // less buffer is what keeps pre-existing direct-FoldMarkers_ usage
    // (e.g. a plain FundamentalMode buffer with a marker set by hand)
    // working exactly as it always has.
    if (mode_.fold) {
        EnsureFoldableBlocksCache();
        hiddenLineRanges_ = editor::codefold::FoldedLineRanges(buffer, buffer.Content(), foldableBlocksCache_);
    }
    else {
        hiddenLineRanges_ = editor::org::FoldedLineRanges(buffer);
    }
    hiddenLineRangesCacheBuffer_            = &buffer;
    hiddenLineRangesCacheContentGeneration_ = buffer.ContentGeneration();
    hiddenLineRangesCacheFoldGeneration_    = buffer.FoldGeneration();
}

void BufferView::EnsureLinkCache() const {
    if (mode_.name != "org-mode") {
        // Fast path: every non-Org buffer never even reaches org::ParseLinks
        // -- see this cache's own doc comment in BufferView.h.
        linkCache_.clear();
        linkCacheBuffer_     = nullptr;
        linkCacheGeneration_ = 0;
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (linkCacheBuffer_ == &buffer && linkCacheGeneration_ == buffer.ContentGeneration()) {
        return;
    }

    linkCache_           = editor::org::ParseLinks(buffer.Text());
    linkCacheBuffer_     = &buffer;
    linkCacheGeneration_ = buffer.ContentGeneration();
}

bool BufferView::IsLineHidden(std::size_t line) const {
    EnsureHiddenLineRangesCache();
    for (const auto& [start, end] : hiddenLineRanges_) {
        if (line >= start && line < end)
            return true;
    }
    return false;
}

std::size_t BufferView::NextVisibleLine(std::size_t line, std::size_t limit) const {
    while (line < limit && IsLineHidden(line))
        ++line;
    return line;
}

std::size_t BufferView::AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const {
    while (count > 0 && line < limit) {
        line = NextVisibleLine(line + 1, limit);
        --count;
    }
    return line;
}

std::size_t BufferView::VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        if (!IsLineHidden(line))
            ++count;
    }
    return count;
}

void BufferView::Paint(Canvas c) {
    EnsureTopLineValidForActiveBuffer();

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

    const std::size_t gutterWidth = GutterWidth();
    // status/line-number-spacing follow-up: GutterWidth() already reserves
    // these columns only when actually wanted (see its own doc comment) --
    // recomputing the same condition here keeps the layout math below in
    // agreement with it without a second source of truth. Column offsets,
    // left to right: [status][gap][digits][gap][fold].
    const std::size_t foldColumnWidth = (mode_.fold && editor::CodeFoldingEnabled()) ? kMaxFoldDepthColumns : 0;
    const std::size_t digitsStart     = kStatusWidth + kDiagnosticWidth + kLineNumberGap;
    const std::size_t gutterDigits    = gutterWidth - digitsStart - kLineNumberGap - foldColumnWidth;
    const std::size_t foldStart       = digitsStart + gutterDigits + kLineNumberGap;

    // status-gutter unsaved-change-indicator follow-up: recomputed once
    // per Paint() call (not per row) -- see EnsureUnsavedChangeCache's own
    // doc comment. Unconditional, unlike EnsureFoldGutterCache -- every
    // buffer gets a status column regardless of mode/language.
    EnsureUnsavedChangeCache();
    // LSP client follow-up: same "unconditional, every buffer gets one"
    // reasoning as EnsureUnsavedChangeCache above.
    EnsureDiagnosticGutterCache();

    // LSP client follow-up: syncs the *active* buffer only, once per frame
    // -- see LspManager::SyncBuffer's own doc comment for why only the
    // currently-visible buffer, not every open one. A no-op if lspManager_
    // is unset (ordinary tests) or nothing's configured for this mode's
    // language (LspServerCommand returns nullopt, checked inside SyncBuffer
    // itself).
    if (lspManager_) {
        lspManager_->SyncBuffer(buffer, LanguageForMode(mode_));
    }

    // depth-aware-fold-gutter follow-up: recomputed once per Paint() call
    // (not per row, and not rebuilt from scratch even across separate
    // Paint() calls when neither content nor fold state has changed) -- see
    // EnsureFoldGutterCache's/foldGutterEntries_'s own doc comments in
    // BufferView.h for the real [Performance]-test-driven history behind
    // exactly what's cached here and why (a naive per-row scan, then an
    // unordered_map that measurably made ASan wall time *worse* via its
    // per-insert heap allocations, then finally this: sorted vectors cached
    // alongside foldableBlocksCache_ itself rather than rebuilt every
    // Paint() call).
    EnsureFoldGutterCache();

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

    // Links follow-up: see EnsureLinkCache's own doc comment in BufferView.h
    // for why this is a no-op outside an org-mode buffer.
    EnsureLinkCache();

    // depth-aware-fold-gutter follow-up: streaming state for the per-row
    // gutter rendering below -- one pass over the whole row loop, not
    // rebuilt per row; see that code's own doc comment for why a plain
    // per-column stack is the correct (and correctly performing) structure
    // here. Plain Paint()-local state, reset fresh every call.
    std::size_t                                                foldGutterEntryCursor = 0;
    std::array<const FoldGutterEntry*, kMaxFoldDepthColumns>   foldGutterHeaderAtColumn{};
    std::array<std::size_t, kMaxFoldDepthColumns>              foldColumnCursor{};
    std::array<std::vector<std::size_t>, kMaxFoldDepthColumns> foldColumnOpenEnds;

    // A running buffer-line cursor, seeded at topLine_ (already guaranteed
    // visible by SetTopLine) and advanced by NextVisibleLine each iteration
    // -- Org-mode fold/unfold follow-up: was a flat `topLine_ + row` 1:1
    // mapping; a fold can make "the row-th line below topLine_" and "the
    // row-th buffer line below topLine_" disagree, so this has to walk
    // forward skipping whatever's currently hidden instead.
    std::size_t line = topLine_;
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            ftxui::Cell& cell = c[{.x = col, .y = row}];
            cell.character    = " ";
            emptyBrush.ApplyTo(cell);
        }

        if (line < renderEndLine) {
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
            // status-gutter unsaved-change-indicator follow-up: a solid
            // colored cell (character " ", not a glyph -- a 1-char-wide
            // color swatch, matching the user's own "just 1 char width"
            // ask and ScrollBar's own thumb-via-cell.inverted precedent)
            // when this line has edits since the buffer was last
            // loaded/saved. A plain binary search against
            // unsavedChangeLineRanges_ -- these ranges are flat and
            // disjoint by construction, unlike the fold depth columns, so
            // no streaming stack state is needed here.
            {
                const auto it = std::lower_bound(
                    unsavedChangeLineRanges_.begin(), unsavedChangeLineRanges_.end(), line,
                    [](const auto& range, std::size_t targetLine) { return range.second <= targetLine; });
                const bool   changed        = it != unsavedChangeLineRanges_.end() && it->first <= line;
                const Color  indicatorColor = changed ? theme_.unsavedChangeIndicator : theme_.background;
                const Brush  statusBrush{.background = indicatorColor, .foreground = indicatorColor};
                ftxui::Cell& cell = c[{.x = 0, .y = row}];
                cell.character    = " ";
                statusBrush.ApplyTo(cell);
            }

            // LSP client follow-up: same solid-color-swatch shape as the
            // status column just above, in its own dedicated column --
            // see diagnosticLineSeverities_'s own doc comment for why a
            // plain binary search suffices here too (at most one entry per
            // line, already sorted).
            if (static_cast<int>(kStatusWidth) < c.size().width) {
                const auto it             = std::lower_bound(diagnosticLineSeverities_.begin(), diagnosticLineSeverities_.end(), line,
                                                             [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                const bool hasDiagnostic  = it != diagnosticLineSeverities_.end() && it->first == line;
                Color      indicatorColor = theme_.background;
                if (hasDiagnostic) {
                    switch (it->second) {
                        case text::Buffer::Diagnostic::Severity::Error:
                            indicatorColor = theme_.diagnosticError;
                            break;
                        case text::Buffer::Diagnostic::Severity::Warning:
                            indicatorColor = theme_.diagnosticWarning;
                            break;
                        case text::Buffer::Diagnostic::Severity::Information:
                            indicatorColor = theme_.diagnosticInformation;
                            break;
                        case text::Buffer::Diagnostic::Severity::Hint:
                            indicatorColor = theme_.diagnosticHint;
                            break;
                    }
                }
                const Brush  diagnosticBrush{.background = indicatorColor, .foreground = indicatorColor};
                ftxui::Cell& cell = c[{.x = static_cast<int>(kStatusWidth), .y = row}];
                cell.character    = " ";
                diagnosticBrush.ApplyTo(cell);
            }

            const std::string number  = std::to_string(line + 1); // 1-indexed, matches ModeLine's L/C convention
            const std::size_t padding = gutterDigits > number.size() ? gutterDigits - number.size() : 0;
            // Leading gap (status/line-number-spacing follow-up -- the
            // line-number gutter now gets breathing room on BOTH sides,
            // not just the trailing gap it already had). Sits right after
            // the diagnostic column now (LSP client follow-up), not
            // directly after the status column -- digitsStart itself
            // already accounts for kDiagnosticWidth, so this is just
            // "one column before digitsStart."
            if (static_cast<int>(digitsStart - kLineNumberGap) < c.size().width) {
                ftxui::Cell& cell = c[{.x = static_cast<int>(digitsStart - kLineNumberGap), .y = row}];
                cell.character    = " ";
                gutterGapBrush.ApplyTo(cell);
            }
            for (std::size_t i = 0; i < padding && static_cast<int>(digitsStart + i) < c.size().width; ++i) {
                ftxui::Cell& cell = c[{.x = static_cast<int>(digitsStart + i), .y = row}];
                cell.character    = " ";
                gutterBrush.ApplyTo(cell);
            }
            for (std::size_t i = 0; i < number.size() && static_cast<int>(digitsStart + padding + i) < c.size().width; ++i) {
                ftxui::Cell& cell = c[{.x = static_cast<int>(digitsStart + padding + i), .y = row}];
                cell.character    = std::string(1, number[i]);
                gutterBrush.ApplyTo(cell);
            }
            if (static_cast<int>(digitsStart + gutterDigits) < c.size().width) {
                ftxui::Cell& cell = c[{.x = static_cast<int>(digitsStart + gutterDigits), .y = row}];
                cell.character    = " ";
                gutterGapBrush.ApplyTo(cell);
            }

            // depth-aware-fold-gutter follow-up: one column per nesting
            // level (capped at kMaxFoldDepthColumns) -- a block's OWN
            // header row shows its ⊞/⊟ toggle at its own column (⊟, real
            // Org's own "there's more, click to open" shape inverted --
            // classic outline-widget convention: minus means "already
            // open, click to close" -- when expanded; ⊞, "click to expand,"
            // when collapsed, buffer.FoldMarkerAt has an entry for it, cell
            // rendered inverted matching ScrollBar's own solid-thumb
            // convention so it visually pops). Every other row a block's
            // own EXPANDED span covers gets a guide line ('│', or '└' --
            // reusing ProjectSidebar's own box-drawing connector glyph
            // rather than inventing new Unicode -- on the span's own last
            // row) at that block's column, tracing where it closes; a
            // COLLAPSED block gets no line at all, only its header ⊞ --
            // there's nothing to trace while its body is hidden (an
            // explicit user choice, not an oversight).
            //
            // foldGutterHeaderAtColumn_/foldColumnOpenEnds_/foldColumnCursor_
            // (declared just above the row loop) turn this into a single
            // linear streaming pass over foldGutterEntries_/
            // foldGutterLineRangesByColumn_ across the WHOLE row loop --
            // amortized O(blocks in viewport), not a fresh per-row scan or
            // binary search -- correct because same-column ranges from a
            // real syntax tree are always either disjoint or properly
            // nested (a laminar family), so a plain per-column stack,
            // advanced as `line` monotonically increases row by row, is
            // exactly the right structure: an ancestor's own range can
            // never close before a still-open descendant mapped to the
            // same (capped) column does.
            if (foldColumnWidth > 0) {
                foldGutterHeaderAtColumn.fill(nullptr);
                // <= line, not == line: when topLine_ > 0 (scrolled past
                // any blocks whose header sits earlier in the file), those
                // earlier entries must still be consumed here to advance
                // the cursor past them -- an exact-match-only condition
                // left the cursor permanently stuck on the first entry
                // whose headerLine falls before topLine_, silently
                // suppressing every ⊞/⊟ glyph for the rest of the buffer
                // (a real, reported bug: scrolling past ~line 50 in a file
                // with earlier foldable blocks stopped drawing them at
                // all). Only an exact match actually gets recorded for
                // rendering; anything strictly earlier is skipped, not
                // rendered, matching mid-scroll-start behavior
                // foldColumnCursor's own analogous `<=` loop below already
                // got right the first time.
                while (foldGutterEntryCursor < foldGutterEntries_.size() &&
                       foldGutterEntries_[foldGutterEntryCursor].headerLine <= line) {
                    const auto& entry = foldGutterEntries_[foldGutterEntryCursor];
                    if (entry.headerLine == line) {
                        foldGutterHeaderAtColumn[entry.column] = &entry;
                    }
                    ++foldGutterEntryCursor;
                }

                for (int col = 0; col < kMaxFoldDepthColumns; ++col) {
                    auto&       cursor   = foldColumnCursor[col];
                    auto&       openEnds = foldColumnOpenEnds[col];
                    const auto& ranges   = foldGutterLineRangesByColumn_[col];
                    while (cursor < ranges.size() && ranges[cursor].first <= line) {
                        openEnds.push_back(ranges[cursor].second);
                        ++cursor;
                    }
                    while (!openEnds.empty() && openEnds.back() <= line) {
                        openEnds.pop_back();
                    }

                    const int screenCol = static_cast<int>(foldStart) + col;
                    if (screenCol >= c.size().width) {
                        continue;
                    }
                    char32_t glyph    = U' ';
                    bool     inverted = false;
                    if (const FoldGutterEntry* header = foldGutterHeaderAtColumn[col]; header != nullptr) {
                        inverted = buffer.FoldMarkerAt(header->blockStart).has_value();
                        glyph    = inverted ? U'⊞' : U'⊟'; // ⊞ collapsed / ⊟ expanded
                    }
                    else if (!openEnds.empty()) {
                        glyph = (openEnds.back() - 1 == line) ? U'└' : U'│'; // closing row / mid-span
                    }
                    ftxui::Cell& cell = c[{.x = screenCol, .y = row}];
                    cell.character    = text::EncodeCodepointUtf8(glyph);
                    gutterBrush.ApplyTo(cell);
                    cell.inverted = inverted;
                }
            }

            const std::vector<editor::HighlightSpan> lineSpans = SpansForLine(highlightSpans, lineStart, lineEnd);
            const std::vector<RenderedLink>          lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);

            std::size_t offset = lineStart;
            int         col    = static_cast<int>(gutterWidth);
            while (offset < lineEnd && col < c.size().width) {
                if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                    // Links follow-up: real Org's own "descriptive links" --
                    // the raw "[[target][description]]" markup collapses down
                    // to just its own displayText on screen, whole-hog (never
                    // truncated mid-glyph the way an ordinary too-wide line
                    // gets clipped at the viewport edge -- Org links are
                    // short enough in practice that this isn't worth the
                    // extra bookkeeping a partial-clip would need). Tab/
                    // control-byte glyphs within displayText (a realistic
                    // edge case, not assumed impossible) still go through the
                    // same expand-or-hex-placeholder treatment as ordinary
                    // buffer text -- CodepointColumns/DisplayColumns already
                    // account for their wider column cost, so the actual
                    // glyphs written here have to match or the two would
                    // silently disagree about layout.
                    const Brush      linkBrush{.background = theme_.background, .foreground = theme_.linkForeground, .bold = true};
                    std::size_t      textOffset = 0;
                    const text::Rope displayRope(link->displayText);
                    while (textOffset < displayRope.ByteLength() && col < c.size().width) {
                        const auto glyph = displayRope.CodepointAt(textOffset);
                        if (glyph.codepoint == U'\t') {
                            const int tabWidth = editor::TabWidth();
                            for (int i = 0; i < tabWidth && col < c.size().width; ++i) {
                                ftxui::Cell& cell = c[{.x = col, .y = row}];
                                cell.character    = " ";
                                linkBrush.ApplyTo(cell);
                                ++col;
                            }
                        }
                        else if (IsUnprintableControl(glyph.codepoint)) {
                            const char32_t glyphs[4] = {kBinaryOpen, HexDigit((glyph.codepoint >> 4) & 0xF),
                                                        HexDigit(glyph.codepoint & 0xF), kBinaryClose};
                            for (const char32_t hexGlyph : glyphs) {
                                if (col >= c.size().width)
                                    break;
                                ftxui::Cell& cell = c[{.x = col, .y = row}];
                                cell.character    = text::EncodeCodepointUtf8(hexGlyph);
                                linkBrush.ApplyTo(cell);
                                ++col;
                            }
                        }
                        else {
                            ftxui::Cell& cell = c[{.x = col, .y = row}];
                            cell.character    = text::EncodeCodepointUtf8(glyph.codepoint);
                            linkBrush.ApplyTo(cell);
                            ++col;
                        }
                        textOffset += glyph.byteLength;
                    }
                    offset = link->endByte;
                    continue;
                }

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

            // Org-mode fold/unfold follow-up: any marked headline (Collapsed or
            // ChildrenVisible -- either way, something below this line is
            // currently hidden) gets a short ellipsis painted right after its
            // own content, real Org's own visual cue that there's more here
            // than what's shown. Reuses theme_.lineNumberForeground rather than
            // a new dedicated Theme field -- deliberately minimal, a distinct
            // color is an easy follow-up if it turns out to matter in practice.
            // generic-code-folding follow-up: was `buffer.FoldMarkerAt(lineStart).has_value()`
            // -- correct for Org, whose marker key always IS the headline
            // line's own start byte, but not for a code fold, whose marker
            // key is a foldable block's own startByte (e.g. a function's
            // "{"), which sits partway through its header line, not at
            // column 0. Checking hiddenLineRanges_ for an entry starting
            // right after this line is the one condition both fold sources
            // agree on (see FoldedLineRanges' own [startLine+1, endLine+1)
            // convention, shared by org:: and codefold:: alike), so this is
            // the generic trigger both the ellipsis and the preview below
            // key off, rather than a marker lookup at all.
            EnsureHiddenLineRangesCache();
            for (const auto& [hiddenStart, hiddenEnd] : hiddenLineRanges_) {
                if (hiddenStart != line + 1 || hiddenEnd == hiddenStart) {
                    continue;
                }
                const Brush foldBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                for (const char32_t glyph : {U' ', kFoldEllipsis}) {
                    if (col >= c.size().width)
                        break;
                    ftxui::Cell& cell = c[{.x = col, .y = row}];
                    cell.character    = text::EncodeCodepointUtf8(glyph);
                    foldBrush.ApplyTo(cell);
                    ++col;
                }

                // A short, dim preview of the folded region's own last line
                // (e.g. a closing "}" or "};") right after the ellipsis, so
                // collapsing a block doesn't fully erase what its closing
                // line looked like -- every folded line is hidden from
                // rendering by definition, so this preview is the only way
                // that line's content ever reaches the screen while the
                // fold is closed. Leading whitespace is skipped (this is a
                // preview snippet, not a faithful column-accurate render,
                // so the closing line's own indentation would just waste
                // columns).
                const std::size_t lastHiddenLine = hiddenEnd - 1;
                std::size_t       previewOffset  = content.LineToByteOffset(lastHiddenLine);
                const std::size_t previewEnd     = (lastHiddenLine + 1 < totalLines)
                                                       ? content.LineToByteOffset(lastHiddenLine + 1) - 1
                                                       : content.ByteLength();
                while (previewOffset < previewEnd) {
                    const auto decoded = content.CodepointAt(previewOffset);
                    if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                        break;
                    }
                    previewOffset += decoded.byteLength;
                }
                if (previewOffset >= previewEnd || col >= c.size().width) {
                    break;
                }
                const Brush previewBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                {
                    ftxui::Cell& spaceCell = c[{.x = col, .y = row}];
                    spaceCell.character    = " ";
                    previewBrush.ApplyTo(spaceCell);
                    ++col;
                }
                while (previewOffset < previewEnd && col < c.size().width) {
                    const auto   decoded = content.CodepointAt(previewOffset);
                    ftxui::Cell& cell    = c[{.x = col, .y = row}];
                    cell.character       = text::EncodeCodepointUtf8(decoded.codepoint);
                    previewBrush.ApplyTo(cell);
                    ++col;
                    previewOffset += decoded.byteLength;
                }
                break;
            }
        }

        line = NextVisibleLine(line + 1, renderEndLine);
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

    // Org-mode fold/unfold follow-up: a point sitting on a currently-hidden
    // line has no on-screen row to report at all -- can't happen through
    // org-cycle itself (see CycleFoldAtPoint's own doc comment), but stays
    // a real, harmless "no cursor this frame" rather than an invalid
    // position if some other path ever moves point into a hidden region.
    if (pointLine < topLine_ || IsLineHidden(pointLine)) {
        return std::nullopt;
    }
    const std::size_t visibleRow = VisibleLineCountBetween(topLine_, pointLine);

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
    if (sizeIsKnown && visibleRow >= static_cast<std::size_t>(sizeNow.height)) {
        return std::nullopt;
    }

    const std::size_t lineStart = content.LineToByteOffset(pointLine);
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();
    const int maxColumns = sizeIsKnown ? sizeNow.width - static_cast<int>(gutterWidth) : std::numeric_limits<int>::max();

    // Links follow-up: point's own line never has a link collapsed AT
    // point's own position (LinksForLine excludes any link containing
    // point), so this always agrees with what Paint() actually drew for
    // this specific row.
    EnsureLinkCache();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);

    const std::optional<int> visualCol = VisualColumn(content, lineStart, point, maxColumns, lineLinks);
    if (!visualCol) {
        return std::nullopt;
    }

    const std::size_t col = gutterWidth + static_cast<std::size_t>(*visualCol);
    if (sizeIsKnown && col >= static_cast<std::size_t>(sizeNow.width)) {
        return std::nullopt; // scrolled off horizontally; no horizontal scroll in v1
    }
    return Point{.x = static_cast<int>(col), .y = static_cast<int>(visibleRow)};
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
        inputMode_ == InputMode::FindScratch || inputMode_ == InputMode::StringRectangle ||
        inputMode_ == InputMode::SetHeadlineTags) {
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
    if (inputMode_ == InputMode::ProjectFindFile) {
        // Unlike ExecuteCommand, Enter here just opens a file directly
        // (BufferList::OpenOrCreateFile + activeBuffer_.Set()) rather than
        // routing through RunCommandAndHandleOutcome, so the ordinary
        // after-the-fact ClampPointToNarrowing() every other prompt-shaped
        // mode uses is correct here too.
        HandleProjectFindFileKey(*chord);
        ClampPointToNarrowing();
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
    RunCommandAndHandleOutcome(context, [&] { return dispatcher_.Feed(*chord, context) == editor::Dispatcher::Outcome::Invoked; });
    return true;
}

void BufferView::RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<bool()>& invoke) {
    bool ran = false;
    try {
        ran = invoke();
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
        ran            = true; // a command did run, it just threw -- still "something happened"
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

    // structural-selection-expansion follow-up: any dispatched command other
    // than expand-selection/shrink-selection themselves invalidates the
    // expansion-history stack -- this is the one choke point every dispatch
    // (typing, arrow motion, everything) passes through, so it's what
    // catches ordinary editing/motion, which never touches
    // interactiveRequest at all (stays InteractiveRequest::None). A
    // command-driven buffer switch (find-file, switch-to-buffer, ...) is
    // covered here too; a non-command-driven one (a TabBar/ProjectSidebar
    // mouse click) is instead caught by ExpandSelection/ShrinkSelection's own
    // buffer-identity staleness check in StartInteractiveSession.
    if (ran && context.interactiveRequest != editor::InteractiveRequest::ExpandSelection &&
        context.interactiveRequest != editor::InteractiveRequest::ShrinkSelection) {
        expansionHistory_.clear();
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
        RunCommandAndHandleOutcome(context, [&] { return dispatcher_.Feed(chord, context) == editor::Dispatcher::Outcome::Invoked; });

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
        case editor::InteractiveRequest::ProjectAgenda:
            BuildResultsBuffer(editor::CollectProjectTodos(editor::ProjectRoot()), "*agenda*");
            return;
        case editor::InteractiveRequest::KillBuffer:
            RequestCloseBuffer(activeBuffer_.Get());
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
        // org-set-tags follow-up: org-set-tags already checked
        // HeadlineAtPoint before setting this request, but point can't
        // have moved since then (no other command runs between a
        // command's own dispatch and StartInteractiveSession) -- resolving
        // it again here, rather than threading it through
        // InteractiveRequest somehow, is what lets the prompt pre-fill
        // with the headline's *current* tags without adding any new
        // payload to CommandContext.
        case editor::InteractiveRequest::SetHeadlineTags: {
            inputMode_          = InputMode::SetHeadlineTags;
            const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get());
            prompt_.emplace("Tags (colon-separated): ");
            if (headline && !headline->tags.empty()) {
                std::string joined;
                for (const std::string& tag : headline->tags) {
                    if (!joined.empty())
                        joined += ':';
                    joined += tag;
                }
                prompt_->SetText(joined);
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
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
        // project-find-file follow-up: same "populate and show the full
        // candidate list right away" shape as ExecuteCommand just above,
        // but the candidate list is a real recursive directory walk
        // (editor::BuildProjectTree), not a free in-memory lookup -- done
        // once here, up front, rather than per keystroke (see
        // projectFindFileCandidates_'s own doc comment in BufferView.h). A
        // project with no files at all is a degenerate case with nothing to
        // pick from, so it's reported directly rather than opening a prompt
        // session over an empty list.
        case editor::InteractiveRequest::ProjectFindFile: {
            projectFindFileCandidates_.clear();
            const std::filesystem::path root = editor::ProjectRoot();
            for (const editor::ProjectTreeEntry& entry : editor::BuildProjectTree(root)) {
                if (!entry.isDirectory) {
                    projectFindFileCandidates_.push_back(std::filesystem::relative(entry.path, root).generic_string());
                }
            }
            if (projectFindFileCandidates_.empty()) {
                statusMessage_ = "No files found under " + root.string();
                return;
            }
            inputMode_ = InputMode::ProjectFindFile;
            prompt_.emplace("Find file (fuzzy): ");
            projectFindFileSelection_ = 0;
            RefreshProjectFindFileStatus();
            return;
        }
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
        // structural-selection-expansion follow-up: one-shot direct actions,
        // same shape as NarrowToRegion/ToggleProjectSidebar above.
        case editor::InteractiveRequest::ExpandSelection: {
            if (!mode_.expandSelection) {
                statusMessage_ = "No structural selection support in this mode.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (expansionHistoryBuffer_ != &buffer || expansionHistoryGeneration_ != buffer.ContentGeneration()) {
                expansionHistory_.clear();
            }
            const auto [startByte, endByte]                                   = buffer.HasMark() ? buffer.Region() : std::pair{buffer.Point(), buffer.Point()};
            const std::optional<std::pair<std::size_t, std::size_t>> expanded = mode_.expandSelection(buffer.Text(), startByte, endByte);
            if (!expanded) {
                statusMessage_ = "Already at outermost node.";
                return;
            }
            expansionHistory_.emplace_back(startByte, endByte);
            buffer.SetMark(expanded->first);
            buffer.SetPoint(expanded->second);
            expansionHistoryBuffer_     = &buffer;
            expansionHistoryGeneration_ = buffer.ContentGeneration();
            statusMessage_.clear();
            return;
        }
        case editor::InteractiveRequest::ShrinkSelection: {
            text::Buffer& buffer = activeBuffer_.Get();
            const bool    stale =
                expansionHistoryBuffer_ != &buffer || expansionHistoryGeneration_ != buffer.ContentGeneration() || expansionHistory_.empty();
            if (stale) {
                statusMessage_ = "No selection to shrink to.";
                return;
            }
            const auto [startByte, endByte] = expansionHistory_.back();
            expansionHistory_.pop_back();
            buffer.SetMark(startByte);
            buffer.SetPoint(endByte);
            expansionHistoryGeneration_ = buffer.ContentGeneration();
            statusMessage_.clear();
            return;
        }
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
        // Links follow-up: a one-shot direct action, same shape as
        // VisitSearchResult -- doesn't touch inputMode_.
        case editor::InteractiveRequest::OpenLinkAtPoint:
            OpenLinkAtPoint();
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
    executeCommandSelection_  = 0;
    projectFindFileSelection_ = 0;
    projectFindFileCandidates_.clear(); // cached only for the duration of one session -- see its own doc comment in BufferView.h
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
        else if (inputMode_ == InputMode::SetHeadlineTags) {
            // Re-resolved fresh (matches StartInteractiveSession's own
            // comment on why this is safe) rather than trusting a value
            // captured when the prompt opened -- point hasn't moved, so
            // this always finds the same headline.
            if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
                std::vector<std::string> newTags;
                std::string              current;
                for (const char ch : input) {
                    if (ch == ':') {
                        if (!current.empty()) {
                            newTags.push_back(current);
                            current.clear();
                        }
                    }
                    else {
                        current.push_back(ch);
                    }
                }
                if (!current.empty())
                    newTags.push_back(current);
                editor::org::SetHeadlineTags(activeBuffer_.Get(), *headline, newTags);
            }
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
        inputMode_ != InputMode::CreateDirectory && inputMode_ != InputMode::StringRectangle &&
        inputMode_ != InputMode::SetHeadlineTags) {
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

void BufferView::OpenLinkAtPoint() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (mode_.name == "org-mode") {
        if (const auto orgLink = editor::org::LinkAtPoint(buffer)) {
            if (!orgLink->target.empty() && orgLink->target.front() == '*') {
                // Real Org's own internal-link form: "[[*Some Headline]]".
                // "#custom-id" targets are explicitly out of scope -- see
                // Org.h's own FindHeadlineByTitle doc comment.
                const std::string title = orgLink->target.substr(1);
                if (const auto lineStartByte = editor::org::FindHeadlineByTitle(buffer.Text(), title)) {
                    buffer.ClearMark();
                    buffer.SetPoint(*lineStartByte);
                    statusMessage_.clear();
                    ScrollToShowPoint();
                }
                else {
                    statusMessage_ = "Link target not found: " + title;
                }
                return;
            }
            // Not an internal link -- an Org link to a URL or a file path is
            // still just a URL or a file path once its brackets are
            // stripped away, so this reuses the exact same open/report tail
            // the generic (non-Org) path below uses.
            OpenDetectedLink(editor::link::DetectedLink{
                .kind      = editor::link::ClassifyTarget(orgLink->target),
                .target    = orgLink->target,
                .startByte = orgLink->startByte,
                .endByte   = orgLink->endByte,
            });
            return;
        }
        // Point isn't on a bracket link -- fall through to the same generic
        // bare-URL/file detection every other mode uses, the same
        // "org-specific first, generic fallback" chain org-cycle's own body
        // already established for fold-cycle -> table-align.
    }

    const auto detected = editor::link::DetectLinkAtPoint(buffer.Text(), buffer.Point());
    if (!detected) {
        statusMessage_ = "No link at point.";
        return;
    }
    OpenDetectedLink(*detected);
}

void BufferView::OpenDetectedLink(const editor::link::DetectedLink& detected) {
    if (detected.kind == editor::link::LinkKind::Url) {
        if (editor::link::OpenUrl(detected.target)) {
            statusMessage_ = "Opening " + detected.target;
        }
        else {
            statusMessage_ = "No URL-open command configured (ned/set-url-open-command).";
        }
        return;
    }

    text::Buffer&               buffer = activeBuffer_.Get();
    const std::filesystem::path baseDirectory =
        buffer.Path() ? buffer.Path()->parent_path() : editor::ProjectRoot();
    const auto resolved = editor::link::ResolveFileLink(detected.target, baseDirectory);
    if (!resolved) {
        statusMessage_ = "No such file: " + detected.target;
        return;
    }

    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(*resolved);
        activeBuffer_.Set(opened);
        statusMessage_.clear();
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
    // Org-mode fold/unfold follow-up: was topLine_ + at.y, a flat 1:1
    // mapping -- a click on screen row N means the N-th *visible* buffer
    // line below topLine_, not literally topLine_ + N, whenever a fold is
    // hiding lines above the click.
    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::Rope&   content     = buffer.Content();
    const std::size_t   totalLines  = content.LineCount();
    const std::size_t   line        = std::min(AdvanceVisibleLines(topLine_, static_cast<std::size_t>(std::max(at.y, 0)), totalLines),
                                               totalLines - 1); // mirrors Buffer::ByteOffsetForLineAndColumn's own clamp
    const std::size_t   x           = static_cast<std::size_t>(std::max(at.x, 0));
    const std::size_t   gutterWidth = GutterWidth();
    // A click inside the gutter itself lands on that line's first column,
    // same as clicking right at the start of the line's text.
    const std::size_t column = (x > gutterWidth) ? x - gutterWidth : 0;

    // Links follow-up: was a direct Buffer::ByteOffsetForLineAndColumn call
    // -- that method must stay entirely link-oblivious (Buffer has zero
    // Org-specific knowledge), so this reimplements its same tab-aware walk
    // locally, link-aware, via ByteOffsetForColumnInLine (see its own doc
    // comment above for why it's safe to call unconditionally here even in
    // a non-Org buffer). lineLinks is built against buffer.Point() -- the
    // buffer's point *before* this click resolves -- so it excludes exactly
    // the links Paint() left uncollapsed the last time this row was
    // actually drawn.
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    EnsureLinkCache();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());
    return ByteOffsetForColumnInLine(content, lineStart, lineEnd, column, editor::TabWidth(), lineLinks);
}

std::size_t BufferView::GutterWidth() const {
    const std::size_t totalLines = activeBuffer_.Get().Content().LineCount();
    // status/line-number-spacing follow-up (LSP client follow-up: gained a
    // second, dedicated diagnostic column -- see kDiagnosticWidth's own doc
    // comment): [status][diagnostic][gap][digits][gap][fold], left to right
    // -- status and diagnostic are always reserved; the fold region
    // (generic-code-folding / depth-aware-fold-gutter follow-ups) only when
    // a mode has a real fold query and the feature is enabled, a fixed
    // kMaxFoldDepthColumns-wide reservation (not one that grows with how
    // deep the currently-visible content happens to nest -- an explicit
    // user choice, so the gutter's own width never jumps around while
    // scrolling past a deeply nested region).
    const std::size_t foldColumn = (mode_.fold && editor::CodeFoldingEnabled()) ? kMaxFoldDepthColumns : 0;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + std::to_string(totalLines).size() + kLineNumberGap + foldColumn;
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

        // depth-aware-fold-gutter follow-up: a click inside the reserved
        // fold-depth region (see GutterWidth()/Paint()'s own doc comments)
        // toggles the fold at that row/column instead of placing point --
        // checked first, ahead of the generic point-placement path below,
        // the same "one specific gutter region wins over the generic click
        // fallthrough" shape a click inside the gutter more broadly already
        // has in ByteOffsetForPoint. The clicked column names which block
        // to toggle directly (a click at column 1 toggles only a depth-1
        // block, not whatever's innermost at that line) -- clicking a plain
        // guide line ('│'/'└', not a header cell) is a no-op, matching how
        // indent guides are inert-to-click in every mainstream editor.
        const std::size_t foldColumnWidth = (mode_.fold && editor::CodeFoldingEnabled()) ? kMaxFoldDepthColumns : 0;
        // Mirrors GutterWidth()/Paint()'s own [status][gap][digits][gap][fold]
        // layout -- foldStart is where the fold region actually starts on
        // screen now that it's no longer the leftmost gutter region.
        const std::size_t foldStart = GutterWidth() - foldColumnWidth;
        if (foldColumnWidth > 0 && mouse->at.x >= static_cast<int>(foldStart) &&
            static_cast<std::size_t>(mouse->at.x) < foldStart + foldColumnWidth) {
            text::Buffer&     buffer        = activeBuffer_.Get();
            const text::Rope& content       = buffer.Content();
            const std::size_t totalLines    = content.LineCount();
            const std::size_t line          = std::min(AdvanceVisibleLines(topLine_, static_cast<std::size_t>(std::max(mouse->at.y, 0)), totalLines),
                                                       totalLines - 1);
            const int         clickedColumn = mouse->at.x - static_cast<int>(foldStart);
            EnsureFoldGutterCache();
            auto it = std::lower_bound(foldGutterEntries_.begin(), foldGutterEntries_.end(), line,
                                       [](const FoldGutterEntry& entry, std::size_t targetLine) { return entry.headerLine < targetLine; });
            for (; it != foldGutterEntries_.end() && it->headerLine == line; ++it) {
                if (it->column == clickedColumn) {
                    const bool collapsed = buffer.FoldMarkerAt(it->blockStart).has_value();
                    buffer.SetFoldMarker(it->blockStart,
                                         collapsed ? std::nullopt : std::optional(text::Buffer::FoldMarker::Collapsed));
                    break;
                }
            }
            return true;
        }

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

std::size_t BufferView::AvailableCandidateColumns(std::size_t prefixLength) const {
    const std::size_t     totalWidth    = size().width > 0 ? static_cast<std::size_t>(size().width) : 80;
    constexpr std::size_t kClosingBrace = 1;
    return totalWidth > prefixLength + kClosingBrace ? totalWidth - prefixLength - kClosingBrace : 1;
}

void BufferView::RefreshExecuteCommandStatus() {
    const std::vector<std::string> ranked =
        editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());
    executeCommandSelection_ = ranked.empty() ? 0 : std::min(executeCommandSelection_, ranked.size() - 1);

    if (ranked.empty()) {
        statusMessage_ = prompt_->StatusText();
        return;
    }
    const std::string prefix  = prompt_->StatusText() + "  {";
    const std::size_t columns = AvailableCandidateColumns(prefix.size());
    statusMessage_            = prefix + FormatFuzzyCandidates(ranked, executeCommandSelection_, columns) + "}";
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
        RunCommandAndHandleOutcome(context, [&] {
            dispatcher_.Registry().Invoke(name, context);
            return true; // Invoke() always runs the command directly -- no Pending concept here
        });
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

void BufferView::RefreshProjectFindFileStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(projectFindFileCandidates_, prompt_->Text());
    projectFindFileSelection_             = ranked.empty() ? 0 : std::min(projectFindFileSelection_, ranked.size() - 1);

    if (ranked.empty()) {
        statusMessage_ = prompt_->StatusText();
        return;
    }
    const std::string prefix  = prompt_->StatusText() + "  {";
    const std::size_t columns = AvailableCandidateColumns(prefix.size());
    statusMessage_            = prefix + FormatFuzzyCandidates(ranked, projectFindFileSelection_, columns) + "}";
}

void BufferView::HandleProjectFindFileKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(projectFindFileCandidates_, prompt_->Text());

        if (ranked.empty()) {
            statusMessage_ = "No file matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::filesystem::path selected = ranked[std::min(projectFindFileSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        const std::filesystem::path absolutePath = editor::ProjectRoot() / selected;
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(absolutePath);
            activeBuffer_.Set(opened);
            statusMessage_ = "Opened " + opened.Name();
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_.clear();
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(projectFindFileCandidates_, prompt_->Text());
        if (!ranked.empty() && projectFindFileSelection_ + 1 < ranked.size()) {
            ++projectFindFileSelection_;
        }
        RefreshProjectFindFileStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        if (projectFindFileSelection_ > 0) {
            --projectFindFileSelection_;
        }
        RefreshProjectFindFileStatus();
        return;
    }

    // Same "typing re-snaps to the top match" reasoning as
    // HandleExecuteCommandKey above -- see that method's own comment.
    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteChar();
        projectFindFileSelection_ = 0;
        RefreshProjectFindFileStatus();
        return;
    }
    if (IsPlainCharacter(chord)) {
        prompt_->AppendChar(chord.Codepoint);
        projectFindFileSelection_ = 0;
        RefreshProjectFindFileStatus();
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

void BufferView::EnsureTopLineValidForActiveBuffer() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (topLineValidatedBuffer_ == &buffer) {
        return;
    }
    topLineValidatedBuffer_ = &buffer;
    // ScrollToShowPoint() alone (no need to reset topLine_ to 0 first) is
    // already safe against topLine_ being an arbitrary leftover value from
    // whichever buffer was active before: its own "point is above topLine_"
    // branch fires unconditionally whenever pointLine < topLine_, which is
    // exactly what happens when topLine_ was left pointing well past the
    // newly active (and possibly much shorter) buffer's own last line --
    // this buffer's own pointLine is always a real, in-range line, so it's
    // always < an out-of-range topLine_. The one thing this deliberately
    // preserves rather than discarding: if topLine_ happens to already show
    // the new buffer's own point (e.g. switching between two similarly
    // long buffers), it's left exactly where it was instead of always
    // jumping back to the top.
    ScrollToShowPoint();
}

void BufferView::ScrollToShowPoint() {
    const text::Rope& content   = activeBuffer_.Get().Content();
    const std::size_t pointLine = content.ByteOffsetToLine(activeBuffer_.Get().Point());

    if (pointLine < topLine_) {
        topLine_ = pointLine;
    }
    else if (size().height > 0) {
        const auto visibleLines = static_cast<std::size_t>(size().height);
        // Org-mode fold/unfold follow-up: was `pointLine >= topLine_ +
        // visibleLines` / `topLine_ = pointLine - visibleLines + 1`, raw
        // buffer-line arithmetic that assumed every line between topLine_
        // and pointLine renders as its own row. VisibleLineCountBetween is
        // the fold-aware "how many rows would that actually take" query;
        // the backward walk below is its inverse ("where would topLine_
        // have to be so pointLine lands exactly visibleLines - 1 visible
        // rows below it").
        if (VisibleLineCountBetween(topLine_, pointLine) >= visibleLines) {
            std::size_t newTop    = pointLine;
            std::size_t remaining = visibleLines - 1;
            while (remaining > 0 && newTop > 0) {
                --newTop;
                if (!IsLineHidden(newTop))
                    --remaining;
            }
            topLine_ = newTop; // guaranteed visible: either line 0 (never hidden -- hiddenStart is always >= 1), or the line that just made remaining hit 0
        }
    }
}

std::size_t BufferView::TopLine() const {
    return topLine_;
}

void BufferView::SetTopLine(std::size_t line) {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    // Org-mode fold/unfold follow-up: topLine_ must always land on a
    // visible line (every other fold-aware query here assumes that), so a
    // target sitting inside a hidden range rounds forward to the next
    // visible one before the usual clamp.
    line     = NextVisibleLine(std::max(line, rangeStart), rangeEnd);
    topLine_ = std::clamp(line, rangeStart, MaxTopLine());
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
    const auto visibleLines           = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    // Org-mode fold/unfold follow-up: was `rangeStart + (totalLines >
    // visibleLines ? totalLines - visibleLines : 0)`, plain buffer-line
    // subtraction. VisibleLineCountBetween/the backward walk below are the
    // fold-aware equivalents of "how much content is there" and "where do
    // I have to start to leave exactly one viewport of it visible."
    if (VisibleLineCountBetween(rangeStart, rangeEnd) <= visibleLines) {
        return rangeStart;
    }
    std::size_t newTop    = rangeEnd;
    std::size_t remaining = visibleLines;
    while (remaining > 0 && newTop > rangeStart) {
        --newTop;
        if (!IsLineHidden(newTop))
            --remaining;
    }
    return newTop;
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

void BufferView::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
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
