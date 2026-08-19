#include "BufferView.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <utility>
#include <vector>

#include "EchoArea.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/FuzzyMatch.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Org.h"
#include "Editor/ProjectAgenda.h"
#include "Editor/ProjectFileOps.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/ProjectTree.h"
#include "Editor/Rectangle.h"
#include "Editor/ScratchPad.h"
#include "Editor/TabWidth.h"
#include "Editor/WrapOverrides.h"
#include "KeyTranslation.h"
#include "Text/BinaryDetect.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // large-file-async-load follow-up: past this size, never run mode_.highlight
    // at all, regardless of whether the file's extension happens to match a
    // real tree-sitter grammar. buffer.ReadOnly() already suppresses
    // highlighting for a synthesized results buffer and (now) for a buffer
    // that's still IsLoading() -- but a huge file that finishes loading and
    // reverts to writable would otherwise still pay for a full buffer.Text()
    // copy plus a whole-buffer tree-sitter parse on every edit. 8 MiB is
    // generous for any real source file and well short of "expensive."
    constexpr std::size_t kMaxHighlightBytes = 8 * 1024 * 1024;

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

    // line-truncation-indicator follow-up: overwrites a clipped line's own
    // last column when wrap is off and the line is too long for the
    // viewport -- see the render loop's own use below.
    constexpr char32_t kTruncationIndicator = U'»';

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

    // line-wrap follow-up. A word-break boundary this codebase treats as
    // breakable -- ASCII space/tab only, matching MoveForwardWord/
    // MoveBackwardWord's own already-established "not Unicode-aware,
    // deliberate v1 scope cut" precedent (Buffer.h), and the same informal
    // whitespace definition the fold-ellipsis trailing-space trim just below
    // in this file already uses.
    bool IsWrapBreakWhitespace(char32_t cp) {
        return cp == U' ' || cp == U'\t';
    }

    // [startByte, endByte) content range one wrapped canvas row draws --
    // always at least one per line, even an empty one.
    struct WrapSegment {
        std::size_t startByte;
        std::size_t endByte;
    };

    // line-wrap follow-up. Splits [lineStart, lineEnd) into one or more
    // word-break-aware segments, none exceeding wrapWidth columns. Breaks
    // at the most recent whitespace run's own end when one exists within
    // the current segment; otherwise hard-breaks immediately before the
    // oversized unit (a single token wider than the whole viewport, e.g. a
    // long URL, still must make progress -- forced onto its own segment
    // rather than looping forever). A RenderedLink span is treated as one
    // atomic, unbreakable unit, the same way Paint()'s render loop and
    // VisualColumn already do via LinkStartingAt -- never split mid-link.
    // Trailing whitespace at a break point is included in the ending
    // segment rather than trimmed out of it -- functionally invisible
    // either way, since Paint() washes every row's background blank before
    // drawing, so a trailing space cell looks identical whether "drawn" or
    // simply never reached.
    //
    // Not cached beyond a single call -- same "recompute fresh, it's cheap
    // for one line" precedent VisualColumn/ByteOffsetForColumnInLine already
    // establish; called only for the handful of lines actually on screen or
    // containing point, never for the whole buffer (RowsForLine's own cache
    // in BufferView.h is what avoids re-running this for the entire buffer
    // on every Paint()).
    std::vector<WrapSegment> ComputeWrapSegments(const text::Rope& content, std::size_t lineStart, std::size_t lineEnd,
                                                 int wrapWidth, const std::vector<RenderedLink>& lineLinks) {
        wrapWidth = std::max(wrapWidth, 1);

        std::vector<WrapSegment>   segments;
        std::size_t                segmentStart = lineStart;
        std::size_t                offset       = lineStart;
        int                        col          = 0;
        std::optional<std::size_t> breakByte;    // byte offset just past the latest whitespace run since segmentStart
        int                        breakCol = 0; // col value at that same point

        while (offset < lineEnd) {
            std::size_t unitEnd;
            int         unitWidth;
            bool        isWhitespace = false;
            if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                unitEnd   = link->endByte;
                unitWidth = DisplayColumns(link->displayText);
            }
            else {
                const auto decoded = content.CodepointAt(offset);
                unitEnd            = offset + decoded.byteLength;
                unitWidth          = CodepointColumns(decoded.codepoint);
                isWhitespace       = IsWrapBreakWhitespace(decoded.codepoint);
            }

            if (col > 0 && col + unitWidth > wrapWidth) {
                if (breakByte && *breakByte > segmentStart) {
                    segments.push_back(WrapSegment{.startByte = segmentStart, .endByte = *breakByte});
                    segmentStart = *breakByte;
                    col -= breakCol; // carry over the width already consumed between breakByte and offset
                }
                else {
                    segments.push_back(WrapSegment{.startByte = segmentStart, .endByte = offset});
                    segmentStart = offset;
                    col          = 0;
                }
                breakByte.reset();
                breakCol = 0;
                continue; // retry the same unit against the new segment
            }

            col += unitWidth;
            offset = unitEnd;
            if (isWhitespace) {
                breakByte = offset;
                breakCol  = col;
            }
        }
        segments.push_back(WrapSegment{.startByte = segmentStart, .endByte = lineEnd});
        return segments;
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

    // hover/completion follow-up: byte offset where the ASCII word/
    // identifier token immediately before point begins (an alnum/underscore
    // run) -- shared by the auto-completion suppression heuristic (rejecting
    // a purely numeric token) and ghost-text suffix computation (the
    // already-typed prefix to subtract from a completion item's own
    // insertText). Deliberately ASCII-only (matches Buffer's own word-motion
    // classification), so the returned [start, point) range is guaranteed
    // single-byte-per-codepoint -- safe to treat as raw bytes.
    std::size_t WordPrefixStart(const text::Rope& content, std::size_t point) {
        std::size_t start = point;
        while (start > 0) {
            const std::size_t prior      = content.PreviousCodepointBoundary(start);
            const auto        decoded    = content.CodepointAt(prior);
            const bool        isWordChar = (decoded.codepoint < 0x80) &&
                                           (std::isalnum(static_cast<unsigned char>(decoded.codepoint)) != 0 || decoded.codepoint == U'_');
            if (!isWordChar) {
                break;
            }
            start = prior;
        }
        return start;
    }

    // Tag string for LogMouseEvent, derived from the raw event rather than
    // passed in separately at each call site (was four distinct
    // mouse_press/mouse_move/mouse_release/mouse_wheel overrides, now one
    // unified OnMouseEvent).
    std::string_view MouseEventTag(const MouseEvent& mouse) {
        if (mouse.button == MouseEvent::Button::WheelUp || mouse.button == MouseEvent::Button::WheelDown) {
            return "wheel";
        }
        switch (mouse.motion) {
            case MouseEvent::Motion::Pressed:
                return "press";
            case MouseEvent::Motion::Released:
                return "release";
            case MouseEvent::Motion::Moved:
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
    // Same reasoning as topLineValidatedBuffer_ just above, for
    // onActiveBufferChanged_: the buffer active at construction is already
    // reflected in whatever Mode the owning Pane constructed this
    // BufferView with, so the first Paint() must not re-fire the callback.
    modeSyncBuffer_ = &activeBuffer_.Get();
}

editor::CommandContext BufferView::MakeContext() {
    editor::CommandContext context{activeBuffer_.Get(), killRing_, bufferList_, editor::KeyChord{}, &statusMessage_};
    context.mode       = &mode_;
    context.lspManager = lspManager_;
    context.taskRunner = taskRunner_;
    return context;
}

void BufferView::SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler) {
    onWindowRequest_ = std::move(handler);
}

void BufferView::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void BufferView::SetOnActiveBufferChanged(std::function<void(text::Buffer&)> handler) {
    onActiveBufferChanged_ = std::move(handler);
}

void BufferView::EnsureFoldableBlocksCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!FoldGutterActive()) {
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

void BufferView::EnsureRowCountCache() const {
    text::Buffer&     buffer       = activeBuffer_.Get();
    const bool        wrapEnabled  = EffectiveWrapLines();
    const std::size_t gutterWidth  = GutterWidth();
    const int         contentWidth = std::max(1, size().width - static_cast<int>(gutterWidth));

    if (!wrapEnabled) {
        // Fast path: every buffer with wrap off (the common case) never
        // needs a real per-line row count at all -- RowsForLine's own "1
        // when !wrapEnabled" branch below never even looks at
        // rowCountPerLine_ in that case, so this just keeps the cache keys
        // themselves current without ever calling ComputeWrapSegments.
        rowCountPerLine_.clear();
        rowCountCacheBuffer_            = &buffer;
        rowCountCacheContentGeneration_ = buffer.ContentGeneration();
        rowCountCacheContentWidth_      = contentWidth;
        rowCountCacheWrapEnabled_       = false;
        return;
    }

    if (rowCountCacheBuffer_ == &buffer && rowCountCacheContentGeneration_ == buffer.ContentGeneration() &&
        rowCountCacheContentWidth_ == contentWidth && rowCountCacheWrapEnabled_ == wrapEnabled) {
        return; // still valid -- whatever's already memoized in rowCountPerLine_ (per RowsForLine) stays
    }

    // line-wrap follow-up: only resets the cache's sizing/keys here (a
    // cheap sentinel fill, not real work) -- RowsForLine below is what
    // actually computes and memoizes one line's row count, lazily, the
    // first time that specific line is asked about. An earlier version
    // eagerly computed every line's real word-break scan right here, which
    // a [Performance] test caught as a genuine regression: MaxTopLine()/
    // ScrollToShowPoint() run every Paint() call, so an eager whole-buffer
    // scan here made every single Paint() call on a huge wrap-enabled
    // document pay for the full document's word-break cost up front.
    rowCountPerLine_.assign(buffer.Content().LineCount(), kRowCountUnknown);
    rowCountCacheBuffer_            = &buffer;
    rowCountCacheContentGeneration_ = buffer.ContentGeneration();
    rowCountCacheContentWidth_      = contentWidth;
    rowCountCacheWrapEnabled_       = wrapEnabled;
}

std::size_t BufferView::RowsForLine(std::size_t line) const {
    if (IsLineHidden(line)) {
        return 0;
    }
    if (!EffectiveWrapLines()) {
        return 1;
    }
    EnsureRowCountCache();
    if (line >= rowCountPerLine_.size()) {
        return 1;
    }
    if (rowCountPerLine_[line] == kRowCountUnknown) {
        // line-wrap follow-up: the real, lazy, per-line word-break scan --
        // computed and memoized only for a line actually asked about, never
        // eagerly for the whole buffer (see EnsureRowCountCache's own doc
        // comment for why that distinction is load-bearing, not cosmetic).
        text::Buffer&     buffer    = activeBuffer_.Get();
        const text::Rope& content   = buffer.Content();
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        EnsureLinkCache();
        const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());
        rowCountPerLine_[line] =
            ComputeWrapSegments(content, lineStart, lineEnd, rowCountCacheContentWidth_, lineLinks).size();
    }
    return rowCountPerLine_[line];
}

std::size_t BufferView::VisibleRowCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
    }
    return count;
}

bool BufferView::VisibleRowCountAtLeast(std::size_t startLine, std::size_t endLineExclusive, std::size_t limit) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
        if (count >= limit) {
            return true;
        }
    }
    return false;
}

void BufferView::Paint(Canvas c) {
    EnsureTopLineValidForActiveBuffer();
    EnsureStatusMessageFreshness();

    text::Buffer& buffer = activeBuffer_.Get();
    if (modeSyncBuffer_ != &buffer) {
        modeSyncBuffer_ = &buffer;
        if (onActiveBufferChanged_) {
            onActiveBufferChanged_(buffer);
        }
    }

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
    const std::size_t foldColumnWidth = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
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

    // error-visibility follow-up: a cheap once-per-frame poll, the same
    // "recompute, don't cache" idiom every other Paint()-time check here
    // already uses. Gated on statusMessage_ being empty so this never
    // clobbers an in-progress prompt or another just-set message -- surface
    // via statusMessage_, never forcibly switch the user's buffer out from
    // under them (see BufferView::StartInteractiveSession's LspShowLog case
    // for the actual, user-initiated way to view the log).
    if (lspManager_ && lspManager_->HasUnseenLogEntry() && statusMessage_.empty()) {
        statusMessage_ = "LSP error -- see *lsp log* (M-x lsp-show-log)";
        lspManager_->AcknowledgeLogEntry();
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
    // read-only-buffers follow-up: a synthesized, read-only buffer (project-
    // search results, project-replace's preview, project-agenda) is never
    // real code in whatever language the pane's own Mode happens to be --
    // running that Mode's highlight query against "path:line: text" content
    // would produce meaningless spans, not an empty result, so ReadOnly()
    // suppresses this the same way FoldGutterActive() suppresses folding.
    if (!mode_.highlight || buffer.ReadOnly() || buffer.Size() > kMaxHighlightBytes) {
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
    // line-wrap follow-up: segmentIndex is which wrap segment (row) of
    // `line` is currently being drawn -- 0 for a non-wrapped line, always.
    // lineSegments/currentLineSpans/currentLineLinks are recomputed only
    // when segmentIndex == 0 (i.e. this row starts a new buffer line), then
    // read on every row -- including continuation rows -- of that same
    // line, the same "compute once per line, not once per row" shape this
    // function already used for lineSpans/lineLinks before wrap existed.
    const bool                         wrapActive   = EffectiveWrapLines();
    std::size_t                        segmentIndex = 0;
    std::vector<WrapSegment>           lineSegments;
    std::vector<editor::HighlightSpan> currentLineSpans;
    std::vector<RenderedLink>          currentLineLinks;
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            emptyBrush.ApplyTo(cell);
        }

        if (line < renderEndLine) {
            const std::size_t lineStart = content.LineToByteOffset(line);
            const std::size_t lineEnd =
                (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

            // line-wrap follow-up: recomputed only when this row starts a
            // new buffer line (segmentIndex == 0), then read on every row
            // of that same line, including continuation rows -- the same
            // "compute once per line" shape lineSpans/lineLinks already
            // used before wrap existed, now also covering lineSegments
            // itself. A non-wrapped line always gets exactly one segment
            // spanning its whole content, so every call site below that
            // reads lineSegments[segmentIndex] behaves identically to the
            // pre-wrap code when wrapActive is false.
            if (segmentIndex == 0) {
                currentLineSpans = SpansForLine(highlightSpans, lineStart, lineEnd);
                currentLineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);
                if (wrapActive) {
                    const int wrapWidth = std::max(1, c.size().width - static_cast<int>(gutterWidth));
                    lineSegments        = ComputeWrapSegments(content, lineStart, lineEnd, wrapWidth, currentLineLinks);
                }
                else {
                    lineSegments = {WrapSegment{.startByte = lineStart, .endByte = lineEnd}};
                }
            }
            const WrapSegment& currentSegment = lineSegments[segmentIndex];

            // line-wrap follow-up: everything in this block is per-REAL-LINE,
            // not per-row (a line number/fold glyph only ever belongs on a
            // line's own first row) -- skipped entirely for a continuation
            // row of a wrapped line; the top-of-row blanking pass already
            // washed this row's gutter columns blank.
            if (segmentIndex == 0) {
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
                    const bool  changed        = it != unsavedChangeLineRanges_.end() && it->first <= line;
                    const Color indicatorColor = changed ? theme_.unsavedChangeIndicator : theme_.background;
                    const Brush statusBrush{.background = indicatorColor, .foreground = indicatorColor};
                    Cell&       cell = c[{.x = 0, .y = row}];
                    cell.character   = " ";
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
                    const Brush diagnosticBrush{.background = indicatorColor, .foreground = indicatorColor};
                    Cell&       cell = c[{.x = static_cast<int>(kStatusWidth), .y = row}];
                    cell.character   = " ";
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
                    Cell& cell     = c[{.x = static_cast<int>(digitsStart - kLineNumberGap), .y = row}];
                    cell.character = " ";
                    gutterGapBrush.ApplyTo(cell);
                }
                for (std::size_t i = 0; i < padding && static_cast<int>(digitsStart + i) < c.size().width; ++i) {
                    Cell& cell     = c[{.x = static_cast<int>(digitsStart + i), .y = row}];
                    cell.character = " ";
                    gutterBrush.ApplyTo(cell);
                }
                for (std::size_t i = 0; i < number.size() && static_cast<int>(digitsStart + padding + i) < c.size().width; ++i) {
                    Cell& cell     = c[{.x = static_cast<int>(digitsStart + padding + i), .y = row}];
                    cell.character = std::string(1, number[i]);
                    gutterBrush.ApplyTo(cell);
                }
                if (static_cast<int>(digitsStart + gutterDigits) < c.size().width) {
                    Cell& cell     = c[{.x = static_cast<int>(digitsStart + gutterDigits), .y = row}];
                    cell.character = " ";
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
                        Cell& cell     = c[{.x = screenCol, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        gutterBrush.ApplyTo(cell);
                        cell.inverted = inverted;
                    }
                }
            } // if (segmentIndex == 0) -- line-level gutter rendering

            const std::vector<editor::HighlightSpan>& lineSpans = currentLineSpans;
            const std::vector<RenderedLink>&          lineLinks = currentLineLinks;

            std::size_t offset = currentSegment.startByte;
            // line-wrap follow-up: horizontal-scroll-follow's own
            // fast-forward phase -- consumes (but never draws) whatever
            // falls before leftColumn_, the same width accounting the real
            // drawing loop below uses, so the two can never disagree about
            // where a given column actually lands. leftColumn_ stays 0 for
            // any buffer whose EffectiveWrapLines() is true (see
            // ScrollToShowPointHorizontally's own doc comment), so this is
            // a no-op loop in that case without needing a separate check
            // here.
            if (leftColumn_ > 0) {
                int skipped = 0;
                while (offset < currentSegment.endByte && skipped < static_cast<int>(leftColumn_)) {
                    if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                        skipped += DisplayColumns(link->displayText);
                        offset = link->endByte;
                        continue;
                    }
                    const auto decoded = content.CodepointAt(offset);
                    skipped += CodepointColumns(decoded.codepoint);
                    offset += decoded.byteLength;
                }
            }
            int col = static_cast<int>(gutterWidth);
            while (offset < currentSegment.endByte && col < c.size().width) {
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
                                Cell& cell     = c[{.x = col, .y = row}];
                                cell.character = " ";
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
                                Cell& cell     = c[{.x = col, .y = row}];
                                cell.character = text::EncodeCodepointUtf8(hexGlyph);
                                linkBrush.ApplyTo(cell);
                                ++col;
                            }
                        }
                        else {
                            Cell& cell     = c[{.x = col, .y = row}];
                            cell.character = text::EncodeCodepointUtf8(glyph.codepoint);
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
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = " ";
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
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        binaryBrush.ApplyTo(cell);
                        ++col;
                    }
                }
                else {
                    Cell& cell     = c[{.x = col, .y = row}];
                    cell.character = text::EncodeCodepointUtf8(decoded.codepoint);
                    brush.ApplyTo(cell);
                    ++col;
                }

                offset += decoded.byteLength;
            }

            // line-truncation-indicator follow-up: offset < endByte here
            // means the content loop above stopped because it ran out of
            // viewport width, not because it reached the end of what this
            // row actually has to show -- only reachable with wrap off (a
            // wrapped segment never exceeds the viewport width by
            // construction, see ComputeWrapSegments's own doc comment).
            // Overwrites the row's own last-drawn column rather than
            // reserving a dedicated one, the same "small, unobtrusive
            // marker" approach the fold-ellipsis glyph just below already
            // takes for a conceptually similar "there's more here" cue.
            if (offset < currentSegment.endByte && col > 0) {
                const Brush truncationBrush{.background = theme_.background, .foreground = theme_.truncationIndicatorForeground};
                Cell&       cell = c[{.x = col - 1, .y = row}];
                cell.character   = text::EncodeCodepointUtf8(kTruncationIndicator);
                truncationBrush.ApplyTo(cell);
            }

            // line-wrap follow-up: this ellipsis represents "content AFTER
            // this line is hidden" -- belongs on the line's own last visual
            // row, not every wrap continuation row.
            if (segmentIndex + 1 == lineSegments.size()) {
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
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
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
                        Cell& spaceCell     = c[{.x = col, .y = row}];
                        spaceCell.character = " ";
                        previewBrush.ApplyTo(spaceCell);
                        ++col;
                    }
                    while (previewOffset < previewEnd && col < c.size().width) {
                        const auto decoded = content.CodepointAt(previewOffset);
                        Cell&      cell    = c[{.x = col, .y = row}];
                        cell.character     = text::EncodeCodepointUtf8(decoded.codepoint);
                        previewBrush.ApplyTo(cell);
                        ++col;
                        previewOffset += decoded.byteLength;
                    }
                    break;
                }
            } // if (segmentIndex + 1 == lineSegments.size()) -- fold ellipsis/preview

            // hover/completion follow-up: ghost-text completion suggestion,
            // dimmed, right after point on point's own line. Deliberately
            // anchored via VisualColumn(..., point, ...) rather than reusing
            // this row's own `col` (which reflects where the LINE's real
            // content ends, not where POINT is -- the two only coincide
            // when point sits at end-of-line, the common case right after a
            // self-insert keystroke that triggered this, but not guaranteed
            // in general). requestPoint == point is the staleness check --
            // point moving since the request was issued/answered means this
            // suggestion no longer applies to whatever's now under point.
            // line-wrap follow-up: also requires point to fall within THIS
            // row's own segment -- point's line can span several wrapped
            // rows, and the suggestion belongs only on the one actually
            // showing point, not every one of them.
            if (ghostCompletion_ && line == pointLine && ghostCompletion_->requestPoint == point &&
                point >= currentSegment.startByte && point <= currentSegment.endByte &&
                static_cast<int>(gutterWidth) < c.size().width) {
                // line-wrap follow-up: horizontal-scroll-follow -- same
                // leftColumn_-aware bound/offset CursorPosition() uses;
                // always a no-op adjustment while wrapActive (leftColumn_
                // stays 0 then).
                const int ghostContentWidth = c.size().width - static_cast<int>(gutterWidth);
                const int maxColumns        = ghostContentWidth + static_cast<int>(leftColumn_);
                if (const std::optional<int> pointColumn = VisualColumn(content, currentSegment.startByte, point, maxColumns, lineLinks);
                    pointColumn && *pointColumn >= static_cast<int>(leftColumn_)) {
                    const std::string suffix = GhostSuffixFor(ghostCompletion_->items[ghostCompletion_->selectedIndex]);
                    const Brush       ghostBrush{.background = theme_.background, .foreground = theme_.ghostTextForeground, .italic = true};
                    const text::Rope  suffixRope(suffix);
                    std::size_t       suffixOffset = 0;
                    int               ghostCol     = static_cast<int>(gutterWidth) + *pointColumn - static_cast<int>(leftColumn_);
                    while (suffixOffset < suffixRope.ByteLength() && ghostCol < c.size().width) {
                        const auto decoded = suffixRope.CodepointAt(suffixOffset);
                        // Never send a raw control byte to the terminal --
                        // same discipline tab-rendering-fix/binary-rendering
                        // established (see this file's own header comment);
                        // a completion suggestion realistically never
                        // contains one, so truncating here rather than
                        // growing a parallel hex-placeholder path for this
                        // narrow case is the right trade.
                        if (decoded.codepoint < 0x20 || decoded.codepoint == 0x7F) {
                            break;
                        }
                        Cell& cell     = c[{.x = ghostCol, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(decoded.codepoint);
                        ghostBrush.ApplyTo(cell);
                        ++ghostCol;
                        suffixOffset += decoded.byteLength;
                    }
                }
            }

            // line-wrap follow-up: advance to the next wrap segment (row)
            // of the same buffer line if there is one, otherwise advance to
            // the next visible buffer line -- was an unconditional
            // `line = NextVisibleLine(line + 1, renderEndLine)` before wrap
            // existed, which segmentIndex staying 0 (lineSegments always
            // exactly one entry) reduces to exactly.
            if (segmentIndex + 1 < lineSegments.size()) {
                ++segmentIndex;
            }
            else {
                segmentIndex = 0;
                line         = NextVisibleLine(line + 1, renderEndLine);
            }
        }
        else {
            line = NextVisibleLine(line + 1, renderEndLine);
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

    // Org-mode fold/unfold follow-up: a point sitting on a currently-hidden
    // line has no on-screen row to report at all -- can't happen through
    // org-cycle itself (see CycleFoldAtPoint's own doc comment), but stays
    // a real, harmless "no cursor this frame" rather than an invalid
    // position if some other path ever moves point into a hidden region.
    if (pointLine < topLine_ || IsLineHidden(pointLine)) {
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

    const std::size_t lineStart = content.LineToByteOffset(pointLine);
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();

    // Links follow-up: point's own line never has a link collapsed AT
    // point's own position (LinksForLine excludes any link containing
    // point), so this always agrees with what Paint() actually drew for
    // this specific row.
    EnsureLinkCache();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);

    // line-wrap follow-up: which wrap segment (row) of pointLine actually
    // contains point -- 0, and the whole line as one segment, when wrap is
    // off or the viewport size isn't known yet (mirrors the rest of this
    // method's own "unknown size means don't try to bound" tolerance).
    // Prefers the earliest segment point is strictly inside; only the
    // line's own LAST segment also accepts point sitting exactly at its
    // end (point at end-of-line) -- a point sitting exactly at an earlier
    // segment's own boundary belongs to the NEXT segment instead (the
    // start of a new visual row), matching how a real editor's cursor
    // behaves at a wrapped line break.
    std::size_t rowWithinLine = 0;
    std::size_t segmentStart  = lineStart;
    if (EffectiveWrapLines() && sizeIsKnown) {
        const int                      wrapWidth = std::max(1, sizeNow.width - static_cast<int>(gutterWidth));
        const std::vector<WrapSegment> segments  = ComputeWrapSegments(content, lineStart, lineEnd, wrapWidth, lineLinks);
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const bool isLast = (i + 1 == segments.size());
            if (point >= segments[i].startByte && (point < segments[i].endByte || (isLast && point == segments[i].endByte))) {
                rowWithinLine = i;
                segmentStart  = segments[i].startByte;
                break;
            }
        }
    }

    const std::size_t visibleRow = VisibleRowCountBetween(topLine_, pointLine) + rowWithinLine;
    if (sizeIsKnown && visibleRow >= static_cast<std::size_t>(sizeNow.height)) {
        return std::nullopt;
    }

    // line-wrap follow-up: horizontal-scroll-follow -- the scan has to walk
    // far enough right to still find point even when scrolled, so the bound
    // grows by leftColumn_ (only meaningful once sizeIsKnown -- an unknown
    // size already means "don't bound at all"); the true on-screen column
    // is the raw column minus leftColumn_, subtracted back out below.
    // leftColumn_ is always 0 once EffectiveWrapLines() is true (see
    // ScrollToShowPointHorizontally), so this is a no-op adjustment then.
    const int maxColumns = sizeIsKnown ? sizeNow.width - static_cast<int>(gutterWidth) + static_cast<int>(leftColumn_)
                                       : std::numeric_limits<int>::max();

    const std::optional<int> visualCol = VisualColumn(content, segmentStart, point, maxColumns, lineLinks);
    if (!visualCol || *visualCol < static_cast<int>(leftColumn_)) {
        return std::nullopt; // scrolled off the left edge -- shouldn't happen once leftColumn_ is correct, but a safe guard
    }

    const std::size_t col = gutterWidth + static_cast<std::size_t>(*visualCol) - leftColumn_;
    if (sizeIsKnown && col >= static_cast<std::size_t>(sizeNow.width)) {
        return std::nullopt; // scrolled off horizontally to the right
    }
    return Point{.x = static_cast<int>(col), .y = static_cast<int>(visibleRow)};
}

bool BufferView::Focusable() const {
    return true; // was FocusPolicy::Strong
}

bool BufferView::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        return OnMouseEvent(event);
    }
    return OnKeyEvent(event);
}

bool BufferView::OnKeyEvent(const Event& event) {
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
    if (inputMode_ == InputMode::ConfirmOpenBinary) {
        HandleConfirmOpenBinaryKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::SwitchToBuffer ||
        inputMode_ == InputMode::ProjectSearch || inputMode_ == InputMode::CreateDirectory ||
        inputMode_ == InputMode::FindScratch || inputMode_ == InputMode::StringRectangle ||
        inputMode_ == InputMode::SetHeadlineTags || inputMode_ == InputMode::TaskName) {
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
    if (inputMode_ == InputMode::LspCodeActionSelect) {
        HandleCodeActionSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspCodeActionConfirm) {
        HandleCodeActionConfirmKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspGotoDefinitionSelect) {
        HandleDefinitionSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspRenameNewName) {
        HandlePromptKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspRenameConfirm) {
        HandleRenameConfirmKey(*chord);
        ClampPointToNarrowing();
        return true;
    }

    // hover/completion follow-up: ghost-text state only ever exists while
    // inputMode_ == Normal (every branch above returns before reaching
    // here), so this is the one place it needs handling -- Tab accepts,
    // M-n/M-p cycle, and (falling through the first three) any other key
    // dismisses it, then continues to whatever that key would ordinarily
    // do. Checked ahead of the normal dispatch below so Tab/M-n/M-p never
    // reach Dispatcher::Feed while a suggestion is showing.
    if (ghostCompletion_) {
        if (chord->Special == editor::SpecialKey::Tab && !chord->Control && !chord->Meta) {
            AcceptGhostCompletion();
            ClampPointToNarrowing();
            return true;
        }
        if (chord->Meta && !chord->Control && chord->Codepoint == U'n') {
            CycleGhostCompletion(1);
            return true;
        }
        if (chord->Meta && !chord->Control && chord->Codepoint == U'p') {
            CycleGhostCompletion(-1);
            return true;
        }
        ghostCompletion_.reset();
    }

    // project-search-visit-result follow-up: Enter on a read-only
    // ("tossable") buffer -- search results, project-replace's preview,
    // project-agenda -- visits whatever result is under point instead of
    // doing nothing (the buffer can't accept a literal newline anyway,
    // being read-only). VisitSearchResult's own silent no-op on a
    // non-matching line (see its doc comment) is what makes this safe to
    // key off ReadOnly() alone, without needing to know which specific
    // kind of results buffer this is.
    if (chord->Special == editor::SpecialKey::Enter && !chord->Control && !chord->Meta && activeBuffer_.Get().ReadOnly()) {
        VisitSearchResult();
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
    // status-message-lifecycle follow-up: attemptedSequence reconstructs
    // exactly what Feed's own pending_ will see (its first line is
    // pending_.push_back(chord)) -- captured *before* calling Feed since
    // Feed clears pending_ itself on a NoMatch/Unbound result, so there'd be
    // nothing left to read afterward otherwise.
    std::vector<editor::KeyChord> attemptedSequence = dispatcher_.Pending();
    attemptedSequence.push_back(*chord);

    editor::Dispatcher::Outcome outcome = editor::Dispatcher::Outcome::Unbound;
    editor::CommandContext      context = MakeContext();
    context.viewportHeight              = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    const bool ran                      = RunCommandAndHandleOutcome(
        context,
        [&] {
            outcome = dispatcher_.Feed(*chord, context);
            return outcome == editor::Dispatcher::Outcome::Invoked;
        },
        &*chord);

    // ran (not outcome) gates this: outcome can be stale -- if Feed's own
    // Match case invokes a command that throws, Feed never reaches its
    // `return Outcome::Invoked` line, leaving outcome at its default
    // Unbound even though a real command genuinely ran (and already
    // reported its own exception message via RunCommandAndHandleOutcome's
    // catch). ran correctly reflects that either way. Only reached when
    // !ran (Pending/Unbound never invoke a command, so *this* is never at
    // risk of having been destroyed by a window-management
    // interactiveRequest here -- see RunCommandAndHandleOutcome's own doc
    // comment).
    if (!ran) {
        if (outcome == editor::Dispatcher::Outcome::Pending) {
            statusMessage_ = editor::FormatKeySequence(dispatcher_.Pending()) + "-"; // matches real Emacs' own "C-x-" while-waiting convention
        }
        else if (outcome == editor::Dispatcher::Outcome::Unbound) {
            statusMessage_ = editor::FormatKeySequence(attemptedSequence) + " is undefined";
        }
    }
    return true;
}

bool BufferView::RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<bool()>& invoke,
                                            const editor::KeyChord* triggeringChord) {
    const std::size_t generationBefore    = activeBuffer_.Get().ContentGeneration();
    const std::string statusMessageBefore = statusMessage_; // status-message-lifecycle: see the "clear if unchanged" check below
    bool              ran                 = false;
    try {
        ran = invoke();
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
        ran            = true; // a command did run, it just threw -- still "something happened"
    }

    // status-message-lifecycle follow-up: a real, invoked command (ran ==
    // true -- Pending/Unbound never reach here with ran true, so a
    // still-accumulating prefix sequence's own about-to-be-shown "C-x-"
    // indicator is never touched by this) that didn't itself report
    // anything new clears whatever stale message was already showing --
    // "just sitting there" after some other real action (moving point,
    // editing, anything) doesn't make sense. Guarded on statusMessage_
    // still matching what it was before this dispatch even started: a
    // command that explicitly re-sets the exact same text (rare) is
    // indistinguishable from one that never touched it at all with this
    // diff-only approach -- a narrow, documented trade-off rather than
    // threading a "did I actually write something" flag through every one
    // of dozens of existing command implementations.
    if (ran && !statusMessage_.empty() && statusMessage_ == statusMessageBefore) {
        statusMessage_.clear();
    }

    if (context.quit) {
        // eventLoop_ is nullptr outside a real, running-editor SetEventLoop
        // call -- every unit test, and any other headless use of
        // BufferView. It is never null during real, running-editor usage
        // (main.cpp is what constructs the EventLoop and wires it in), but
        // skipping the null check entirely crashed the whole process the
        // instant a test exercised `quit` under the FTXUI-era equivalent of
        // this same check (ftxui::ScreenInteractive::Active(), confirmed
        // via a real SIGSEGV then) -- kept just as strict here, not
        // reintroduced as a hypothetical risk.
        if (eventLoop_) {
            eventLoop_->Exit();
        }
        return ran;
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
        return ran;
    }

    // hover/completion follow-up: only reached for an ordinary, non-
    // interactive, still-alive-*this* dispatch -- exactly the "organic
    // keystroke" case worth considering for automatic completion.
    // triggeringChord is only non-null from OnKeyEvent's own call site (see
    // this method's own doc comment in BufferView.h), so macro replay/M-x
    // never reach this.
    if (triggeringChord) {
        MaybeScheduleAutoCompletion(*triggeringChord, generationBefore);
    }

    ClampPointToNarrowing();
    ScrollToShowPoint();
    return ran;
}

void BufferView::EnsureStatusMessageFreshness() {
    // While any interactive session is active (isearch, a prompt like
    // "Project search: ", query-replace, ...), statusMessage_ is that
    // session's own live, actively-managed text -- e.g. what the user has
    // typed into a prompt so far. It must never be auto-cleared out from
    // under them just because they paused for a few seconds mid-typing,
    // and it's already re-shown on every keystroke by the session's own
    // Handle*Key method regardless, so there's nothing for the idle timer
    // to usefully guard here. Deliberately not just "skip arming a new
    // deadline" -- also drops any deadline armed before this session
    // started (Normal-mode message that was already showing when the
    // session opened, and would otherwise silently expire mid-session and
    // then, confusingly, expire the session's *own* text on the very next
    // check right after the session ends) and keeps the snapshot synced so
    // there's no stale diff to misfire against once back in Normal mode.
    if (inputMode_ != InputMode::Normal) {
        statusMessageSnapshot_ = statusMessage_;
        statusMessageChangedAt_.reset();
        return;
    }

    if (statusMessage_ == statusMessageSnapshot_) {
        return; // nothing wrote a new message since the last Paint() call
    }
    statusMessageSnapshot_ = statusMessage_;
    if (statusMessage_.empty()) {
        statusMessageChangedAt_.reset(); // nothing to time out
        return;
    }
    statusMessageChangedAt_ = std::chrono::steady_clock::now();

    // FTXUI -> Notcurses migration: was ftxui::animation::RequestAnimationFrame(),
    // which guaranteed OnAnimation got called again purely so idle time
    // could actually elapse even with no further input; DeadlineTimer::Arm
    // is the direct replacement -- a real background thread wakes exactly
    // once, kStatusMessageTimeout from now, and Posts the clear back onto
    // the loop thread, no polling needed. Guarded by statusMessageSnapshot_
    // still matching statusMessage_ at fire time: if something else wrote a
    // new message before this deadline elapsed, this fire must not clear
    // text it didn't set (a fresh deadline for that newer text was armed
    // by this same method's own next call, which re-armed
    // statusMessageTimer_, superseding this one outright -- Arm() cancels
    // any not-yet-fired previous callback the same way OnAnimation's own
    // "only the latest deadline matters" comment used to describe, just via
    // real cancellation now instead of a stale-check that never actually
    // needed to run).
    if (eventLoop_) {
        statusMessageTimer_.Arm(*eventLoop_, kStatusMessageTimeout, [this] {
            if (statusMessage_ == statusMessageSnapshot_) {
                statusMessage_.clear();
                statusMessageSnapshot_.clear();
            }
            statusMessageChangedAt_.reset();
        });
    }
}

void BufferView::RequestCompletionAtPoint() {
    if (!lspManager_) {
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++completionRequestGeneration_;

    lspManager_->RequestCompletion(
        buffer, point, [this, bufferPtr, point, generation](std::vector<editor::lsp::CompletionItem> items) {
            if (generation != completionRequestGeneration_) {
                return; // superseded by a newer request
            }
            // bufferPtr is only ever compared, never dereferenced, unless
            // this comparison already confirms it's the (guaranteed alive)
            // current active buffer -- safe even if the buffer it pointed to
            // was since closed, the same idiom BufferList::PreviewBuffer's
            // own mutable Buffer* already relies on.
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (items.empty()) {
                ghostCompletion_.reset();
                return;
            }
            ghostCompletion_ = GhostCompletion{.requestPoint = point, .items = std::move(items), .selectedIndex = 0};
        });
}

bool BufferView::ShouldSuppressAutoCompletion() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    const std::size_t   point  = buffer.Point();
    if (point == 0) {
        return false;
    }
    const text::Rope& content = buffer.Content();

    if (highlightCacheBuffer_ == &buffer) {
        const std::size_t                        line        = content.ByteOffsetToLine(point);
        const std::size_t                        lineStart   = content.LineToByteOffset(line);
        const std::size_t                        lineEnd     = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::vector<editor::HighlightSpan> lineSpans   = SpansForLine(highlightCacheSpans_, lineStart, lineEnd);
        const std::size_t                        priorOffset = content.PreviousCodepointBoundary(point);
        switch (ClassAtOffset(lineSpans, priorOffset)) {
            case editor::SyntaxClass::String:
            case editor::SyntaxClass::StringEscape:
            case editor::SyntaxClass::Comment:
            case editor::SyntaxClass::DocComment:
            case editor::SyntaxClass::Number:
                return true;
            default:
                break;
        }
    }

    // Fallback, independent of highlighting availability (covers
    // FundamentalMode and any other mode with no highlighter): a purely
    // numeric token immediately before point shouldn't trigger completion
    // either -- the exact "typing a number" complaint that motivated this
    // heuristic. WordPrefixStart's own ASCII-only guarantee makes token a
    // safe raw-byte string to scan.
    const std::size_t prefixStart = WordPrefixStart(content, point);
    if (prefixStart < point) {
        const std::string token = content.Substring(prefixStart, point - prefixStart);
        if (std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            return true;
        }
    }
    return false;
}

void BufferView::MaybeScheduleAutoCompletion(const editor::KeyChord& chord, std::size_t generationBefore) {
    ghostCompletion_.reset(); // typing invalidates any currently-shown suggestion
    if (!lspManager_ || !editor::lsp::LspAutoCompleteEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta || chord.Special != editor::SpecialKey::None) {
        return; // only plain self-insert keystrokes schedule automatic completion
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    if (ShouldSuppressAutoCompletion()) {
        return;
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    completionDebounceDeadline_ = std::chrono::steady_clock::now() + delay;
    // FTXUI -> Notcurses migration: was ftxui::animation::RequestAnimationFrame()
    // (OnAnimation polled completionDebounceDeadline_ every frame until it
    // elapsed); DeadlineTimer::Arm fires exactly once, for real, delay from
    // now -- re-typing before it fires re-arms it via this same call site on
    // the very next qualifying keystroke, cancelling the stale one outright,
    // which is what makes this a debounce rather than a fixed-interval
    // repeat (same behavior the old overwritten-deadline approach had).
    if (eventLoop_) {
        completionDebounceTimer_.Arm(*eventLoop_, delay, [this] {
            completionDebounceDeadline_.reset();
            RequestCompletionAtPoint();
        });
    }
}

void BufferView::AcceptGhostCompletion() {
    if (!ghostCompletion_) {
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::string suffix = GhostSuffixFor(ghostCompletion_->items[ghostCompletion_->selectedIndex]);
    ghostCompletion_.reset();
    if (!suffix.empty()) {
        buffer.InsertAtPoint(suffix);
    }
}

void BufferView::CycleGhostCompletion(int direction) {
    if (!ghostCompletion_ || ghostCompletion_->items.empty()) {
        return;
    }
    const std::size_t count         = ghostCompletion_->items.size();
    const std::size_t current       = ghostCompletion_->selectedIndex;
    ghostCompletion_->selectedIndex = (direction > 0) ? (current + 1) % count : (current + count - 1) % count;
}

std::string BufferView::GhostSuffixFor(const editor::lsp::CompletionItem& item) const {
    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::Rope&   content     = buffer.Content();
    const std::size_t   point       = buffer.Point();
    const std::size_t   prefixStart = WordPrefixStart(content, point);
    const std::string   prefix      = content.Substring(prefixStart, point - prefixStart);

    if (item.insertText.size() > prefix.size() && item.insertText.compare(0, prefix.size(), prefix) == 0) {
        return item.insertText.substr(prefix.size());
    }
    // The server's insertText doesn't share our naively-computed word
    // prefix (e.g. it used a textEdit range instead) -- shown in full
    // rather than guessed at; a documented v1 limitation, not a crash risk.
    return item.insertText;
}

void BufferView::RequestCodeActionsAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++codeActionRequestGeneration_;

    // Prefer the diagnostic covering point (same lookup lsp-show-diagnostic
    // already does), else a zero-length range at point.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const bool atPoint = (diagnostic.startByte == diagnostic.endByte) ? (point == diagnostic.startByte)
                                                                          : (diagnostic.startByte <= point && point < diagnostic.endByte);
        if (atPoint) {
            rangeStart = diagnostic.startByte;
            rangeEnd   = diagnostic.endByte;
            break;
        }
    }

    statusMessage_ = "Requesting code actions...";
    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd, [this, bufferPtr, point, generation](std::vector<editor::lsp::CodeAction> actions) {
            if (generation != codeActionRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- see RequestCompletionAtPoint's own identical guard
            }
            pendingCodeActions_ = std::move(actions);
            if (pendingCodeActions_.empty()) {
                statusMessage_ = "No code actions available.";
                return;
            }
            codeActionSelection_ = 0;
            if (pendingCodeActions_.size() == 1) {
                inputMode_     = InputMode::LspCodeActionConfirm;
                statusMessage_ = "Apply \"" + pendingCodeActions_[0].title + "\"? (y/n)";
                return;
            }
            inputMode_ = InputMode::LspCodeActionSelect;
            RefreshCodeActionSelectStatus();
        });
}

void BufferView::RefreshCodeActionSelectStatus() {
    std::string status = "Code action: ";
    for (std::size_t i = 0; i < pendingCodeActions_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == codeActionSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingCodeActions_[i].title + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleCodeActionSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Code action cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        codeActionSelection_ = (codeActionSelection_ + 1) % pendingCodeActions_.size();
        RefreshCodeActionSelectStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        codeActionSelection_ = (codeActionSelection_ + pendingCodeActions_.size() - 1) % pendingCodeActions_.size();
        RefreshCodeActionSelectStatus();
        return;
    }
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index < pendingCodeActions_.size()) {
            codeActionSelection_ = index;
        }
        // falls through to the same Confirm transition Enter performs below
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the selection list
    }

    inputMode_     = InputMode::LspCodeActionConfirm;
    statusMessage_ = "Apply \"" + pendingCodeActions_[codeActionSelection_].title + "\"? (y/n)";
}

void BufferView::HandleCodeActionConfirmKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        const editor::lsp::CodeAction action = pendingCodeActions_[codeActionSelection_];
        // code-actions-resolve follow-up: a server (clangd included)
        // advertising resolveProvider deliberately sends this action back
        // without an edit yet -- codeAction/resolve fills it in, only now
        // that the user has actually chosen to apply it (see CodeAction::
        // resolvable's own doc comment in LspContent.h for why this isn't
        // done eagerly for every listed action). Fire-and-forget, same
        // async shape as every other LSP request here: EndInteractiveSession()
        // runs immediately, ApplyCodeAction runs later from inside the
        // callback once the resolved edit actually arrives.
        if (action.resolvable && lspManager_) {
            text::Buffer* const bufferPtr = &activeBuffer_.Get();
            statusMessage_                = "Resolving \"" + action.title + "\"...";
            lspManager_->ResolveCodeAction(activeBuffer_.Get(), action,
                                           [this, bufferPtr, action](std::optional<editor::lsp::CodeAction> resolved) {
                                               if (bufferPtr != &activeBuffer_.Get()) {
                                                   return; // active buffer changed since the resolve request was sent
                                               }
                                               if (!resolved || !resolved->hasEdit) {
                                                   statusMessage_ = "\"" + action.title + "\" could not be resolved.";
                                                   return;
                                               }
                                               ApplyCodeAction(*resolved);
                                           });
            EndInteractiveSession();
            return;
        }
        ApplyCodeAction(action);
        EndInteractiveSession();
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Code action cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay at the confirmation.
}

namespace {

    // Shared by ApplyCodeAction and ApplyRename: resolves each edit's
    // LspPositions to byte offsets against buffer's CURRENT content, sorts
    // descending by start byte (keeps an edit not yet applied valid as an
    // earlier-in-the-buffer one shifts positions -- LSP guarantees edits
    // within one WorkspaceEdit don't overlap, so a plain sort suffices), and
    // applies each via Buffer::DeleteRange + Buffer::InsertAt.
    void ApplyWorkspaceTextEdits(text::Buffer& buffer, const std::vector<editor::lsp::WorkspaceTextEdit>& edits) {
        const text::Rope& content = buffer.Content();

        struct ResolvedEdit {
            std::size_t startByte;
            std::size_t endByte;
            std::string newText;
        };
        std::vector<ResolvedEdit> resolved;
        resolved.reserve(edits.size());
        for (const editor::lsp::WorkspaceTextEdit& edit : edits) {
            resolved.push_back(ResolvedEdit{
                .startByte = editor::lsp::LspPositionToByte(content, edit.start),
                .endByte   = editor::lsp::LspPositionToByte(content, edit.end),
                .newText   = edit.newText,
            });
        }
        std::sort(resolved.begin(), resolved.end(), [](const ResolvedEdit& a, const ResolvedEdit& b) { return a.startByte > b.startByte; });

        for (const ResolvedEdit& edit : resolved) {
            buffer.DeleteRange(edit.startByte, edit.endByte - edit.startByte);
            buffer.InsertAt(edit.startByte, edit.newText);
        }
    }

} // namespace

void BufferView::ApplyCodeAction(const editor::lsp::CodeAction& action) {
    if (action.touchesOtherFiles) {
        statusMessage_ = "\"" + action.title + "\" edits other files -- not supported yet.";
        return;
    }
    if (!action.hasEdit || action.edits.empty()) {
        statusMessage_ = "\"" + action.title + "\" has no edit to apply.";
        return;
    }

    ApplyWorkspaceTextEdits(activeBuffer_.Get(), action.edits);
    statusMessage_ = "Applied \"" + action.title + "\".";
}

void BufferView::RequestDefinitionAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++definitionRequestGeneration_;

    statusMessage_ = "Requesting definition...";
    lspManager_->RequestDefinition(
        buffer, point, [this, bufferPtr, point, generation](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
            if (generation != definitionRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- see RequestCodeActionsAtPoint's own identical guard
            }
            pendingDefinitions_ = std::move(locations);
            if (pendingDefinitions_.empty()) {
                statusMessage_ = "No definition found.";
                return;
            }
            if (pendingDefinitions_.size() == 1) {
                // No confirmation needed, unlike a code action -- opening a
                // file and moving point is trivially undoable/re-navigable,
                // nothing destructive to confirm.
                JumpToDefinition(pendingDefinitions_[0]);
                return;
            }
            definitionSelection_ = 0;
            inputMode_           = InputMode::LspGotoDefinitionSelect;
            RefreshDefinitionSelectStatus();
        });
}

void BufferView::RefreshDefinitionSelectStatus() {
    std::string status = "Definition: ";
    for (std::size_t i = 0; i < pendingDefinitions_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == definitionSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingDefinitions_[i].path.filename().string() +
                  ":" + std::to_string(pendingDefinitions_[i].position.line + 1) + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDefinitionSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Go to definition cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        definitionSelection_ = (definitionSelection_ + 1) % pendingDefinitions_.size();
        RefreshDefinitionSelectStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        definitionSelection_ = (definitionSelection_ + pendingDefinitions_.size() - 1) % pendingDefinitions_.size();
        RefreshDefinitionSelectStatus();
        return;
    }
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index < pendingDefinitions_.size()) {
            definitionSelection_ = index;
        }
        // falls through to the same jump Enter performs below
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the selection list
    }

    const editor::lsp::LspManager::ResolvedLocation location = pendingDefinitions_[definitionSelection_];
    EndInteractiveSession();
    JumpToDefinition(location);
}

void BufferView::JumpToDefinition(const editor::lsp::LspManager::ResolvedLocation& location) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(location.path);
        activeBuffer_.Set(opened);
        opened.SetPoint(editor::lsp::LspPositionToByte(opened.Content(), location.position));
        statusMessage_.clear();
        ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
}

void BufferView::RequestRenameAtPoint(const std::string& newName) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++renameRequestGeneration_;

    statusMessage_ = "Requesting rename...";
    lspManager_->RequestRename(
        buffer, point, newName,
        [this, bufferPtr, point, generation](std::optional<editor::lsp::LspManager::ResolvedRename> result) {
            if (generation != renameRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (!result) {
                statusMessage_ = "Rename failed.";
                return;
            }
            if (result->touchesUnsupportedForm) {
                statusMessage_ = "Rename uses an unsupported edit form -- not applied.";
                return;
            }
            if (!result->hasEdit || result->edits.empty()) {
                statusMessage_ = "No rename edits available.";
                return;
            }
            pendingRename_ = std::move(*result);

            std::size_t fileCount = pendingRename_->edits.size();
            std::size_t editCount = 0;
            for (const auto& edit : pendingRename_->edits) {
                editCount += edit.edits.size();
            }
            renameTitle_ = std::to_string(editCount) + " edit" + (editCount == 1 ? "" : "s") + " across " +
                           std::to_string(fileCount) + " file" + (fileCount == 1 ? "" : "s");

            inputMode_ = InputMode::LspRenameConfirm;
            RefreshRenameConfirmStatus();
        });
}

void BufferView::RefreshRenameConfirmStatus() {
    statusMessage_ = "Rename: " + renameTitle_ + "? (y/n)";
}

void BufferView::HandleRenameConfirmKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        if (pendingRename_) {
            ApplyRename(*pendingRename_);
        }
        EndInteractiveSession();
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Rename cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay at the confirmation.
}

void BufferView::ApplyRename(const editor::lsp::LspManager::ResolvedRename& result) {
    if (result.touchesUnsupportedForm || !result.hasEdit || result.edits.empty()) {
        statusMessage_ = "Rename has no edit to apply.";
        return;
    }

    // Resolve (find-or-open) every touched buffer FIRST, applying nothing
    // until every single one succeeds -- a rename either fully applies
    // across every affected file or leaves every buffer untouched, never a
    // partial rename across only some of them.
    std::vector<text::Buffer*> buffers;
    buffers.reserve(result.edits.size());
    try {
        for (const editor::lsp::LspManager::ResolvedRenameEdit& edit : result.edits) {
            text::Buffer* buffer = bufferList_.FindByPath(edit.path);
            if (!buffer) {
                buffer = &bufferList_.OpenFile(edit.path);
            }
            buffers.push_back(buffer);
        }
    }
    catch (const std::exception& e) {
        statusMessage_ = std::string("Rename failed: ") + e.what();
        return;
    }

    for (std::size_t i = 0; i < result.edits.size(); ++i) {
        ApplyWorkspaceTextEdits(*buffers[i], result.edits[i].edits);
    }
    statusMessage_ = "Renamed (" + renameTitle_ + ").";
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
                if (buffer->Modified() && !buffer->ReadOnly()) {
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
        case editor::InteractiveRequest::LspGotoDefinition:
            RequestDefinitionAtPoint();
            return;
        case editor::InteractiveRequest::LspRename:
            inputMode_ = InputMode::LspRenameNewName;
            prompt_.emplace("New name: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::LspShowLog: {
            const std::string logName = std::string(editor::lsp::kLspLogBufferName);
            text::Buffer*     log     = bufferList_.Find(logName);
            if (!log) {
                log = &bufferList_.CreateBuffer(logName);
                log->SetReadOnly(true);
            }
            activeBuffer_.Set(*log);
            return;
        }
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
        case editor::InteractiveRequest::RunTask:
            taskPromptAction_ = TaskPromptAction::Run;
            inputMode_        = InputMode::TaskName;
            prompt_.emplace("Run task: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::CancelTask:
            taskPromptAction_ = TaskPromptAction::Cancel;
            inputMode_        = InputMode::TaskName;
            prompt_.emplace("Cancel task: ");
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
        // hover/completion follow-up: another one-shot direct action --
        // doesn't touch inputMode_, ghost-text state coexists with ordinary
        // Normal-mode editing rather than replacing it (see
        // GhostCompletion's own doc comment in BufferView.h).
        case editor::InteractiveRequest::LspComplete:
            RequestCompletionAtPoint();
            return;
        // code-actions follow-up: also a one-shot direct action -- inputMode_
        // is deliberately left untouched here, only changed later, from
        // inside RequestCodeActionsAtPoint's own async callback once the
        // response actually arrives (see that method's own doc comment).
        case editor::InteractiveRequest::LspCodeAction:
            RequestCodeActionsAtPoint();
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
    pendingBinaryOpenPath_.clear();
    deleteStage_ = DeleteFileStage::EnteringPath;
    deleteTarget_.clear();
    renameStage_ = RenameFileStage::EnteringSource;
    renameSource_.clear();
    executeCommandSelection_  = 0;
    projectFindFileSelection_ = 0;
    projectFindFileCandidates_.clear(); // cached only for the duration of one session -- see its own doc comment in BufferView.h
    pendingCodeActions_.clear();
    codeActionSelection_ = 0;
    pendingDefinitions_.clear();
    definitionSelection_ = 0;
    pendingRename_.reset();
    renameTitle_.clear();
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
            catch (const text::BinaryFileError&) {
                // open-binary-anyway follow-up: ask instead of just
                // reporting the refusal -- returns without EndInteractiveSession()
                // below, since this transitions to a second y/n prompt
                // rather than finishing the session outright.
                prompt_.reset();
                BeginConfirmOpenBinary(input);
                return;
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
        else if (inputMode_ == InputMode::LspRenameNewName) {
            // Fire-and-forget, same async shape as RequestCodeActionsAtPoint:
            // EndInteractiveSession() below runs immediately, the actual
            // LspRenameConfirm transition happens later, from inside
            // RequestRenameAtPoint's own callback, once the response
            // arrives.
            RequestRenameAtPoint(input);
        }
        else if (inputMode_ == InputMode::TaskName) {
            if (input.empty()) {
                statusMessage_ = "No task name given.";
            }
            else if (!taskRunner_) {
                statusMessage_ = "No task runner available.";
            }
            else if (taskPromptAction_ == TaskPromptAction::Run) {
                if (text::Buffer* buffer = taskRunner_->RunTask(input)) {
                    activeBuffer_.Set(*buffer);
                    statusMessage_.clear();
                }
            }
            else { // Cancel
                if (taskRunner_->IsRunning(input)) {
                    taskRunner_->CancelTask(input);
                    statusMessage_ = "Cancelling task \"" + input + "\"...";
                }
                else {
                    statusMessage_ = "No running task named \"" + input + "\"";
                }
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
    if (IsQuit(chord)) {
        // status-message-lifecycle follow-up: a real, visible "cancelled"
        // message rather than an immediate blank -- the new auto-clear
        // mechanism (EnsureStatusMessageFreshness/OnAnimation) takes it
        // from here, the same way it does for any other status message.
        std::string label;
        switch (inputMode_) {
            case InputMode::FindFile:
                label = "Find file";
                break;
            case InputMode::SwitchToBuffer:
                label = "Switch to buffer";
                break;
            case InputMode::ProjectSearch:
                label = "Project search";
                break;
            case InputMode::CreateDirectory:
                label = "Create directory";
                break;
            case InputMode::FindScratch:
                label = "Find scratch";
                break;
            case InputMode::StringRectangle:
                label = "String rectangle";
                break;
            case InputMode::SetHeadlineTags:
                label = "Set headline tags";
                break;
            case InputMode::LspRenameNewName:
                label = "Rename";
                break;
            case InputMode::TaskName:
                label = (taskPromptAction_ == TaskPromptAction::Run) ? "Run task" : "Cancel task";
                break;
            default:
                label = "Prompt";
                break;
        }
        statusMessage_ = label + " cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Tab && inputMode_ != InputMode::ProjectSearch &&
        inputMode_ != InputMode::CreateDirectory && inputMode_ != InputMode::StringRectangle &&
        inputMode_ != InputMode::SetHeadlineTags && inputMode_ != InputMode::LspRenameNewName &&
        inputMode_ != InputMode::TaskName) {
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
    // read-only-buffers follow-up: a synthesized, no-file-to-save-to
    // buffer -- read-only both to prevent editing it (nothing meaningful
    // would happen to the edit anyway) and, doubling as "tossable," so its
    // Modified() state (unavoidable -- InsertAtPoint above already set it)
    // never triggers the close/quit unsaved-changes prompt. See
    // RequestCloseBuffer/StartInteractiveSession's ConfirmQuit case.
    results.SetReadOnly(true);
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
    // hiding lines above the click. line-wrap follow-up: was
    // AdvanceVisibleLines (pure line-stepping, 1 row per visible line);
    // now a row-aware walk that consumes RowsForLine(line) rows per line
    // instead, additionally reporting which segment of the landed-on line
    // the target row corresponds to.
    const text::Buffer& buffer     = activeBuffer_.Get();
    const text::Rope&   content    = buffer.Content();
    const std::size_t   totalLines = content.LineCount();

    std::size_t targetRow     = static_cast<std::size_t>(std::max(at.y, 0));
    std::size_t line          = topLine_;
    std::size_t segmentInLine = 0;
    while (line < totalLines) {
        const std::size_t rows = RowsForLine(line);
        if (rows == 0) {
            line = NextVisibleLine(line + 1, totalLines);
            continue;
        }
        if (targetRow < rows) {
            segmentInLine = targetRow;
            break;
        }
        targetRow -= rows;
        line = NextVisibleLine(line + 1, totalLines);
    }
    line = std::min(line, totalLines - 1); // mirrors Buffer::ByteOffsetForLineAndColumn's own clamp

    const std::size_t x           = static_cast<std::size_t>(std::max(at.x, 0));
    const std::size_t gutterWidth = GutterWidth();
    // A click inside the gutter itself lands on that line's first column,
    // same as clicking right at the start of the line's text. line-wrap
    // follow-up: leftColumn_ added back on -- the click's on-screen column
    // has to be translated back to the line's own column space, the same
    // "screen column = real column - leftColumn_" relationship Paint()'s own
    // fast-forward phase established for drawing. Always 0 once
    // EffectiveWrapLines() is true, a no-op then.
    const std::size_t column = (x > gutterWidth) ? x - gutterWidth + leftColumn_ : leftColumn_;

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

    // line-wrap follow-up: resolve the click against the landed-on
    // segment's own [startByte, endByte) instead of the whole line's range
    // -- lineLinks is still the whole line's own set (matches Paint()'s own
    // "compute once per line, reuse per segment" shape), just the byte
    // range being searched narrows to this one row.
    std::size_t segStart = lineStart;
    std::size_t segEnd   = lineEnd;
    if (EffectiveWrapLines()) {
        const int                      wrapWidth      = std::max(1, size().width - static_cast<int>(gutterWidth));
        const std::vector<WrapSegment> segments       = ComputeWrapSegments(content, lineStart, lineEnd, wrapWidth, lineLinks);
        const std::size_t              clampedSegment = std::min(segmentInLine, segments.size() - 1);
        segStart                                      = segments[clampedSegment].startByte;
        segEnd                                        = segments[clampedSegment].endByte;
    }

    return ByteOffsetForColumnInLine(content, segStart, segEnd, column, editor::TabWidth(), lineLinks);
}

bool BufferView::FoldGutterActive() const {
    return mode_.fold && editor::CodeFoldingEnabled() && !activeBuffer_.Get().ReadOnly();
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
    const std::size_t foldColumn = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + std::to_string(totalLines).size() + kLineNumberGap + foldColumn;
}

bool BufferView::OnMouseEvent(const Event& event) {
    const MouseEvent rawMouse = event.mouse();
    LogMouseEvent(MouseEventTag(rawMouse), rawMouse);

    // A growing sidebar-resize drag (round-2 sidebar follow-up) can deliver
    // move/release events while the cursor is over BufferView, not
    // ProjectSidebar itself -- checked first, regardless of position (every
    // leaf widget receives every mouse event in FTXUI; see Widget.h's own
    // header comment), taking priority over BufferView's own handling.
    if (projectSidebar_ != nullptr && projectSidebar_->IsResizing()) {
        if (rawMouse.motion == MouseEvent::Motion::Moved) {
            projectSidebar_->UpdateResize(rawMouse.at.x);
            return true;
        }
        if (rawMouse.motion == MouseEvent::Motion::Released) {
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
    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr std::size_t kWheelScrollLines = 3;
        if (mouse->button == MouseEvent::Button::WheelUp) {
            SetTopLine((topLine_ > kWheelScrollLines) ? topLine_ - kWheelScrollLines : 0);
        }
        else {
            SetTopLine(topLine_ + kWheelScrollLines);
        }
        return true;
    }

    if (inputMode_ != InputMode::Normal || mouse->button != MouseEvent::Button::Left) {
        return false;
    }

    if (mouse->motion == MouseEvent::Motion::Pressed) {
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
        const std::size_t foldColumnWidth = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
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
        // project-search-visit-result follow-up: a click on a read-only
        // ("tossable") results buffer visits the result under the click,
        // the same "just press it" convention Enter now also follows
        // there -- see this method's own OnKeyEvent counterpart.
        if (buffer.ReadOnly()) {
            VisitSearchResult();
        }
        return true;
    }
    if (mouse->motion == MouseEvent::Motion::Moved) {
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

void BufferView::LogMouseEvent(std::string_view event, const MouseEvent& mouse) const {
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
    log << event << " at=(" << mouse.at.x << ',' << mouse.at.y << ')' << " button=" << static_cast<int>(mouse.button)
        << " inputMode=" << static_cast<int>(inputMode_) << " point=" << buffer.Point()
        << " mark=" << (buffer.HasMark() ? static_cast<long long>(buffer.Mark()) : -1LL) << " topLine=" << topLine_
        << " size=" << size().width << 'x' << size().height << '\n';
}

void BufferView::HandleConfirmQuitKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        // See the identical null check in OnKeyEvent's own context.quit
        // branch for why this is required, not defensive.
        if (eventLoop_) {
            eventLoop_->Exit();
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

    if (!buffer.Modified() || buffer.ReadOnly()) {
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

void BufferView::RequestOpenBinaryFile(const std::filesystem::path& path) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    BeginConfirmOpenBinary(path);
}

void BufferView::BeginConfirmOpenBinary(const std::filesystem::path& path) {
    pendingBinaryOpenPath_ = path;
    inputMode_             = InputMode::ConfirmOpenBinary;
    statusMessage_         = "\"" + path.string() + "\" looks like a binary file; open anyway? (y/n)";
}

void BufferView::HandleConfirmOpenBinaryKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        const std::filesystem::path path = pendingBinaryOpenPath_;
        EndInteractiveSession(); // clears pendingBinaryOpenPath_ before the open below touches activeBuffer_
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(path, /*allowBinary=*/true);
            activeBuffer_.Set(opened);
            statusMessage_ = "Opened " + opened.Name();
        }
        catch (const std::exception& e) {
            statusMessage_ = e.what();
        }
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Open cancelled.";
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
            statusMessage_ = "Delete cancelled.";
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
        statusMessage_ = "Rename cancelled.";
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
        statusMessage_ = "Register command cancelled.";
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
        statusMessage_ = "Command cancelled.";
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
        statusMessage_ = "Project find file cancelled.";
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
        const auto        visibleLines  = static_cast<std::size_t>(size().height);
        const std::size_t pointLineRows = RowsForLine(pointLine);
        // Org-mode fold/unfold follow-up: was `pointLine >= topLine_ +
        // visibleLines` / `topLine_ = pointLine - visibleLines + 1`, raw
        // buffer-line arithmetic that assumed every line between topLine_
        // and pointLine renders as its own row. line-wrap follow-up:
        // VisibleRowCountAtLeast is the fold-AND-wrap-aware "would
        // pointLine's own last row still fit" check (was
        // VisibleLineCountBetween, then VisibleRowCountBetween -- an
        // early-exit bounded check now, not an exact sum, so this never
        // walks more of a huge document than the viewport itself needs;
        // see VisibleRowCountAtLeast's own doc comment).
        // pointLineRows > visibleLines is checked first, short-circuiting
        // before the subtraction below could ever underflow (pointLine
        // itself taller than the whole viewport -- always needs a rescroll
        // regardless of what's above it).
        if (pointLineRows > visibleLines || VisibleRowCountAtLeast(topLine_, pointLine, visibleLines + 1 - pointLineRows)) {
            // line-wrap follow-up: was a partial-credit backward walk
            // (`remaining -= min(remaining, RowsForLine(newTop))`) that
            // could stop mid-line, silently discarding whatever didn't
            // fit -- topLine_ can only ever start at a line's own first
            // row (a documented scope cut, not mid-segment), so
            // "partially fitting" a line here doesn't correspond to
            // anything Paint() can actually render: it draws that line's
            // FULL row count regardless. Now walks backward including only
            // WHOLE lines that still fit alongside pointLine's own (always
            // fully included) rows, stopping before, not mid-way through,
            // one that wouldn't.
            std::size_t newTop      = pointLine;
            std::size_t accumulated = pointLineRows;
            while (newTop > 0) {
                const std::size_t candidate = newTop - 1;
                if (IsLineHidden(candidate)) {
                    newTop = candidate;
                    continue;
                }
                const std::size_t rows = RowsForLine(candidate);
                if (accumulated + rows > visibleLines) {
                    break;
                }
                accumulated += rows;
                newTop = candidate;
            }
            topLine_ = newTop;
        }
    }
    // line-wrap follow-up: every call site here wants "make sure point is
    // visible," full stop -- folding the horizontal half in here means
    // every one of ScrollToShowPoint()'s existing call sites gets it for
    // free, rather than needing a second call added at each of them.
    ScrollToShowPointHorizontally();
}

void BufferView::ScrollToShowPointHorizontally() {
    if (EffectiveWrapLines()) {
        return; // a wrapped line never extends past the viewport width -- nothing to scroll
    }

    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::Rope&   content     = buffer.Content();
    const std::size_t   point       = buffer.Point();
    const std::size_t   pointLine   = content.ByteOffsetToLine(point);
    const std::size_t   lineStart   = content.LineToByteOffset(pointLine);
    const std::size_t   gutterWidth = GutterWidth();
    const Size          sizeNow     = size();
    if (sizeNow.width <= 0) {
        return; // nothing meaningful to clamp against yet (e.g. before the first real layout)
    }
    const int contentWidth = std::max(1, sizeNow.width - static_cast<int>(gutterWidth));

    EnsureLinkCache();
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);

    // Point's true column from the start of the line, unbounded (well,
    // bounded only by the line's own length, not the viewport) -- needed to
    // decide whether leftColumn_ has to move at all, so this can't reuse
    // VisualColumn's own maxColumns-bounded form directly; a pathologically
    // long line still only walks as far as point itself, same cost class as
    // every other per-line scan in this file.
    const std::optional<int> visualCol =
        VisualColumn(content, lineStart, point, std::numeric_limits<int>::max(), lineLinks);
    if (!visualCol) {
        return; // shouldn't happen with an unbounded maxColumns, but a safe no-op
    }

    if (*visualCol < static_cast<int>(leftColumn_)) {
        leftColumn_ = static_cast<std::size_t>(*visualCol);
    }
    else if (*visualCol >= static_cast<int>(leftColumn_) + contentWidth) {
        leftColumn_ = static_cast<std::size_t>(*visualCol - contentWidth + 1);
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

std::size_t BufferView::LeftColumn() const {
    return leftColumn_;
}

void BufferView::SetLeftColumn(std::size_t column) {
    leftColumn_ = column;
}

bool BufferView::EffectiveWrapLines() const {
    return editor::EffectiveWrapLines(activeBuffer_.Get().Path(), mode_);
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
    // subtraction. line-wrap follow-up: VisibleRowCountAtLeast is the
    // fold-AND-wrap-aware "does everything already fit in one viewport"
    // check (was VisibleLineCountBetween, then an exact-sum
    // VisibleRowCountBetween -- an early-exit bounded check now, so this
    // never walks more of a huge document than the viewport itself could
    // need; see that method's own doc comment). Checking "at least
    // visibleLines + 1" is the negation of "the total is <= visibleLines."
    if (!VisibleRowCountAtLeast(rangeStart, rangeEnd, visibleLines + 1)) {
        return rangeStart;
    }
    // line-wrap follow-up: was a partial-credit backward walk
    // (`remaining -= min(remaining, RowsForLine(newTop))`) that could stop
    // mid-line, silently discarding whatever didn't fit -- topLine_ can
    // only ever start at a line's own first row (a documented scope cut,
    // not mid-segment), so "partially fitting" a line here doesn't
    // correspond to anything Paint() can actually render: it draws that
    // line's FULL row count regardless of how much of it "fit" in this
    // walk's own bookkeeping, which could silently push whatever comes
    // after it off the bottom of the viewport entirely. A real, reported
    // bug this fixes: scrolling to the end of a wrapped document could
    // leave its own last lines permanently unreachable, since a wrapped
    // line earlier in the walk could swallow the whole remaining budget
    // without actually being fully shown. Now walks backward including
    // only WHOLE lines that still fit within the budget, stopping before
    // (not mid-way through) one that wouldn't -- except the very first
    // real line considered, which is always included in full even if it
    // alone exceeds visibleLines (matching "always show at least the last
    // line" -- the same guarantee this walk already gave for free back
    // when every line was implicitly exactly 1 row).
    std::size_t newTop      = rangeEnd;
    std::size_t accumulated = 0;
    while (newTop > rangeStart) {
        const std::size_t candidate = newTop - 1;
        if (IsLineHidden(candidate)) {
            newTop = candidate;
            continue;
        }
        const std::size_t rows = RowsForLine(candidate);
        if (accumulated > 0 && accumulated + rows > visibleLines) {
            break;
        }
        accumulated += rows;
        newTop = candidate;
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

void BufferView::SetTaskRunner(editor::tasks::TaskRunner* taskRunner) {
    taskRunner_ = taskRunner;
}

void BufferView::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
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
