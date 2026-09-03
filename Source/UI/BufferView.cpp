#include "BufferView.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

#include "Border.h"
#include "EchoArea.h"
#include "Editor/Acp/AcpConfig.h"
#include "Editor/Bookmark.h"
#include "Editor/BufferSave.h"
#include "Editor/Clipboard.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/DabbrevComplete.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/FuzzyMatch.h"
#include "Editor/HeaderSource.h"
#include "Editor/HighlightSettings.h"
#include "Editor/HugeStructuralWindow.h"
#include "Editor/ImportResolutionConfig.h"
#include "Editor/InlineDiagnostics.h"
#include "Editor/JanetSymbolComplete.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/NextError.h"
#include "Editor/NodeModules.h"
#include "Editor/Org.h"
#include "Editor/OrgCapture.h"
#include "Editor/ProjectAgenda.h"
#include "Editor/ProjectFileOps.h"
#include "Editor/ProjectRegistry.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/ProjectSettings.h"
#include "Editor/ProjectSwitch.h"
#include "Editor/ProjectTree.h"
#include "Editor/ProjectUndo.h"
#include "Editor/RecentFiles.h"
#include "Editor/Rectangle.h"
#include "Editor/RelativeLineNumberSettings.h"
#include "Editor/ScratchPad.h"
#include "Editor/Session.h"
#include "Editor/StickyScroll.h"
#include "Editor/StickyScrollSettings.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/TabWidth.h"
#include "Editor/TestRun/TestResultsBuffer.h"
#include "Editor/TestRun/TestRunConfig.h"
#include "Editor/ToolchainIncludePaths.h"
#include "Editor/Variables.h"
#include "Editor/Vcs/DiffPatch.h"
#include "Editor/Vim/VimSettings.h"
#include "Editor/WhichKeySettings.h"
#include "Editor/WhitespaceSettings.h"
#include "Editor/WrapOverrides.h"
#include "Janet/Environment.h"
#include "KeyTranslation.h"
#include "Text/BinaryDetect.h"
#include "Text/Utf8.h"
#include "UI/ThemeFile.h"
#include "UI/ThemeRegistry.h"

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

    // Double/triple-click word/line selection: same window ProjectSidebar's
    // own double-click-to-open uses.
    constexpr std::chrono::milliseconds kDoubleClickWindow{400};

    // ASCII alphanumeric + underscore, deliberately not Unicode-aware --
    // mirrors Buffer.cpp's own (private) IsWordCodepoint used by
    // MoveForwardWord/MoveBackwardWord.
    bool IsWordCodepointForClick(char32_t codepoint) {
        return (codepoint >= U'a' && codepoint <= U'z') || (codepoint >= U'A' && codepoint <= U'Z') ||
               (codepoint >= U'0' && codepoint <= U'9') || codepoint == U'_';
    }

    // Expands a click offset to the bounds of the contiguous word/non-word
    // run it falls in -- e.g. double-clicking mid-identifier selects the
    // whole identifier, double-clicking mid-whitespace selects the whole
    // run of whitespace.
    std::pair<std::size_t, std::size_t> WordBoundsAtOffset(const text::ITextStorage& content, std::size_t offset) {
        const std::size_t total = content.ByteLength();
        if (total == 0) {
            return {0, 0};
        }
        const std::size_t probe = offset < total ? offset : content.PreviousCodepointBoundary(offset);
        const bool        isWord = IsWordCodepointForClick(content.CodepointAt(probe).codepoint);

        std::size_t start = probe;
        while (start > 0) {
            const std::size_t previous = content.PreviousCodepointBoundary(start);
            if (IsWordCodepointForClick(content.CodepointAt(previous).codepoint) != isWord) {
                break;
            }
            start = previous;
        }
        std::size_t end = probe;
        while (end < total && IsWordCodepointForClick(content.CodepointAt(end).codepoint) == isWord) {
            end = content.NextCodepointBoundary(end);
        }
        return {start, end};
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

    // generic-popup follow-up (Phase 3): builds one popup row per
    // candidate, bounded to kMaxPopupRows rows rather than an unbounded
    // dump -- grows a window containing `selected` outward, forward first,
    // only as far as kMaxPopupRows allows. scroll-indicator-count follow-up:
    // an "above"/"below" boundary row appears on whichever side(s) still
    // have hidden candidates, each with its own live count -- a single
    // trailing "+K more" (the total hidden count) never changed as the
    // window scrolled, which read as stuck/broken rather than reflecting
    // where you actually were in the list. These are extra rows on top of
    // kMaxPopupRows, not carved out of it, so real candidates never lose a
    // visible slot to make room for them -- the popup just grows up to two
    // rows taller when both are showing, still comfortably inside the
    // placement clamp in main.cpp (kMaxPopupRows + 2 boundary rows + 2
    // border rows == 14, exactly candidatePopup's own height cap).
    // Selection still "scrolls" the same way it always did as arrow keys
    // move it.
    constexpr std::size_t kMaxPopupRows = 10;

    // select-theme-current-row follow-up: the synthetic first candidate
    // StartInteractiveSession's SelectTheme case prepends to
    // selectThemeCandidates_ -- picking it (by opening on it, arrowing back
    // to it, or committing it) means "leave the live theme exactly as it
    // is," resolved against themeBeforePreview_ rather than ThemeByName(),
    // which is what makes it safe: ThemeByName() returns the bare registry
    // theme with none of init.janet's own (ned/theme-set ...) overrides
    // reapplied, so treating "the theme already active" as just another
    // named lookup would strip those overrides the moment it's (re)selected
    // -- confirmed live as the "doesn't apply correctly across the entire
    // screen" symptom. See ApplySelectedThemePreview/HandleSelectThemeKey's
    // own checks against this same constant.
    constexpr std::string_view kCurrentThemeLabel = "Current theme";

    // dropdown-path-completion follow-up: optional `display` transform lets a
    // caller show something other than the raw candidate string per row (a
    // path candidate masked down to its last segment) while `ranked` itself
    // stays the real value Enter/Tab resolve against -- every pre-existing
    // caller passes nullptr and is unaffected.
    ListPopupModel BuildFuzzyCandidatePopupModel(const std::string& title, const std::vector<std::string>& ranked,
                                                 std::size_t selected,
                                                 const std::function<std::string(const std::string&)>& display = nullptr) {
        ListPopupModel model;
        model.title = title;
        if (ranked.empty()) {
            return model;
        }
        selected = std::min(selected, ranked.size() - 1);

        std::size_t       windowStart = selected;
        std::size_t       windowEnd   = selected + 1;
        const std::size_t maxRows     = std::min(kMaxPopupRows, ranked.size());
        while (windowEnd - windowStart < maxRows) {
            if (windowEnd < ranked.size()) {
                ++windowEnd;
            }
            else if (windowStart > 0) {
                --windowStart;
            }
            else {
                break;
            }
        }

        model.rows.reserve(windowEnd - windowStart + 2);
        if (windowStart > 0) {
            model.rows.push_back({.main = "↑ " + std::to_string(windowStart) + " more above"});
        }
        for (std::size_t i = windowStart; i < windowEnd; ++i) {
            model.rows.push_back({.main = display ? display(ranked[i]) : ranked[i]});
        }
        model.selectedIndex = (selected - windowStart) + (windowStart > 0 ? 1 : 0);

        const std::size_t hiddenBelow = ranked.size() - windowEnd;
        if (hiddenBelow > 0) {
            model.rows.push_back({.main = "↓ " + std::to_string(hiddenBelow) + " more below"});
        }
        return model;
    }

    // dropdown-path-completion follow-up: turns an accumulated
    // text::CompleteFilePath candidate ("src/editor/" or
    // "src/editor/BufferView.cpp") into just its last segment ("editor/" or
    // "BufferView.cpp"), independent of how deep the accumulated prefix is --
    // the `display` transform RefreshPathCompletionPopup passes to
    // BuildFuzzyCandidatePopupModel for FindFile/OpenProjectPath rows.
    std::string MaskPathCandidateToLastSegment(const std::string& candidate) {
        const bool        isDirectory = candidate.ends_with('/');
        const std::string trimmed     = isDirectory ? candidate.substr(0, candidate.size() - 1) : candidate;
        std::string        segment     = std::filesystem::path(trimmed).filename().string();
        if (isDirectory) {
            segment += '/';
        }
        return segment;
    }

    // symbol-search follow-up. One display line for a SymbolResult, used as
    // both the fuzzy-filter candidate string (LspGotoSymbol) and the ranked
    // row shown as-is (LspWorkspaceSymbol, already server-ranked). The line
    // number is always folded in -- what keeps two candidates with the same
    // name/container from producing identical labels (documentSymbolLabels_/
    // workspaceSymbolLabels_' own doc comments explain why that matters).
    // includePath is only true for workspace/symbol results, which span
    // multiple files and need one to disambiguate; a document-symbol result
    // is always the current buffer, so a repeated path would be noise.
    std::string BuildSymbolLabel(const editor::lsp::LspManager::SymbolResult& symbol, bool includePath) {
        std::string label(editor::lsp::SymbolKindLabel(symbol.kind));
        label += " ";
        label += symbol.name;
        if (!symbol.containerName.empty()) {
            label += "  in " + symbol.containerName;
        }
        if (includePath) {
            std::error_code             ec;
            const std::filesystem::path relative = std::filesystem::relative(symbol.path, editor::ProjectRoot(), ec);
            label += "  — " + ((!ec && !relative.empty()) ? relative.string() : symbol.path.string());
        }
        label += ":" + std::to_string(symbol.position.line + 1); // LSP is 0-indexed, displayed 1-indexed like every other line reference here
        return label;
    }

    // call/type-hierarchy follow-up: one TreeRow::label, BuildSymbolLabel's
    // own "kind name — path:line" shape reused verbatim (SymbolKindLabel
    // takes the same raw LSP SymbolKind vocabulary both HierarchyItem::kind
    // and SymbolEntry::kind use) -- a hierarchy row and a symbol-picker row
    // are answering the same underlying question ("what is this, and
    // where"), so they read the same way. containerName has no equivalent
    // here (HierarchyItem carries none), so this is the includePath=true
    // branch of BuildSymbolLabel with the containerName segment dropped
    // rather than a parallel near-duplicate.
    std::string BuildHierarchyRowLabel(const editor::lsp::LspManager::ResolvedHierarchyItem& resolved) {
        std::string label(editor::lsp::SymbolKindLabel(resolved.item.kind));
        label += " ";
        label += resolved.item.name;
        std::error_code             ec;
        const std::filesystem::path relative = std::filesystem::relative(resolved.path, editor::ProjectRoot(), ec);
        label += "  — " + ((!ec && !relative.empty()) ? relative.string() : resolved.path.string());
        label += ":" + std::to_string(resolved.item.position.line + 1);
        return label;
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
    // specifically distinct from TabBar's ‹› so a binary placeholder never
    // reads as one of those instead.
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

    // Whitespace-visualization follow-up: overwrites a leading-whitespace
    // cell that lands exactly on an indent-width column boundary -- see the
    // render loop's own use below.
    constexpr char32_t kIndentGuide = U'│';

    // Depth-colorized-indent-guides follow-up: which color a guide glyph at
    // a given display column gets. displayColumn is always a positive
    // multiple of tabWidth at every real call site (the callers' own
    // "displayColumn > 0 && displayColumn % tabWidth == 0" guard), so
    // displayColumn / tabWidth is that guide's 1-indexed nesting level --
    // pure column arithmetic, no fold/tree-sitter data needed, which is what
    // lets this apply uniformly to every mode, including ones with no fold
    // query at all (plain text, JSON without json-folds.scm's block shape
    // matching visual indentation, ...). Falls back to the flat
    // indentGuideForeground when the setting's off or a hand-built Theme
    // left the palette empty.
    Color IndentGuideColor(const Theme& theme, int displayColumn, int tabWidth) {
        if (!editor::IndentGuideDepthColorsEnabled() || theme.indentGuideDepthPalette.empty() || tabWidth <= 0) {
            return theme.indentGuideForeground;
        }
        const std::size_t level = static_cast<std::size_t>(displayColumn / tabWidth - 1);
        return theme.indentGuideDepthPalette[level % theme.indentGuideDepthPalette.size()];
    }

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
    // scan the simplest correct option, same as SpansForLine/SpanAtOffset's
    // own approach for highlight spans.
    const RenderedLink* LinkStartingAt(const std::vector<RenderedLink>& links, std::size_t offset) {
        for (const RenderedLink& link : links) {
            if (link.startByte == offset) {
                return &link;
            }
        }
        return nullptr;
    }

    // inlayHint follow-up. A LspManager::ResolvedInlayHint (byteOffset +
    // label) filtered down to just [lineStart, lineEnd) -- same "filter
    // once per line, consult per-codepoint" shape LinksForLine/
    // SpansForLine already establish. Unlike a RenderedLink, this never
    // consumes/replaces real bytes -- see InlayHintStartingAt's own doc
    // comment for how a render-loop consumer uses it.
    struct RenderedInlayHint {
        std::size_t byteOffset;
        std::string label;
    };

    std::vector<RenderedInlayHint> InlayHintsForLine(const std::vector<editor::lsp::LspManager::ResolvedInlayHint>& hints,
                                                     std::size_t lineStart, std::size_t lineEnd) {
        std::vector<RenderedInlayHint> rendered;
        for (const editor::lsp::LspManager::ResolvedInlayHint& hint : hints) {
            if (hint.byteOffset >= lineStart && hint.byteOffset < lineEnd) {
                rendered.push_back(RenderedInlayHint{.byteOffset = hint.byteOffset, .label = hint.label});
            }
        }
        return rendered;
    }

    // Finds the RenderedInlayHint (if any) anchored exactly at offset --
    // same linear-scan-over-a-small-per-line-list shape LinkStartingAt
    // uses. Unlike LinkStartingAt, a caller finding one here does NOT skip
    // past offset -- the hint renders as extra cells *before* the real
    // character still at offset, which keeps rendering normally right
    // after (a hint is virtual text alongside real content, not a
    // replacement for it).
    const RenderedInlayHint* InlayHintStartingAt(const std::vector<RenderedInlayHint>& hints, std::size_t offset) {
        for (const RenderedInlayHint& hint : hints) {
            if (hint.byteOffset == offset) {
                return &hint;
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
    std::optional<int> VisualColumn(const text::ITextStorage& content, std::size_t lineStart, std::size_t byteOffset,
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

    std::size_t ByteOffsetForColumnInLine(const text::ITextStorage& content, std::size_t lineStart, std::size_t lineEnd,
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
    std::vector<WrapSegment> ComputeWrapSegments(const text::ITextStorage& content, std::size_t lineStart, std::size_t lineEnd,
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
    // row from Paint(), *not* once per rendered codepoint, so SpanAtOffset
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

    // Finds the winning HighlightSpan at byteOffset from a (typically small,
    // already line-filtered -- see SpansForLine) HighlightSpan list; a
    // synthetic Default/kNoCapture span if none covers it. Spans overlapping
    // the same byte resolve in `spans`' own order, later wins -- see
    // HighlightSpan's own doc comment in Mode.h for why. Returns the whole
    // span rather than just its SyntaxClass (exhaustive-highlighting
    // follow-up) so the render loop can reach the winning capture's own
    // per-capture styling too; call sites that only care about the class
    // read .syntaxClass and lose nothing.
    editor::HighlightSpan SpanAtOffset(const std::vector<editor::HighlightSpan>& spans, std::size_t byteOffset) {
        editor::HighlightSpan winner{.startByte = 0, .endByte = 0, .syntaxClass = editor::SyntaxClass::Default};
        for (const editor::HighlightSpan& span : spans) {
            if (span.startByte <= byteOffset && byteOffset < span.endByte) {
                winner = span;
            }
        }
        return winner;
    }

    // hover/completion follow-up: byte offset where the ASCII word/
    // identifier token immediately before point begins (an alnum/underscore
    // run) -- shared by the auto-completion suppression heuristic (rejecting
    // a purely numeric token) and completion-insert-suffix computation (the
    // already-typed prefix to subtract from a completion item's own
    // insertText). Deliberately ASCII-only (matches Buffer's own word-motion
    // classification), so the returned [start, point) range is guaranteed
    // single-byte-per-codepoint -- safe to treat as raw bytes.
    // Factored out of WordPrefixStart below so completion-auto-trigger-gate
    // follow-up's own trigger check can share the exact same ASCII-only word
    // definition rather than drifting from it.
    bool IsWordCodepoint(char32_t codepoint) {
        return (codepoint < 0x80) && (std::isalnum(static_cast<unsigned char>(codepoint)) != 0 || codepoint == U'_');
    }

    std::size_t WordPrefixStart(const text::ITextStorage& content, std::size_t point) {
        std::size_t start = point;
        while (start > 0) {
            const std::size_t prior = content.PreviousCodepointBoundary(start);
            if (!IsWordCodepoint(content.CodepointAt(prior).codepoint)) {
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
                       editor::PromptHistory& promptHistory, text::BufferList& bufferList, editor::Dispatcher& dispatcher,
                       std::string& statusMessage, const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), killRing_(killRing), registers_(registers),
                                                                                                   promptHistory_(promptHistory), bufferList_(bufferList),
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
    // session-persistence slice 1: this seeding is also exactly why the
    // EnsureTopLineValidForActiveBuffer seam can never restore a stored
    // viewport for the buffer a pane STARTS on (the seam only fires on a
    // later switch) -- a real, user-reported gap: relaunching ned on a file
    // restored point but left the view at the top. So the initial buffer's
    // stored topLine is applied right here instead. Clamped by pointLine,
    // not MaxTopLine() (size() is still 0x0 at construction, so MaxTopLine
    // is meaningless): a consistently recorded place always has
    // pointLine >= topLine, so the min only ever bites when the file shrank
    // outside ned and point itself got clamped -- pinning point's own line
    // to the top row is the sane view for that case. A pre-first-Paint
    // wheel/scroll-bar event now adjusts from the restored position rather
    // than from 0, which is the same "don't discard real scroll state"
    // intent the seeding comment above describes.
    if (const auto place = editor::StoredFilePlaceFor(activeBuffer_.Get()); place && place->topLine) {
        const text::Buffer& buffer    = activeBuffer_.Get();
        const std::size_t   pointLine = buffer.Content().ByteOffsetToLine(buffer.Point());
        topLine_                      = std::min(*place->topLine, pointLine);
    }
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
    context.taskRunner  = taskRunner_;
    context.testRunner  = testRunner_;
    context.projectUndo = projectUndo_;
    return context;
}

void BufferView::SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler) {
    onWindowRequest_ = std::move(handler);
}

void BufferView::SetSplitResizeQuery(std::function<bool()> query) {
    splitResizeQuery_ = std::move(query);
}

void BufferView::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void BufferView::SetOnTerminalToggle(std::function<void()> handler) {
    onTerminalToggle_ = std::move(handler);
}

void BufferView::SetOnAcpPanelToggle(std::function<void()> handler) {
    onAcpPanelToggle_ = std::move(handler);
}

void BufferView::SetOnAcpRewindRequest(std::function<void()> handler) {
    onAcpRewindRequest_ = std::move(handler);
}

void BufferView::SetOnDapConsoleToggle(std::function<void()> handler) {
    onDapConsoleToggle_ = std::move(handler);
}

void BufferView::SetOnBufferListToggle(std::function<void()> handler) {
    onBufferListToggle_ = std::move(handler);
}

void BufferView::SetOnActiveBufferChanged(std::function<void(text::Buffer&)> handler) {
    onActiveBufferChanged_ = std::move(handler);
}

void BufferView::SetOnPrefixHintChanged(std::function<void(std::optional<WhichKeyHint>)> handler) {
    onPrefixHintChanged_ = std::move(handler);
}

void BufferView::SetOnCandidatesChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onCandidatesChanged_ = std::move(handler);
}

void BufferView::SetOnCompletionChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onCompletionChanged_ = std::move(handler);
}

void BufferView::ClearBufferCaches(text::Buffer& buffer) {
    highlightCacheByBuffer_.erase(&buffer);
    embeddedDocumentCacheByBuffer_.erase(&buffer);
    foldableBlocksCacheByBuffer_.erase(&buffer);
    if (highlightCacheBuffer_ == &buffer) {
        highlightCacheBuffer_ = nullptr;
        highlightCacheSpans_.clear();
    }
    if (foldableBlocksCacheBuffer_ == &buffer) {
        foldableBlocksCacheBuffer_ = nullptr;
        foldableBlocksCache_.clear();
    }
}

std::pair<std::size_t, std::size_t> BufferView::HugeStructuralWindow(const text::ITextStorage& content) const {
    const std::size_t byteLength = content.ByteLength();
    if (!content.IsHuge()) {
        return {0, byteLength};
    }

    const std::size_t margin    = editor::HugeStructuralWindowBytes();
    const std::size_t lineCount = content.LineCount();
    const std::size_t lastLine  = lineCount > 0 ? lineCount - 1 : 0;
    const std::size_t topLine   = std::min(topLine_, lastLine);
    const std::size_t viewportHeight = size().height > 0 ? static_cast<std::size_t>(size().height) : 1;
    const std::size_t bottomLine     = std::min(topLine + viewportHeight, lastLine);

    const std::size_t viewportStartByte = content.LineToByteOffset(topLine);
    const std::size_t viewportEndByte   = std::min(content.LineToByteOffset(bottomLine) + 1, byteLength);

    // Snapped to the start of whatever line it lands in, not a raw byte
    // subtraction -- confirmed empirically (not assumed) that feeding
    // tree-sitter a substring beginning mid-line/mid-token can desync its
    // parse badly enough to misidentify a spurious definition right at the
    // cut, or silently miss a real one shortly after it, well beyond the
    // "truncated at the tail" class of imprecision the trailing edge alone
    // has to worry about. A clean line start costs at most one extra line
    // of margin and gives the parser real, syntactically valid context from
    // its very first byte.
    const std::size_t rawWindowStart = viewportStartByte > margin ? viewportStartByte - margin : 0;
    const std::size_t windowStart    = content.LineToByteOffset(content.ByteOffsetToLine(rawWindowStart));
    const std::size_t windowEnd      = byteLength - viewportEndByte > margin ? viewportEndByte + margin : byteLength;
    return {windowStart, windowEnd};
}

void BufferView::EnsureFoldableBlocksCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!FoldGutterActive()) {
        foldableBlocksCache_.clear();
        foldableBlocksCacheBuffer_     = &buffer;
        foldableBlocksCacheGeneration_ = buffer.ContentGeneration();
        return;
    }

    const text::ITextStorage&            content                = buffer.Content();
    const bool                           huge                   = content.IsHuge();
    const auto [windowStart, windowEnd]                         = HugeStructuralWindow(content);

    if (foldableBlocksCacheBuffer_ == &buffer && foldableBlocksCacheGeneration_ == buffer.ContentGeneration() &&
        foldableBlocksCacheWindowStart_ == windowStart && foldableBlocksCacheWindowEnd_ == windowEnd) {
        return;
    }

    // per-buffer-highlight-cache follow-up: persists across a buffer
    // switch -- see foldableBlocksCacheByBuffer_'s own doc comment in
    // BufferView.h, and highlightCacheByBuffer_'s for the full reasoning
    // this mirrors.
    const auto it = foldableBlocksCacheByBuffer_.find(&buffer);
    if (it == foldableBlocksCacheByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
        it->second.modeName != mode_.name || it->second.windowStart != windowStart || it->second.windowEnd != windowEnd) {
        FoldableBlocksCacheEntry entry;
        // huge-file-structural-gutters follow-up: a huge buffer feeds
        // mode_.fold a bounded window (content.Substring) instead of the
        // whole document.
        entry.ranges = huge ? editor::codefold::FoldableBlocks(mode_, content.Substring(windowStart, windowEnd - windowStart))
                             : editor::codefold::FoldableBlocks(mode_, buffer.Text());
        if (huge) {
            // A block whose real closing brace lies beyond the window isn't
            // simply "not found" -- tree-sitter still emits a
            // compound_statement node via error recovery for the unclosed
            // "{", extending all the way to wherever the fed substring runs
            // out, and c-folds.scm's plain "(compound_statement) @fold"
            // captures it regardless (confirmed empirically, not assumed:
            // this is exactly what a first version of this fix's own test
            // caught). Reporting that truncated range as the block's real
            // extent would fold to an arbitrary window-edge line, not the
            // block's actual close -- worse than not finding it at all. Drop
            // any range whose end reaches the substring's own edge, the same
            // "don't trust a result abutting the window's own tail unless
            // the window reached the real document end" rule
            // Editor/HugeRegexScan.h already established for search.
            const std::size_t windowLength = windowEnd - windowStart;
            const bool        reachedDocumentEnd = windowEnd >= content.ByteLength();
            std::erase_if(entry.ranges, [&](const auto& range) { return !reachedDocumentEnd && range.second >= windowLength; });
            for (auto& [start, end] : entry.ranges) {
                start += windowStart;
                end += windowStart;
            }
        }
        entry.contentGeneration = buffer.ContentGeneration();
        entry.modeName          = mode_.name;
        entry.windowStart       = windowStart;
        entry.windowEnd         = windowEnd;
        foldableBlocksCache_    = entry.ranges;
        foldableBlocksCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
    }
    else {
        foldableBlocksCache_ = it->second.ranges;
    }
    foldableBlocksCacheBuffer_      = &buffer;
    foldableBlocksCacheGeneration_  = buffer.ContentGeneration();
    foldableBlocksCacheWindowStart_ = windowStart;
    foldableBlocksCacheWindowEnd_   = windowEnd;
}

void BufferView::EnsureEmbeddedDocumentCache() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!mode_.embeddedRegions) {
        embeddedDocumentCacheByBuffer_.erase(&buffer); // nothing to cache -- mode has no embedded regions at all
        return;
    }

    const auto it = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it != embeddedDocumentCacheByBuffer_.end() && it->second.contentGeneration == buffer.ContentGeneration() &&
        it->second.modeName == mode_.name) {
        return; // already current
    }

    EmbeddedDocumentCacheEntry entry;
    entry.documents         = editor::BuildEmbeddedDocuments(mode_, buffer.Text());
    entry.contentGeneration = buffer.ContentGeneration();
    entry.modeName          = mode_.name;
    embeddedDocumentCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
}

std::optional<std::string> BufferView::EmbeddedLanguageAtPoint() {
    EnsureEmbeddedDocumentCache();
    text::Buffer& buffer = activeBuffer_.Get();
    const auto    it     = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it == embeddedDocumentCacheByBuffer_.end()) {
        return std::nullopt;
    }
    return editor::EmbeddedLanguageAtByteOffset(it->second.documents, buffer.Point());
}

std::string BufferView::ResolvedLspServerKey(std::size_t byteOffset) {
    EnsureEmbeddedDocumentCache();
    text::Buffer& buffer = activeBuffer_.Get();
    const auto    it     = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it == embeddedDocumentCacheByBuffer_.end()) {
        return {};
    }
    if (const std::optional<std::string> language = editor::EmbeddedLanguageAtByteOffset(it->second.documents, byteOffset)) {
        return *language;
    }
    return {};
}

void BufferView::EnsureFoldGutterCache() const {
    EnsureFoldableBlocksCache();
    text::Buffer& buffer = activeBuffer_.Get();

    if (foldGutterCacheBuffer_ == &buffer && foldGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        foldGutterCacheFoldGeneration_ == buffer.FoldGeneration() &&
        foldGutterCacheWindowStart_ == foldableBlocksCacheWindowStart_ && foldGutterCacheWindowEnd_ == foldableBlocksCacheWindowEnd_) {
        return;
    }

    foldGutterEntries_.clear();
    for (auto& column : foldGutterLineRangesByColumn_) {
        column.clear();
    }

    if (!foldableBlocksCache_.empty()) {
        const text::ITextStorage& content = buffer.Content();
        const auto        regions = editor::codefold::FoldRegionsWithDepth(foldableBlocksCache_);

        foldGutterEntries_.reserve(regions.size());
        for (const auto& region : regions) {
            if (region.depth >= kMaxFoldDepthColumns) {
                // Deeper than the gutter has columns for: draw nothing at
                // all, rather than the previous clamp-into-the-last-column
                // behavior, which piled every deeper block's ⊞/⊟ and guide
                // line on top of the real depth-3 block's own -- visual
                // noise, and an ambiguous click target (a column-3 click
                // could land on whichever block happened to stack there).
                // The block itself stays fully foldable via
                // code-fold-toggle (M-x/keyboard path reads
                // FoldableBlocks, not these entries), and a deep block
                // collapsed that way still hides its lines and shows the
                // content-side ellipsis -- only the gutter affordance is
                // depth-capped.
                continue;
            }
            const int         column     = region.depth;
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
            if (!foldGutterEntries_.empty() && foldGutterEntries_.back().headerLine == headerLine) {
                // Only the outermost block opening on a line gets a gutter
                // affordance (lisp-nesting fix, clojure-and-jank follow-up:
                // `:profiles {:dev {:dependencies [...` used to stack three
                // ⊞/⊟ columns on one row). Two multi-line blocks sharing a
                // header line are necessarily nested -- a sibling can't
                // start before the previous multi-line block's closing line
                // -- and regions arrive sorted by startByte, so the first
                // entry added for a line is always the outermost; everything
                // after it here is an inner block whose fold the outer one's
                // covers. ToggleFoldAtLine's outermost-wins rule (CodeFold.h)
                // is the keyboard half of this same decision.
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
    foldGutterCacheWindowStart_       = foldableBlocksCacheWindowStart_;
    foldGutterCacheWindowEnd_         = foldableBlocksCacheWindowEnd_;
}

void BufferView::EnsureUnsavedChangeCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (unsavedChangeCacheBuffer_ == &buffer && unsavedChangeCacheContentGeneration_ == buffer.ContentGeneration() &&
        unsavedChangeCacheGeneration_ == buffer.UnsavedChangeGeneration()) {
        return;
    }

    unsavedChangeLineRanges_.clear();
    const text::ITextStorage& content = buffer.Content();
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

    // The one severity -> {glyph, bold} mapping shared by the diagnostics
    // gutter column and the inline annotation rows (inline-diagnostics
    // follow-up), so the two icon vocabularies can never drift apart. The
    // matching theme color is severity-keyed too but needs the Theme, so
    // callers pair this with their own theme_.diagnostic* lookup.
    struct DiagnosticGlyph {
        const char* glyph;
        bool        bold;
    };
    DiagnosticGlyph DiagnosticGlyphFor(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return {"✗", true}; // ✗ BALLOT X -- the universal "failure" cross
            case text::Buffer::Diagnostic::Severity::Warning:
                return {"▲", true}; // ▲ -- the warning-triangle convention
            case text::Buffer::Diagnostic::Severity::Information:
                return {"i", true}; // bold i -- reads "info" directly, no circled-i needed
            case text::Buffer::Diagnostic::Severity::Hint:
                return {"·", false}; // · MIDDLE DOT -- deliberately subtle, matching a hint's low urgency
        }
        return {" ", false}; // unreachable, same convention as DiagnosticSeverityRank above
    }

    // gutter-symbol-kind follow-up: one glyph per SymbolKind bucket, the
    // same "small alphabet of clear, single-character indicators" convention
    // DiagnosticGlyphFor above establishes -- plain Unicode, not Nerd Font
    // codicons, so it renders correctly in any UTF-8 terminal without a
    // patched font (matching ✗/▲/i/· above, and this project's own general
    // avoidance of font-dependent glyphs elsewhere in the gutter). Color
    // comes from the matching SyntaxClass (editor::SyntaxClassFor), not from
    // here -- this only picks the shape.
    const char* SymbolGlyphFor(editor::SymbolKind kind) {
        switch (kind) {
            case editor::SymbolKind::Callable:
                return "ƒ"; // LATIN SMALL LETTER F WITH HOOK -- the standard "function" glyph
            case editor::SymbolKind::TypeLike:
                return "◇"; // WHITE DIAMOND -- a class/interface/type/module definition
            case editor::SymbolKind::Data:
                return "="; // a constant/variable-like definition
            case editor::SymbolKind::Namespace:
                return "§"; // SECTION SIGN -- a namespace definition, distinct from TypeLike's ◇
        }
        return " "; // unreachable, same convention as DiagnosticGlyphFor above
    }

    // completion-popup follow-up: buckets a raw LSP CompletionItemKind
    // (spec section 3.17.2.3, 1-25) down onto the gutter's own three-bucket
    // SymbolKind wherever the mapping is a natural fit, reusing
    // SymbolGlyphFor/editor::SyntaxClassFor/Theme::BrushFor entirely
    // unchanged for the glyph and its color -- deliberately NOT a fourth
    // SymbolKind enumerator: that type's own doc comment scopes it to the
    // gutter's tree-sitter-tag-derived "definition site" landmarks, and a
    // completion item (a keyword, a snippet, a file path, ...) often isn't
    // one at all. Everything outside the three matched ranges (Text,
    // Keyword, Snippet, Color, File, Folder, Unit, Operator, Event,
    // Reference, Value) returns nullopt -- rendered as a dim, generic glyph
    // by the caller instead of stretching this mapping to cover every LSP
    // kind.
    std::optional<editor::SymbolKind> CompletionKindBucket(int lspKind) {
        switch (lspKind) {
            case 2: // Method
            case 3: // Function
            case 4: // Constructor
                return editor::SymbolKind::Callable;
            case 7:  // Class
            case 8:  // Interface
            case 9:  // Module
            case 22: // Struct
            case 13: // Enum
            case 25: // TypeParameter
                return editor::SymbolKind::TypeLike;
            case 5:  // Field
            case 6:  // Variable
            case 10: // Property
            case 11: // Unit
            case 12: // Value
            case 20: // EnumMember
            case 21: // Constant
                return editor::SymbolKind::Data;
            default:
                return std::nullopt;
        }
    }

    // test-runner integration: the per-test gutter mark. Colors are the
    // diff gutter's own bare Palette16 constants (not Theme fields) --
    // see the diff-column paint block's comment for that precedent.
    const char* TestGlyphFor(editor::testrun::TestResult::Status status) {
        switch (status) {
            case editor::testrun::TestResult::Status::Passed:
                return "✓"; // CHECK MARK
            case editor::testrun::TestResult::Status::Failed:
                return "✗"; // BALLOT X, the diagnostic column's own error glyph
            case editor::testrun::TestResult::Status::Skipped:
                return "−"; // MINUS SIGN
        }
        return " "; // unreachable, same convention as DiagnosticGlyphFor above
    }

    Color TestStatusColor(editor::testrun::TestResult::Status status) {
        switch (status) {
            case editor::testrun::TestResult::Status::Passed:
                return Color::BrightGreen;
            case editor::testrun::TestResult::Status::Failed:
                return Color::BrightRed;
            case editor::testrun::TestResult::Status::Skipped:
                return Color::BrightYellow;
        }
        return Color::BrightGreen; // unreachable
    }

    Color DiagnosticSeverityColor(const Theme& theme, text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return theme.diagnosticError;
            case text::Buffer::Diagnostic::Severity::Warning:
                return theme.diagnosticWarning;
            case text::Buffer::Diagnostic::Severity::Information:
                return theme.diagnosticInformation;
            case text::Buffer::Diagnostic::Severity::Hint:
                return theme.diagnosticHint;
        }
        return theme.diagnosticInformation; // unreachable, same convention as DiagnosticSeverityRank above
    }

    // VCS blame gutter: parses a plugin-supplied date string (expected
    // "YYYY-MM-DD" -- what git's own --date=short produces, and what
    // vcs-git.janet's log-argv/blame parsing actually emits) into an
    // approximate age in days against "now," clamped into
    // [0, kBlameMaxAgeDays] and interpolated between a bright (recent) and
    // dim (old) color -- a directly computed Color rather than routed
    // through Theme::BrushFor(SyntaxClass), matching the diagnostic
    // gutter's own bypass of SyntaxClass for the same reason (age-based
    // blame coloring isn't a tree-sitter capture category). Any
    // unparseable date (a plugin using a different format, or a genuinely
    // empty field) degrades to the oldest/dimmest color rather than
    // throwing -- this is purely cosmetic, never load-bearing.
    Color BlameHashColor(const std::string& date) {
        constexpr int kBlameMaxAgeDays = 365;

        std::istringstream    stream(date);
        std::chrono::sys_days parsed;
        stream >> std::chrono::parse("%Y-%m-%d", parsed);
        if (stream.fail()) {
            return Color::BrightBlack;
        }

        const auto  now     = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        const auto  ageDays = std::chrono::duration_cast<std::chrono::days>(now - parsed).count();
        const float t       = std::clamp(static_cast<float>(ageDays) / static_cast<float>(kBlameMaxAgeDays), 0.0f, 1.0f);
        return Color::Interpolate(t, Color::BrightCyan, Color::BrightBlack);
    }

} // namespace

void BufferView::EnsureDiagnosticGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (diagnosticGutterCacheBuffer_ == &buffer && diagnosticGutterCacheGeneration_ == buffer.DiagnosticsGeneration()) {
        return;
    }

    diagnosticLineSeverities_.clear();
    const text::ITextStorage& content = buffer.Content();
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

void BufferView::EnsureSymbolMarkersCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    // Eligibility gate -- mirrors FoldGutterActive's own mode_.fold/
    // ReadOnly() reasoning (a real query run against a synthesized
    // "path:line: text" results buffer produces meaningless markers, not an
    // empty result). Stamped as up to date even when ineligible so a repeat
    // call this same frame/buffer stays a cheap no-op.
    if (!mode_.symbolKind || buffer.ReadOnly()) {
        symbolMarkersCache_.clear();
        symbolMarkersCacheBuffer_            = &buffer;
        symbolMarkersCacheContentGeneration_ = buffer.ContentGeneration();
        return;
    }

    const text::ITextStorage& content  = buffer.Content();
    const bool                huge     = content.IsHuge();
    const auto [windowStart, windowEnd] = HugeStructuralWindow(content);

    if (symbolMarkersCacheBuffer_ == &buffer && symbolMarkersCacheContentGeneration_ == buffer.ContentGeneration() &&
        symbolMarkersCacheWindowStart_ == windowStart && symbolMarkersCacheWindowEnd_ == windowEnd) {
        return;
    }

    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // mode_.symbolKind a bounded window instead of the whole document --
    // both startByte and endByte are then window-relative, remapped back to
    // absolute buffer coordinates (+= windowStart) here so every consumer
    // (the gutter below, sticky scroll) can treat this cache's coordinates
    // uniformly regardless of buffer size.
    symbolMarkersCache_ =
        huge ? mode_.symbolKind(content.Substring(windowStart, windowEnd - windowStart)) : mode_.symbolKind(buffer.Text());
    if (huge) {
        for (editor::SymbolMarker& marker : symbolMarkersCache_) {
            marker.startByte += windowStart;
            marker.endByte += windowStart;
        }
    }
    symbolMarkersCacheBuffer_            = &buffer;
    symbolMarkersCacheContentGeneration_ = buffer.ContentGeneration();
    symbolMarkersCacheWindowStart_       = windowStart;
    symbolMarkersCacheWindowEnd_         = windowEnd;
}

void BufferView::EnsureSymbolGutterCache() const {
    EnsureSymbolMarkersCache();
    text::Buffer& buffer = activeBuffer_.Get();

    if (symbolGutterCacheBuffer_ == &buffer && symbolGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        symbolGutterCacheWindowStart_ == symbolMarkersCacheWindowStart_ &&
        symbolGutterCacheWindowEnd_ == symbolMarkersCacheWindowEnd_) {
        return;
    }

    // symbolMarkersCache_ arrives sorted by startByte (Mode.cpp's own
    // closure) -- collapsing to one entry per line via a plain overwrite in
    // that order keeps the LAST (highest-byte-offset) marker on a line that
    // somehow has more than one, the same "later wins" convention
    // HighlightSpan's own doc comment establishes elsewhere in this file.
    const text::ITextStorage&                           content = buffer.Content();
    std::unordered_map<std::size_t, editor::SymbolKind> kindByLine;
    for (const editor::SymbolMarker& marker : symbolMarkersCache_) {
        kindByLine[content.ByteOffsetToLine(marker.startByte)] = marker.kind;
    }
    symbolGutterLineKinds_.assign(kindByLine.begin(), kindByLine.end());
    std::sort(symbolGutterLineKinds_.begin(), symbolGutterLineKinds_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    symbolGutterCacheBuffer_            = &buffer;
    symbolGutterCacheContentGeneration_ = buffer.ContentGeneration();
    symbolGutterCacheWindowStart_       = symbolMarkersCacheWindowStart_;
    symbolGutterCacheWindowEnd_         = symbolMarkersCacheWindowEnd_;

    symbolGutterCacheBuffer_            = &buffer;
    symbolGutterCacheContentGeneration_ = buffer.ContentGeneration();
    symbolGutterCacheWindowStart_       = symbolMarkersCacheWindowStart_;
    symbolGutterCacheWindowEnd_         = symbolMarkersCacheWindowEnd_;
}

void BufferView::EnsureTestGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    // Eligibility gate, EnsureSymbolGutterCache's exact shape -- plus the
    // runner itself: no runner wired (tests) or no parsed outcome yet means
    // nothing to mark, at zero width.
    if (!mode_.testDiscovery || buffer.ReadOnly() || testRunner_ == nullptr || !testRunner_->LatestOutcome()) {
        testGutterLineStatuses_.clear();
        testGutterCacheBuffer_            = &buffer;
        testGutterCacheContentGeneration_ = buffer.ContentGeneration();
        testGutterCacheOutcomeGeneration_ = testRunner_ != nullptr ? testRunner_->OutcomeGeneration() : 0;
        return;
    }

    const text::ITextStorage& content              = buffer.Content();
    const bool                huge                 = content.IsHuge();
    const auto [windowStart, windowEnd]             = HugeStructuralWindow(content);

    if (testGutterCacheBuffer_ == &buffer && testGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        testGutterCacheOutcomeGeneration_ == testRunner_->OutcomeGeneration() && testGutterCacheWindowStart_ == windowStart &&
        testGutterCacheWindowEnd_ == windowEnd) {
        return;
    }

    const editor::testrun::TestRunOutcome& outcome = *testRunner_->LatestOutcome();
    const std::string                      bufferBasename =
        buffer.Path() ? buffer.Path()->filename().string() : std::string();

    testGutterLineStatuses_.clear();
    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // mode_.testDiscovery a bounded window instead of the whole document --
    // marker.startByte is then window-relative, remapped back to absolute
    // buffer coordinates (+= windowStart) before the ByteOffsetToLine call
    // below.
    for (const editor::TestMarker& marker :
         huge ? mode_.testDiscovery(content.Substring(windowStart, windowEnd - windowStart)) : mode_.testDiscovery(buffer.Text())) {
        const std::size_t startByte = huge ? marker.startByte + windowStart : marker.startByte;
        // Aggregate every matching result (parameterized instances, go
        // subtests): Failed beats Passed beats Skipped. The result's file,
        // when it names one at all, is only a basename-level *filter*
        // against cross-file name collisions -- the name is the real key
        // (results carry cwd-relative or basename-only paths, see
        // TestOutputParser.h; anything path-shaped stricter than a
        // basename comparison would reject its own legitimate matches).
        std::optional<editor::testrun::TestResult::Status> aggregate;
        for (const editor::testrun::TestResult& result : outcome.results) {
            if (!editor::testrun::MatchesTestName(marker.name, result.name)) {
                continue;
            }
            if (!result.file.empty() && !bufferBasename.empty() &&
                std::filesystem::path(result.file).filename().string() != bufferBasename) {
                continue;
            }
            if (result.status == editor::testrun::TestResult::Status::Failed) {
                aggregate = result.status;
                break;
            }
            if (!aggregate || (aggregate == editor::testrun::TestResult::Status::Skipped &&
                               result.status == editor::testrun::TestResult::Status::Passed)) {
                aggregate = result.status;
            }
        }
        // A discovered test with no result at all reads as "passed" only
        // when the format never names passing tests, the run was a full
        // (unfiltered) one, and the output genuinely parsed -- otherwise
        // absence means "not run", which gets no mark rather than a guess.
        if (!aggregate && outcome.failuresOnly && outcome.parsedOk && !testRunner_->LastRunWasFiltered()) {
            aggregate = editor::testrun::TestResult::Status::Passed;
        }
        if (aggregate) {
            testGutterLineStatuses_.emplace_back(content.ByteOffsetToLine(startByte), *aggregate);
        }
    }
    // One entry per line, Failed winning a same-line tie (a class marker and
    // a same-line method can't collide in practice, but two markers on one
    // line must not produce two sort keys).
    const auto tieRank = [](editor::testrun::TestResult::Status status) {
        switch (status) {
            case editor::testrun::TestResult::Status::Failed:
                return 0;
            case editor::testrun::TestResult::Status::Passed:
                return 1;
            case editor::testrun::TestResult::Status::Skipped:
                return 2;
        }
        return 3;
    };
    std::sort(testGutterLineStatuses_.begin(), testGutterLineStatuses_.end(),
              [&](const auto& a, const auto& b) {
                  return a.first != b.first ? a.first < b.first : tieRank(a.second) < tieRank(b.second);
              });
    testGutterLineStatuses_.erase(std::unique(testGutterLineStatuses_.begin(), testGutterLineStatuses_.end(),
                                              [](const auto& a, const auto& b) { return a.first == b.first; }),
                                  testGutterLineStatuses_.end());

    testGutterCacheBuffer_            = &buffer;
    testGutterCacheContentGeneration_ = buffer.ContentGeneration();
    testGutterCacheOutcomeGeneration_ = testRunner_->OutcomeGeneration();
    testGutterCacheWindowStart_       = windowStart;
    testGutterCacheWindowEnd_         = windowEnd;
}

void BufferView::EnsureInlineDiagnosticCache() const {
    text::Buffer& buffer = activeBuffer_.Get();
    if (inlineDiagnosticCacheBuffer_ == &buffer && inlineDiagnosticCacheDiagGeneration_ == buffer.DiagnosticsGeneration() &&
        inlineDiagnosticCacheContentGeneration_ == buffer.ContentGeneration()) {
        return;
    }

    inlineDiagnosticsByLine_.clear();
    const text::ITextStorage& content = buffer.Content();
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        // prose-diagnostic-callout follow-up: the prose/grammar checker's
        // diagnostics never get this code-style caret+message annotation
        // row -- see PaintProseDiagnosticCallouts instead.
        if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
            continue;
        }
        const std::size_t line = content.ByteOffsetToLine(std::min(diagnostic.startByte, content.ByteLength()));
        const auto        it   = inlineDiagnosticsByLine_.find(line);
        const bool        replaces =
            it == inlineDiagnosticsByLine_.end() ||
            DiagnosticSeverityRank(diagnostic.severity) > DiagnosticSeverityRank(it->second.severity) ||
            (DiagnosticSeverityRank(diagnostic.severity) == DiagnosticSeverityRank(it->second.severity) &&
             diagnostic.startByte < it->second.startByte);
        if (!replaces) {
            continue;
        }
        // First line of the message only -- one annotation row per line,
        // "within reason" (clangd's notes/fix-its can make these multiline).
        std::string message            = diagnostic.message.substr(0, diagnostic.message.find('\n'));
        inlineDiagnosticsByLine_[line] = InlineDiagnostic{
            .severity  = diagnostic.severity,
            .startByte = diagnostic.startByte,
            .endByte   = std::max(diagnostic.endByte, diagnostic.startByte + 1), // widen zero-length spans, same as the underline pass
            .message   = std::move(message),
        };
    }

    inlineDiagnosticCacheBuffer_            = &buffer;
    inlineDiagnosticCacheDiagGeneration_    = buffer.DiagnosticsGeneration();
    inlineDiagnosticCacheContentGeneration_ = buffer.ContentGeneration();
}

std::size_t BufferView::AnnotationRowsForLine(std::size_t line) const {
    if (!editor::InlineDiagnosticsEnabled()) {
        return 0;
    }
    EnsureInlineDiagnosticCache();
    return inlineDiagnosticsByLine_.contains(line) ? 1 : 0;
}

std::size_t BufferView::LeadingAnnotationRowsForLine(std::size_t line) const {
    if (!lspManager_ || !editor::lsp::LspCodeLensEnabled()) {
        return 0;
    }
    text::Buffer&             buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();
    if (line >= content.LineCount()) {
        return 0;
    }
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        if (lens.startByte >= lineStart && lens.startByte < lineEnd) {
            return 1;
        }
        // A lens anchored at an empty line's own zero-width range (lineEnd
        // == lineStart there) would never satisfy `< lineEnd` above --
        // caught here instead, the same edge case InlayHintsForLine's own
        // [lineStart, lineEnd) convention doesn't need to worry about
        // (an inlay hint is never the only content on its own line).
        if (lineStart == lineEnd && lens.startByte == lineStart) {
            return 1;
        }
    }
    return 0;
}

void BufferView::PaintCodeLensRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth) const {
    text::Buffer&             buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();
    if (line >= content.LineCount() || !lspManager_) {
        return;
    }
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    std::string joinedTitle;
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        const bool onThisLine =
            (lens.startByte >= lineStart && lens.startByte < lineEnd) || (lineStart == lineEnd && lens.startByte == lineStart);
        if (!onThisLine || lens.title.empty()) {
            continue;
        }
        if (!joinedTitle.empty()) {
            joinedTitle += " | ";
        }
        joinedTitle += lens.title;
    }
    if (joinedTitle.empty()) {
        return; // shouldn't happen (RowsForLine/LeadingAnnotationRowsForLine agree with this scan) -- leave the blanked row
    }

    const Brush titleBrush{.background = theme_.background, .foreground = theme_.ghostTextForeground, .italic = true};
    const int   width = c.size().width;
    int         col   = static_cast<int>(gutterWidth);
    for (const char ch : joinedTitle) {
        if (col >= width) {
            break;
        }
        Cell& cell     = c[{.x = col, .y = row}];
        cell.character = std::string(1, ch);
        titleBrush.ApplyTo(cell);
        ++col;
    }
}

std::vector<editor::SymbolMarker> BufferView::StickyScrollChainForCurrentViewport() const {
    if (!editor::StickyScrollEnabled()) {
        return {};
    }
    const int maxRows = editor::StickyScrollMaxRows();
    if (maxRows <= 0) {
        return {};
    }
    EnsureSymbolMarkersCache();
    if (symbolMarkersCache_.empty()) {
        return {};
    }

    const text::Buffer&       buffer          = activeBuffer_.Get();
    const text::ITextStorage& content         = buffer.Content();
    const std::size_t         viewportTopByte = content.LineToByteOffset(topLine_);
    std::vector<editor::SymbolMarker> chain =
        editor::stickyscroll::StickyChainForViewportTop(symbolMarkersCache_, viewportTopByte);
    if (static_cast<int>(chain.size()) > maxRows) {
        // Keep the INNERMOST rows (nearest ancestors) when the chain runs
        // deeper than the cap -- the immediate enclosing context is more
        // useful than the outermost namespace once space is tight.
        chain.erase(chain.begin(), chain.begin() + (static_cast<int>(chain.size()) - maxRows));
    }
    return chain;
}

int BufferView::PaintStickyScrollRows(Canvas& c, std::size_t gutterWidth) const {
    const std::vector<editor::SymbolMarker> chain = StickyScrollChainForCurrentViewport();
    if (chain.empty()) {
        return 0;
    }

    const text::ITextStorage& content = activeBuffer_.Get().Content();
    const int                 width   = c.size().width;

    // sticky-scroll-gutter-alignment follow-up: mirrors GutterWidth()/
    // Paint()'s own [dap][diff][status][diagnostic][gap][digits][gap][test]
    // [symbol][fold][blame] column layout (see those methods' own doc
    // comments), recomputed here from the same Active()-flag primitives
    // rather than trusting gutterWidth as a second source of truth -- the
    // same "recompute, don't unpack" precedent OnMouseEvent's own foldStart
    // derivation already follows. Only digitsStart/gutterDigits and
    // symbolStart are needed: those are the two columns a sticky row
    // actually paints into, so its line number and glyph land in the exact
    // same screen columns an ordinary content row's own digits/symbol glyph
    // do -- a sticky row reads as a frozen real gutter row, not a
    // synthesized label. Fold/blame never get anything drawn into them here.
    const std::size_t diffColumnWidth    = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t dapColumnWidth     = DapGutterActive() ? kDapWidth : 0;
    const std::size_t diagnosticStart    = dapColumnWidth + diffColumnWidth + kStatusWidth;
    const std::size_t lineNumberGapWidth = LineNumberGutterActive() ? kLineNumberGap : 0;
    const std::size_t digitsStart        = diagnosticStart + kDiagnosticWidth + lineNumberGapWidth;
    const std::size_t gutterDigits       = LineNumberGutterActive() ? std::to_string(content.LineCount()).size() : 0;
    const std::size_t testColumnWidth    = TestGutterActive() ? kTestWidth : 0;
    const std::size_t symbolStart        = digitsStart + gutterDigits + lineNumberGapWidth + testColumnWidth;

    for (std::size_t i = 0; i < chain.size(); ++i) {
        const editor::SymbolMarker& marker = chain[i];
        const int                   row    = static_cast<int>(i);

        for (int col = 0; col < width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            theme_.tabBar.ApplyTo(cell);
        }

        const std::size_t line = content.ByteOffsetToLine(marker.startByte);

        // Line number -- the real gutter's own digits column, right-aligned
        // to gutterDigits exactly like an ordinary content row. Always
        // absolute (not vim relativenumber-aware): this names "where this
        // header lives," not a motion distance from point.
        if (LineNumberGutterActive()) {
            const std::string lineNumber = std::to_string(line + 1);
            const std::size_t padding    = gutterDigits > lineNumber.size() ? gutterDigits - lineNumber.size() : 0;
            const Brush lineNumberBrush{.background = theme_.tabBar.background, .foreground = theme_.lineNumberForeground};
            for (std::size_t k = 0; k < lineNumber.size() && static_cast<int>(digitsStart + padding + k) < width; ++k) {
                Cell& cell     = c[{.x = static_cast<int>(digitsStart + padding + k), .y = row}];
                cell.character = std::string(1, lineNumber[k]);
                lineNumberBrush.ApplyTo(cell);
            }
        }

        // Glyph -- the real gutter's own symbol column, one fixed column
        // (not staircased with depth -- the content text's own indent below
        // carries that cue instead), matching an ordinary content row's
        // symbol glyph exactly.
        if (static_cast<int>(symbolStart) < width) {
            const Brush glyphBrush{.background = theme_.tabBar.background,
                                   .foreground = theme_.BrushFor(editor::SyntaxClassFor(marker.kind)).foreground,
                                   .bold       = true};
            Cell& glyphCell     = c[{.x = static_cast<int>(symbolStart), .y = row}];
            glyphCell.character = SymbolGlyphFor(marker.kind);
            glyphBrush.ApplyTo(glyphCell);
        }

        // sticky-scroll-signature follow-up: content area (starting at the
        // real gutterWidth, exactly where an ordinary row's own text
        // starts) shows a trimmed copy of the marker's own source line (its
        // "reduced signature") rather than just the bare identifier -- more
        // informative at a glance, and reads as real code (a template-param/
        // base-class list, return type, etc.) instead of a synthesized
        // label. Deliberately just the marker's own start LINE, verbatim
        // minus leading indentation -- not a joined/flattened multi-line
        // signature; "the line it's sticky on" is exactly that, one real
        // source line, same as every other sticky-scroll implementation
        // (VSCode/Zed) shows. Staircase-indented one level per ancestor row
        // -- the depth cue the glyph used to carry back when it lived here
        // instead of the real symbol column above.
        int col = static_cast<int>(gutterWidth) + static_cast<int>(i) * 2;
        if (col >= width) {
            continue;
        }
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::string rawLine       = content.Substring(lineStart, lineEnd - lineStart);
        const std::size_t firstNonBlank = rawLine.find_first_not_of(" \t");
        const std::string trimmedLine   = (firstNonBlank == std::string::npos) ? std::string() : rawLine.substr(firstNonBlank);

        // Signature text keeps whatever brush the row's own blank fill above
        // already applied (theme_.tabBar) -- plain chrome-text color, not
        // colored/bolded by symbol kind; only cell.character changes here.
        // Names/signatures in every bundled tags.scm are effectively-always-
        // ASCII source text; a raw byte-per-cell walk here mirrors
        // PaintCodeLensRow's own established (same-file) precedent.
        // Reserves the row's own last column for a "…" marker when the
        // trimmed line doesn't fit, rather than silently cutting off
        // mid-signature (ListPopup's own convention).
        const bool truncated = col + static_cast<int>(trimmedLine.size()) > width;
        const int  textLimit = truncated ? width - 1 : width;
        for (const char ch : trimmedLine) {
            if (col >= textLimit) {
                break;
            }
            c[{.x = col, .y = row}].character = std::string(1, ch);
            ++col;
        }
        if (truncated && col < width) {
            c[{.x = col, .y = row}].character = "…";
        }
    }
    return static_cast<int>(chain.size());
}

void BufferView::EnsureBlameGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();
    if (blameGutterCacheBuffer_ == &buffer && blameGutterCacheContentGeneration_ == buffer.ContentGeneration()) {
        return; // still valid for this buffer/content -- nothing to do (see this method's own header comment)
    }
    // Either the active buffer changed, or its content did since blame was
    // last populated -- either way, blameLineInfo_ no longer corresponds to
    // real line attribution. Clear it rather than trying to resynthesize
    // it; a fresh vcs-show-blame call is what repopulates it.
    blameLineInfo_.clear();
    blameGutterCacheBuffer_            = &buffer;
    blameGutterCacheContentGeneration_ = buffer.ContentGeneration();
}

bool BufferView::BlameGutterActive() const {
    return !blameLineInfo_.empty();
}

void BufferView::SetVcsRunner(editor::vcs::VcsRunner* vcsRunner) {
    vcsRunner_ = vcsRunner;
}

void BufferView::DispatchBlameForTesting(std::vector<editor::vcs::VcsBlameLine> lines) {
    text::Buffer& buffer = activeBuffer_.Get();
    blameLineInfo_.clear();
    blameLineInfo_.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        blameLineInfo_.emplace_back(i, std::move(lines[i]));
    }
    blameGutterCacheBuffer_            = &buffer;
    blameGutterCacheContentGeneration_ = buffer.ContentGeneration();
}

void BufferView::RequestBlameForCurrentBuffer() {
    // A real toggle: vcs-show-blame called again while blame is already
    // showing for this buffer turns it off instead of re-fetching -- the
    // reported, real gap this fixes is that there was previously no way to
    // turn it back off at all short of switching buffers and back (which
    // clears it as a side effect of Paint()'s own buffer-switch handling,
    // not a deliberate toggle). Guarded on blameGutterCacheBuffer_
    // specifically (not just BlameGutterActive()) so pressing the key
    // again for a *different* buffer than the one blame is currently
    // loaded for still fetches fresh, rather than clearing the wrong
    // buffer's (already-stale-by-definition, since it's a different
    // buffer) data.
    if (BlameGutterActive() && blameGutterCacheBuffer_ == &activeBuffer_.Get()) {
        blameLineInfo_.clear();
        statusMessage_ = "blame hidden";
        return;
    }

    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    vcsRunner_->RequestBlame(
        *buffer,
        [this, buffer](std::vector<editor::vcs::VcsBlameLine> lines) {
            if (&activeBuffer_.Get() != buffer) {
                return; // active buffer changed while the request was in flight -- discard, it's stale
            }
            DispatchBlameForTesting(std::move(lines)); // reused here too -- see its own doc comment
            statusMessage_ = "blame loaded";
        },
        [this](std::string error) { statusMessage_ = "vcs blame: " + error; });
}

void BufferView::ShowBlameDetailAtPoint() {
    if (!BlameGutterActive()) {
        statusMessage_ = "no blame data loaded -- run vcs-show-blame (C-c v b) first";
        return;
    }

    const text::Buffer& buffer = activeBuffer_.Get();
    const std::size_t   line   = buffer.Content().ByteOffsetToLine(buffer.Point());

    // Same lower_bound lookup Paint()'s own blame-gutter rendering uses --
    // blameLineInfo_ is sorted by line, one entry per blamed line.
    const auto it = std::lower_bound(blameLineInfo_.begin(), blameLineInfo_.end(), line,
                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
    if (it == blameLineInfo_.end() || it->first != line) {
        statusMessage_ = "no blame data for this line";
        return;
    }

    const editor::vcs::VcsBlameLine& blame = it->second;
    statusMessage_                         = blame.commitHash + " " + blame.author + " (" + blame.date + "): " + blame.summary;
}

void BufferView::ScheduleDiffRefresh() {
    if (!eventLoop_ || !vcsRunner_) {
        return; // headless test, or no VcsRunner wired in -- see this method's own header comment
    }
    // Same "Arm re-cancels any still-pending previous fire" debounce shape
    // completionDebounceTimer_ already established for LSP completion --
    // rapid typing keeps pushing the debounce deadline out
    // rather than firing once per keystroke.
    diffRefreshTimer_.Arm(*eventLoop_, editor::DiffRefreshDebounce(), [this] { RequestDiffForCurrentBuffer(); });
}

void BufferView::DispatchDiffForTesting(std::vector<editor::vcs::VcsDiffHunk> hunks) {
    // initial-buffer-diff fix: mark the active buffer's diff as synced. In
    // production this is a no-op (the only real caller is the request
    // completion, which only runs after Paint's diffSyncBuffer_ branch
    // already set this); for a test injecting hunks directly it's what
    // keeps the next Paint() from immediately clearing them via that same
    // branch.
    diffSyncBuffer_ = &activeBuffer_.Get();
    std::vector<std::pair<std::size_t, DiffLineKind>> kinds;
    std::vector<std::size_t>                          hunkStartLines;
    for (const editor::vcs::VcsDiffHunk& hunk : hunks) {
        if (hunk.newCount == 0) {
            // Pure deletion -- a boundary, not a covered range. git's
            // newStart is already the 0-indexed line the deletion sits
            // immediately before (1-indexed "the line after the gap" ==
            // 0-indexed "that same line"), confirmed against real `git
            // diff -U0` output while building this (see
            // GitVcsPluginTest.cpp).
            kinds.emplace_back(hunk.newStart, DiffLineKind::Removed);
            hunkStartLines.push_back(hunk.newStart);
            continue;
        }
        const DiffLineKind kind = (hunk.oldCount == 0) ? DiffLineKind::Added : DiffLineKind::Modified;
        for (std::size_t i = 0; i < hunk.newCount; ++i) {
            kinds.emplace_back(hunk.newStart - 1 + i, kind); // newStart is 1-indexed
        }
        hunkStartLines.push_back(hunk.newStart - 1);
    }
    std::sort(kinds.begin(), kinds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    diffLineKinds_ = std::move(kinds);
    std::sort(hunkStartLines.begin(), hunkStartLines.end());
    diffHunkStartLines_ = std::move(hunkStartLines);
}

void BufferView::JumpToNextHunk() {
    if (diffHunkStartLines_.empty()) {
        statusMessage_ = "No changes in this buffer.";
        return;
    }
    text::Buffer&      buffer      = activeBuffer_.Get();
    const std::size_t  currentLine = buffer.Content().ByteOffsetToLine(buffer.Point());
    const auto         it          = std::upper_bound(diffHunkStartLines_.begin(), diffHunkStartLines_.end(), currentLine);
    if (it == diffHunkStartLines_.end()) {
        statusMessage_ = "No more changed hunks below point.";
        return;
    }
    buffer.SetPoint(buffer.Content().LineToByteOffset(*it));
    statusMessage_.clear();
    ScrollToShowPoint();
}

void BufferView::JumpToPreviousHunk() {
    if (diffHunkStartLines_.empty()) {
        statusMessage_ = "No changes in this buffer.";
        return;
    }
    text::Buffer&      buffer      = activeBuffer_.Get();
    const std::size_t  currentLine = buffer.Content().ByteOffsetToLine(buffer.Point());
    const auto         it          = std::lower_bound(diffHunkStartLines_.begin(), diffHunkStartLines_.end(), currentLine);
    if (it == diffHunkStartLines_.begin()) {
        statusMessage_ = "No more changed hunks above point.";
        return;
    }
    buffer.SetPoint(buffer.Content().LineToByteOffset(*(it - 1)));
    statusMessage_.clear();
    ScrollToShowPoint();
}

void BufferView::NextError() {
    StepError(/*forward=*/true);
}

void BufferView::PreviousError() {
    StepError(/*forward=*/false);
}

void BufferView::StepError(bool forward) {
    const std::optional<std::string> name = editor::LastResultsBuffer();
    if (!name) {
        statusMessage_ = "No results to step through.";
        return;
    }
    text::Buffer* resultsBuffer = bufferList_.Find(*name);
    if (!resultsBuffer) {
        statusMessage_ = "No results to step through.";
        return;
    }
    const std::vector<editor::ErrorLocation> locations = editor::CollectResultLocations(*resultsBuffer);
    if (locations.empty()) {
        statusMessage_ = "No results to step through.";
        return;
    }

    // Editor/NextError.h's StepResultLocation owns the actual walk cursor
    // (index-based, process-wide, reset whenever a fresh results buffer is
    // registered) -- see its own doc comment for why this can't just
    // compare against resultsBuffer's Point().
    const std::optional<editor::ErrorLocation> next = editor::StepResultLocation(locations, forward);
    if (!next) {
        statusMessage_ = forward ? "No more errors below point." : "No more errors above point.";
        return;
    }
    JumpToPathLine(next->sourcePath, next->sourceLine);
}

void BufferView::RequestDiffForCurrentBuffer() {
    if (!vcsRunner_) {
        return; // silent -- see this method's own header comment
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    vcsRunner_->RequestDiff(
        *buffer,
        [this, buffer](std::vector<editor::vcs::VcsDiffHunk> hunks) {
            if (&activeBuffer_.Get() != buffer) {
                return; // active buffer changed while the request was in flight -- discard, it's stale
            }
            DispatchDiffForTesting(std::move(hunks)); // reused here too -- see its own doc comment
        },
        [](const std::string&) {}); // silent -- see this method's own header comment
}

void BufferView::RefreshVcsDiff() {
    RequestDiffForCurrentBuffer();
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
        return 0; // hidden hides the annotation row too -- a fold swallows the whole line
    }
    // inline-diagnostics follow-up: an annotated line reports one extra row
    // here, at the single source every row-math consumer already shares
    // (CursorPosition, ScrollToShowPoint, MaxTopLine, ByteOffsetForPoint,
    // VisibleRowCountBetween/AtLeast) -- the same seam wrap continuation
    // rows ride, so none of them can disagree about where an annotation
    // shifted the rows below it.
    const std::size_t annotationRows = AnnotationRowsForLine(line);
    // codeLens follow-up: LeadingAnnotationRowsForLine's own 0-or-1 count,
    // added at the same three return points annotationRows already is --
    // see that method's own doc comment for why this is a leading
    // (above-the-line) row rather than another trailing one.
    const std::size_t leadingRows = LeadingAnnotationRowsForLine(line);
    if (!EffectiveWrapLines()) {
        return 1 + leadingRows + annotationRows;
    }
    EnsureRowCountCache();
    if (line >= rowCountPerLine_.size()) {
        return 1 + leadingRows + annotationRows;
    }
    if (rowCountPerLine_[line] == kRowCountUnknown) {
        // line-wrap follow-up: the real, lazy, per-line word-break scan --
        // computed and memoized only for a line actually asked about, never
        // eagerly for the whole buffer (see EnsureRowCountCache's own doc
        // comment for why that distinction is load-bearing, not cosmetic).
        text::Buffer&     buffer    = activeBuffer_.Get();
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        EnsureLinkCache();
        const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());
        rowCountPerLine_[line] =
            ComputeWrapSegments(content, lineStart, lineEnd, rowCountCacheContentWidth_, lineLinks).size();
    }
    return rowCountPerLine_[line] + leadingRows + annotationRows; // memoized value is content rows only -- annotation state changes independently of the wrap cache's keys
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

void BufferView::Paint(Canvas paneCanvas) {
    EnsureTopLineValidForActiveBuffer();
    EnsureStatusMessageFreshness();

    text::Buffer& buffer = activeBuffer_.Get();
    if (modeSyncBuffer_ != &buffer) {
        modeSyncBuffer_ = &buffer;
        if (onActiveBufferChanged_) {
            onActiveBufferChanged_(buffer);
        }
        // The callback just (possibly) replaced mode_ -- but a GutterWidth()
        // call during the switch's own event handling (ScrollToShowPoint,
        // CursorPosition) may already have run the mode-derived gutter
        // caches against this buffer under the OLD mode and stamped them
        // current-and-empty, which the (buffer, generation) gate can't
        // detect (neither stamp changes with the mode). Confirmed live via
        // an instrumented trace: switching back to a python buffer after
        // visiting a fundamental-mode one left the symbol/test columns
        // permanently blank until the next edit. Discarding the stamps here
        // forces one recompute under the mode that will actually paint.
        symbolGutterCacheBuffer_ = nullptr;
        testGutterCacheBuffer_   = nullptr;
    }
    // Diff gutter markers follow-up: a newly-active buffer's diff markers
    // belong to a completely different file -- clearing immediately
    // (rather than leaving the old buffer's markers visible until the
    // fresh request completes) avoids a real, if brief, "wrong file's
    // markers" flash; RequestDiffForCurrentBuffer then kicks off a fresh,
    // unthrottled (not debounced -- switching buffers is a natural "want
    // it now" moment, same as a save) request for this buffer.
    //
    // initial-buffer-diff fix: tracked by its own diffSyncBuffer_, NOT
    // folded into the modeSyncBuffer_ branch above -- the constructor
    // deliberately pre-seeds modeSyncBuffer_ to suppress a spurious
    // first-frame onActiveBufferChanged_, and while the diff request lived
    // in that branch the seeding silently suppressed the initial buffer's
    // diff request too: the file ned was launched on (and every new split
    // pane's starting view) never showed markers until an edit/save
    // happened to fire a request. Confirmed against a live session with
    // gdb (RequestDiffForCurrentBuffer never called at all), not assumed.
    // diffSyncBuffer_ starts null instead, so a pane's very first Paint()
    // fetches its buffer's diff.
    if (diffSyncBuffer_ != &buffer) {
        diffSyncBuffer_ = &buffer;
        diffLineKinds_.clear();
        RequestDiffForCurrentBuffer();
    }

    const text::ITextStorage& content    = buffer.Content();
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
    if (minimap_ != nullptr) {
        // Same fields, same semantics, same values ScrollBar's own sync
        // above uses -- Minimap mirrors ScrollBar's public surface exactly
        // so its viewport-band/click math stays consistent with it.
        minimap_->scrollable_length  = static_cast<int>(MaxTopLine()) + 1;
        minimap_->position           = static_cast<int>(topLine_);
        minimap_->item_visual_length = 1;
    }

    const std::size_t gutterWidth = GutterWidth();
    // status/line-number-spacing follow-up: GutterWidth() already reserves
    // these columns only when actually wanted (see its own doc comment) --
    // recomputing the same condition here keeps the layout math below in
    // agreement with it without a second source of truth. Column offsets,
    // left to right: [diff][status][diagnostic][gap][digits][gap][symbol]
    // [fold][blame] (diff-gutter-markers follow-up put diff leftmost,
    // matching real editors' own git-gutter placement -- see kDiffWidth's
    // own doc comment; blame stayed the rightmost region, past fold; symbol
    // -- gutter-symbol-kind follow-up -- sits just before fold, clustering
    // the two structure-related columns together).
    const std::size_t foldColumnWidth   = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    const std::size_t blameColumnWidth  = BlameGutterActive() ? kBlameWidth : 0;
    const std::size_t diffColumnWidth   = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t symbolColumnWidth = SymbolGutterActive() ? kSymbolWidth : 0;
    // test-runner integration: pass/fail marks, immediately left of the
    // symbol column (landmark columns clustered together).
    const std::size_t testColumnWidth = TestGutterActive() ? kTestWidth : 0;
    // DAP client slice 2: the debug-marker column, leftmost of all when
    // active -- see kDapWidth's own doc comment for the full layout.
    const std::size_t dapColumnWidth  = DapGutterActive() ? kDapWidth : 0;
    const std::size_t diffStart       = dapColumnWidth;
    const std::size_t statusStart     = diffStart + diffColumnWidth;
    const std::size_t diagnosticStart = statusStart + kStatusWidth;

    // Fetched once per Paint(), not once per row -- BreakpointsForKey
    // returns a copy, and the stop location never changes mid-frame.
    // dapPathKey_ is current here: DapGutterActive() just above ran
    // EnsureDapPathKey(). Slice 4: the richer per-line Breakpoint (kind +
    // verified) rather than bare line numbers, for the gutter's
    // glyph-by-kind/color-by-verified rendering below.
    std::vector<editor::dap::DapManager::Breakpoint>   dapBreakpoints;
    std::optional<std::pair<std::string, std::size_t>> dapStop;
    if (dapColumnWidth > 0) {
        dapBreakpoints = dapManager_->BreakpointsForKey(dapPathKey_);
        dapStop        = dapManager_->CurrentStopKeyAndLine();
        if (dapStop && dapStop->first != dapPathKey_) {
            dapStop.reset(); // stopped in some other file -- nothing to mark here
        }
    }
    // Multibuffers follow-up: LineNumberGutterActive() recomputed here
    // (not derived from gutterWidth by subtraction) for the same
    // "recompute the same condition, don't make gutterWidth a second
    // source of truth" reason foldColumnWidth/blameColumnWidth/
    // diffColumnWidth above already recompute their own XActive() checks
    // rather than trying to unpack them back out of gutterWidth.
    const std::size_t lineNumberGapWidth = LineNumberGutterActive() ? kLineNumberGap : 0;
    const std::size_t digitsStart        = diagnosticStart + kDiagnosticWidth + lineNumberGapWidth;
    const std::size_t gutterDigits       = LineNumberGutterActive() ? std::to_string(totalLines).size() : 0;
    const std::size_t testStart          = digitsStart + gutterDigits + lineNumberGapWidth;
    const std::size_t symbolStart        = testStart + testColumnWidth;
    const std::size_t foldStart          = symbolStart + symbolColumnWidth;
    const std::size_t blameStart         = foldStart + foldColumnWidth;

    // status-gutter unsaved-change-indicator follow-up: recomputed once
    // per Paint() call (not per row) -- see EnsureUnsavedChangeCache's own
    // doc comment. Unconditional, unlike EnsureFoldGutterCache -- every
    // buffer gets a status column regardless of mode/language.
    EnsureUnsavedChangeCache();
    // LSP client follow-up: same "unconditional, every buffer gets one"
    // reasoning as EnsureUnsavedChangeCache above.
    EnsureDiagnosticGutterCache();
    // VCS blame gutter: unconditional every Paint() like the two above, but
    // this only ever clears (never repopulates) blameLineInfo_ -- see its
    // own doc comment.
    EnsureBlameGutterCache();

    // LSP client follow-up: syncs the *active* buffer only, once per frame
    // -- see LspManager::SyncBuffer's own doc comment for why only the
    // currently-visible buffer, not every open one. A no-op if lspManager_
    // is unset (ordinary tests) or nothing's configured for this mode's
    // language (LspServerCommand returns nullopt, checked inside SyncBuffer
    // itself).
    if (lspManager_) {
        lspManager_->SyncBuffer(buffer, editor::LanguageKeyForMode(mode_));
        // semanticTokens follow-up: same per-frame, active-buffer-only
        // cadence as SyncBuffer just above, deliberately called from here
        // (BufferView's own per-frame decision point) rather than from
        // inside LspManager::SyncToServer -- see RequestSemanticTokensFull's
        // own doc comment in LspManager.h for why keeping it out of that
        // hot path matters (a real lesson from pull-diagnostics' own
        // test-regression fix). No-ops internally when disabled, unopened,
        // or content unchanged since the last request.
        lspManager_->RequestSemanticTokensFull(buffer, editor::LanguageKeyForMode(mode_));

        // inlayHint follow-up: same per-frame cadence as the two calls just
        // above, scoped to the currently visible line range -- unlike
        // semanticTokens' whole-document request, inlayHint's own "range"
        // param exists specifically so a client only asks for what's on
        // screen. Same viewport computation HugeStructuralWindow uses,
        // minus its huge-buffer-only parse-safety margin (not needed here
        // -- there's no parser to desync, just a request scope).
        {
            const std::size_t lastLine    = totalLines > 0 ? totalLines - 1 : 0;
            const std::size_t viewportTop = std::min(topLine_, lastLine);
            const std::size_t viewportHeight = size().height > 0 ? static_cast<std::size_t>(size().height) : 1;
            const std::size_t viewportBottom    = std::min(viewportTop + viewportHeight, lastLine);
            const std::size_t viewportStartByte = content.LineToByteOffset(viewportTop);
            const std::size_t viewportEndByte   = std::min(content.LineToByteOffset(viewportBottom) + 1, content.ByteLength());
            lspManager_->RequestInlayHints(buffer, viewportStartByte, viewportEndByte, editor::LanguageKeyForMode(mode_));
        }

        // codeLens follow-up: same per-frame cadence as the calls above,
        // whole-document scope (codeLens has no "range" param, unlike
        // inlayHint) -- see RequestCodeLenses' own doc comment in
        // LspManager.h for the dedup/learn-once gating this no-ops behind.
        lspManager_->RequestCodeLenses(buffer, editor::LanguageKeyForMode(mode_));

        // embedded-language-documents follow-up: computes/caches this
        // buffer's embedded documents (currently just html-mode's
        // <script>/<style> content) once per actually-changed Paint() call
        // -- see EnsureEmbeddedDocumentCache -- then syncs each to its own
        // real LSP server. Called unconditionally, same as SyncBuffer just
        // above: cheap when nothing changed, gated internally by each
        // server's own lastSyncedGeneration, and also what tears down a
        // server whose only region was just edited away (an empty list here
        // when mode_.embeddedRegions is unset or reports nothing).
        std::vector<editor::lsp::LspManager::EmbeddedDocumentSync> embeddedSync;
        EnsureEmbeddedDocumentCache();
        if (const auto cacheIt = embeddedDocumentCacheByBuffer_.find(&buffer); cacheIt != embeddedDocumentCacheByBuffer_.end()) {
            embeddedSync.reserve(cacheIt->second.documents.size());
            for (const editor::EmbeddedDocument& document : cacheIt->second.documents) {
                embeddedSync.push_back(editor::lsp::LspManager::EmbeddedDocumentSync{
                    .language = document.language, .documentText = document.documentText, .ownedRanges = document.ownedRanges});
            }
        }
        lspManager_->SyncEmbeddedDocuments(buffer, embeddedSync);
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

    // user-facing-hang-affordance follow-up (ChildProcess-hang-protection-
    // round-2): same once-per-frame poll idiom as the LSP-log check above,
    // just against the shared *Messages* log instead of the older, LSP-only
    // "*lsp log*" one -- a hang/timeout recovery (a killed clipboard-paste
    // subprocess, an LSP/DAP/ACP stall disconnect, a stale-request timeout)
    // previously left no trace beyond that log entry itself, with nothing to
    // draw the user's eye to it. Checked after the LSP-log branch above so
    // the two never race for the same frame's statusMessage_ (whichever
    // fires first wins; the other's flag stays set and surfaces next frame
    // once statusMessage_ is empty again). Gated on surfaceUnseenLogEntries_
    // (default false, see SetSurfaceUnseenLogEntries's own doc comment) --
    // unlike lspManager_ above, editor::HasUnseenDiagnosticsLogEntry() reads
    // genuinely process-wide state, so an unconditional check here would let
    // any other test's own LogMessage call leak into this one's Paint().
    if (surfaceUnseenLogEntries_ && editor::HasUnseenDiagnosticsLogEntry() && statusMessage_.empty()) {
        statusMessage_ = "New warning -- see *Messages* (M-x show-messages)";
        editor::AcknowledgeDiagnosticsLogEntry();
    }

    // diagnostics-UX follow-up: live echo of the diagnostic on point's own
    // line, updating as point moves, so reading an error never requires a
    // command at all (lsp-show-diagnostic stays for the full/multi-message
    // case). Same once-per-frame poll idiom as the LSP-log check above.
    // autoDiagnosticMessage_ remembers exactly what this poll last wrote so
    // it only ever overwrites/clears its OWN message -- a real command
    // result, prompt text, or any other writer always wins, and leaving the
    // line takes the echo away instead of it lingering like a normal status
    // message would.
    if (inputMode_ == InputMode::Normal) {
        const std::size_t pointLineStart = content.LineToByteOffset(pointLine);
        const std::size_t pointLineEnd =
            (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) : content.ByteLength();
        const text::Buffer::Diagnostic* firstOnLine = nullptr;
        std::size_t                     extraOnLine = 0;
        for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
            if (diagnostic.startByte >= pointLineStart && diagnostic.startByte < pointLineEnd) {
                if (firstOnLine == nullptr) {
                    firstOnLine = &diagnostic;
                }
                else {
                    ++extraOnLine;
                }
            }
        }
        std::string autoMessage;
        if (firstOnLine != nullptr) {
            switch (firstOnLine->severity) {
                case text::Buffer::Diagnostic::Severity::Error:
                    autoMessage = "Error: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Warning:
                    autoMessage = "Warning: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Information:
                    autoMessage = "Info: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Hint:
                    autoMessage = "Hint: ";
                    break;
            }
            autoMessage += firstOnLine->message;
            if (extraOnLine > 0) {
                autoMessage += " (+" + std::to_string(extraOnLine) + " more on this line)";
            }
        }
        if (!autoMessage.empty()) {
            if (statusMessage_.empty() || statusMessage_ == autoDiagnosticMessage_) {
                statusMessage_         = autoMessage;
                autoDiagnosticMessage_ = autoMessage;
            }
        }
        else if (!autoDiagnosticMessage_.empty()) {
            if (statusMessage_ == autoDiagnosticMessage_) {
                statusMessage_.clear();
            }
            autoDiagnosticMessage_.clear();
        }
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
    // Past editor::MaxHighlightBytes() (loose-ends follow-up: was a
    // hardcoded 8 MiB kMaxHighlightBytes here, now Editor/
    // HighlightSettings.h's configurable process-wide setting), never run
    // mode_.highlight at all, regardless of whether the file's extension
    // matches a real grammar -- buffer.ReadOnly() already suppresses
    // highlighting for results buffers and IsLoading() placeholders, but a
    // huge file that finishes loading and reverts to writable would
    // otherwise still pay a full buffer.Text() copy plus a whole-buffer
    // tree-sitter parse on every edit.
    if (!mode_.highlight || buffer.ReadOnly() || buffer.Size() > editor::MaxHighlightBytes()) {
        highlightCacheBuffer_ = nullptr;
        highlightCacheSpans_.clear();
    }
    else if (const std::size_t semanticTokensGeneration = lspManager_ ? lspManager_->SemanticTokensGeneration(buffer) : 0;
             highlightCacheBuffer_ != &buffer || highlightCacheGeneration_ != buffer.ContentGeneration() ||
             highlightCacheClassGeneration_ != editor::CaptureClassGeneration() ||
             highlightCacheSemanticTokensGeneration_ != semanticTokensGeneration) {
        // per-buffer-highlight-cache follow-up: persists across a buffer
        // switch, not just repeated Paint() calls on the same buffer -- see
        // highlightCacheByBuffer_'s own doc comment in BufferView.h. The
        // CaptureClassGeneration() check (exhaustive-highlighting
        // follow-up) and the modeName check (this follow-up) both matter
        // here for the same reason: either can make a stale entry's spans
        // wrong even though buffer's content hasn't changed at all --
        // semanticTokensGeneration (semanticTokens follow-up) is the same
        // idea again: an LSP response can arrive, and change what should
        // render, with no buffer edit at all.
        const auto it = highlightCacheByBuffer_.find(&buffer);
        if (it == highlightCacheByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
            it->second.classGeneration != editor::CaptureClassGeneration() || it->second.modeName != mode_.name ||
            it->second.semanticTokensGeneration != semanticTokensGeneration) {
            HighlightCacheEntry entry;
            entry.spans = mode_.highlight(buffer.Text());
            // semanticTokens follow-up: appended *after* tree-sitter's own
            // spans so LSP-informed classification wins at overlapping
            // bytes -- the exact "later span wins" convention the
            // injection engine already exploits for the same reason (see
            // Mode.h's own HighlightSpan doc comment), not a new
            // resolution rule. Empty when lspManager_ is unset, disabled,
            // or no response has landed yet.
            if (lspManager_) {
                const std::vector<editor::HighlightSpan>& semanticSpans = lspManager_->SemanticTokenSpans(buffer);
                entry.spans.insert(entry.spans.end(), semanticSpans.begin(), semanticSpans.end());
            }
            entry.contentGeneration        = buffer.ContentGeneration();
            entry.classGeneration          = editor::CaptureClassGeneration();
            entry.semanticTokensGeneration = semanticTokensGeneration;
            entry.modeName                 = mode_.name;
            highlightCacheSpans_           = entry.spans;
            highlightCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
        }
        else {
            highlightCacheSpans_ = it->second.spans;
        }
        highlightCacheBuffer_                   = &buffer;
        highlightCacheGeneration_               = buffer.ContentGeneration();
        highlightCacheClassGeneration_          = editor::CaptureClassGeneration();
        highlightCacheSemanticTokensGeneration_ = semanticTokensGeneration;
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
    std::vector<RenderedInlayHint>     currentLineInlayHints;
    // Whitespace-visualization follow-up: same "compute once per line, not
    // once per row/character" shape as currentLineSpans/currentLineLinks
    // above. currentLineTrailingWhitespaceStart is the byte offset of the
    // first byte in the line's own trailing run of spaces/tabs (lineEnd
    // itself if the line has no trailing whitespace, so the `offset >=`
    // check below is trivially false); currentLineIndentEnd is the byte
    // offset one past the line's own leading run of spaces/tabs (lineStart
    // itself if the line has no leading whitespace at all).
    std::size_t currentLineTrailingWhitespaceStart = 0;
    std::size_t currentLineIndentEnd               = 0;
    // Diff gutter markers follow-up: same "compute once per line, not once
    // per row/character" shape as currentLineSpans/currentLineLinks above --
    // feeds the subtle background tint applied per character below
    // (Removed has no line to tint, only Added/Modified ever populate this).
    std::optional<DiffLineKind> currentLineDiffTint;
    // Multibuffers follow-up: same "compute once per line" shape as
    // currentLineDiffTint above, but for the *vcs diff* multibuffer's own
    // stitched added/removed lines (a static property of that buffer's
    // content, not a live comparison against disk the way the source-file
    // diff gutter is) -- see the per-character brush selection below for
    // why a content-area wash is safe here specifically.
    std::optional<editor::multibuffer::LineTint> currentMultibufferTint;
    // diagnostics-UX follow-up: the diagnostic byte spans overlapping the
    // current line, feeding the per-character underline below -- same
    // "compute once per line" shape as everything above. A zero-length
    // diagnostic span (some servers report those) is widened to one byte so
    // it still underlines the cell it points at instead of vanishing.
    std::vector<std::pair<std::size_t, std::size_t>> currentLineDiagnosticSpans;
    // documentHighlight follow-up: the LSP-reported occurrence-of-symbol-at-
    // point byte spans overlapping the current line -- same "compute once
    // per line, from BufferView-owned ephemeral state" shape as
    // currentLineDiagnosticSpans above, but sourced from documentHighlight_
    // (a point-triggered request/response, not Buffer::Diagnostics()'
    // server-pushed set).
    std::vector<std::pair<std::size_t, std::size_t>> currentLineDocumentHighlightSpans;
    // DAP client slice 2: whether the debuggee is stopped exactly on this
    // line -- feeds both the whole-line background wash below and the
    // gutter arrow, so the two can never disagree.
    bool currentLineIsExecutionLine = false;
    // inline-diagnostics follow-up: set when the just-finished line carries
    // an annotation -- the NEXT loop iteration renders that annotation row
    // instead of a buffer line, mirroring how RowsForLine already counts it.
    std::optional<std::size_t> pendingAnnotationLine;
    // codeLens follow-up: the last line whose leading row has already been
    // painted -- unlike pendingAnnotationLine (an optional consumed the
    // very next iteration), a leading row must be checked/emitted the
    // FIRST time this loop reaches a line (segmentIndex == 0), before that
    // line's own real content, and must not re-trigger on that same
    // line's later wrap-continuation rows -- this sentinel is what tells
    // the two apart. kNoRowLine (never a real line index) starts it "no
    // line's leading row emitted yet."
    std::size_t leadingAnnotationEmittedLine = kNoRowLine;
    // prose-diagnostic-callout follow-up: recorded per screen row as the main
    // loop below paints it, then walked in one pass by
    // PaintProseDiagnosticCallouts after the loop -- kNoRowLine marks a row
    // that isn't showing any real buffer line content (an annotation row, or
    // a blank trailing row past end-of-buffer), so a callout brace never
    // mistakes one for open space or mistakes it for part of a line's own
    // span. rowContentEndColumn defaults to "fully blocked" (the row's own
    // width) for exactly that reason -- an unrecognized row must never look
    // like free room; the two branches below that leave a row's `line`
    // untouched (the annotation-row continue, and the past-end-of-buffer
    // blank-row case) explicitly correct that default where it's actually
    // known to be safe.
    // main-editor-sticky-scroll follow-up: drawn into paneCanvas (the pane's
    // full, unshifted view) first; every line of code below this point (the
    // row loop, its rowLine/rowContentEndColumn bookkeeping,
    // PaintProseDiagnosticCallouts) then works against `c`, a NEW Canvas
    // bound to a Box shifted down by however many rows were just drawn --
    // Canvas has a reference member (deleted copy-assignment), so this has
    // to be a fresh local binding, not a reassignment of paneCanvas itself;
    // that's the whole reason the parameter is named paneCanvas and not `c`
    // here. A 0-row shift is a true no-op, so this needs no branch for the
    // common "nothing pinned" case. stickyRowCount_ itself is live
    // BufferView state (not Paint()-local) -- CursorPosition()/
    // ByteOffsetForPoint() read it back so the terminal cursor, popup
    // anchors, and mouse-click row resolution all agree with what got drawn
    // this frame.
    stickyRowCount_ = PaintStickyScrollRows(paneCanvas, gutterWidth);
    Box shiftedBox = Box_();
    shiftedBox.y_min += stickyRowCount_;
    Canvas c = paneCanvas.ForBox(shiftedBox);

    std::vector<std::size_t> rowLine(static_cast<std::size_t>(c.size().height), kNoRowLine);
    std::vector<int>         rowContentEndColumn(static_cast<std::size_t>(c.size().height), c.size().width);
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            emptyBrush.ApplyTo(cell);
        }

        if (pendingAnnotationLine) {
            PaintInlineDiagnosticRow(c, row, *pendingAnnotationLine, gutterWidth);
            pendingAnnotationLine.reset();
            continue; // consumed this row; `line` already points at the next buffer line
        }

        // codeLens follow-up: checked/emitted the first time this loop
        // reaches `line` (segmentIndex == 0), BEFORE that line's own real
        // content -- the opposite ordering from pendingAnnotationLine's
        // trailing row above. Neither `line` nor `segmentIndex` advance
        // here, so the very next iteration renders this same line's real
        // first row normally; leadingAnnotationEmittedLine is what stops
        // this branch from re-triggering on that next iteration.
        if (line < renderEndLine && segmentIndex == 0 && line != leadingAnnotationEmittedLine &&
            LeadingAnnotationRowsForLine(line) > 0) {
            PaintCodeLensRow(c, row, line, gutterWidth);
            leadingAnnotationEmittedLine = line;
            continue;
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
                // inlayHint follow-up: empty when lspManager_ is unset,
                // disabled, or no response has landed for this line's range
                // yet -- InlayHintSpans itself is O(1) (no cache to poll a
                // generation counter for), so no extra staleness bookkeeping
                // is needed here unlike the tree-sitter highlight cache.
                currentLineInlayHints =
                    InlayHintsForLine(lspManager_ ? lspManager_->InlayHintSpans(buffer) : std::vector<editor::lsp::LspManager::ResolvedInlayHint>{},
                                      lineStart, lineEnd);
                // Whitespace-visualization follow-up: skipped (both fields
                // left at their "empty run" default) unless at least one of
                // the two features is on, so a default-off installation pays
                // no extra Substring-per-line cost here.
                currentLineTrailingWhitespaceStart = lineEnd;
                currentLineIndentEnd               = lineStart;
                if (editor::TrailingWhitespaceHighlightEnabled() || editor::IndentGuidesEnabled()) {
                    const std::string lineText      = content.Substring(lineStart, lineEnd - lineStart);
                    std::size_t       trailingStart = lineText.size();
                    while (trailingStart > 0 &&
                           (lineText[trailingStart - 1] == ' ' || lineText[trailingStart - 1] == '\t')) {
                        --trailingStart;
                    }
                    currentLineTrailingWhitespaceStart = lineStart + trailingStart;

                    std::size_t indentEnd = 0;
                    while (indentEnd < lineText.size() && (lineText[indentEnd] == ' ' || lineText[indentEnd] == '\t')) {
                        ++indentEnd;
                    }
                    currentLineIndentEnd = lineStart + indentEnd;
                }
                currentLineDiagnosticSpans.clear();
                for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
                    // prose-diagnostic-callout follow-up: no code-style
                    // underline for the prose/grammar checker's own
                    // diagnostics -- see PaintProseDiagnosticCallouts.
                    if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
                        continue;
                    }
                    const std::size_t spanEnd = std::max(diagnostic.endByte, diagnostic.startByte + 1);
                    if (diagnostic.startByte < lineEnd && spanEnd > lineStart) {
                        currentLineDiagnosticSpans.emplace_back(diagnostic.startByte, spanEnd);
                    }
                }
                currentLineDocumentHighlightSpans.clear();
                if (documentHighlight_ && documentHighlight_->buffer == &buffer &&
                    documentHighlight_->contentGeneration == buffer.ContentGeneration()) {
                    for (const auto& [start, end] : documentHighlight_->ranges) {
                        if (start < lineEnd && end > lineStart) {
                            currentLineDocumentHighlightSpans.emplace_back(start, end);
                        }
                    }
                }
                currentLineIsExecutionLine = dapStop && dapStop->second == line + 1; // dapStop already file-filtered above
                currentLineDiffTint.reset();
                if (diffColumnWidth > 0) {
                    const auto diffIt = std::lower_bound(diffLineKinds_.begin(), diffLineKinds_.end(), line,
                                                         [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    if (diffIt != diffLineKinds_.end() && diffIt->first == line && diffIt->second != DiffLineKind::Removed) {
                        currentLineDiffTint = diffIt->second;
                    }
                }
                currentMultibufferTint.reset();
                if (const auto* multibufferIndex = editor::multibuffer::MultibufferIndexFor(buffer)) {
                    if (const auto tint = multibufferIndex->TintForLine(line); tint != editor::multibuffer::LineTint::None) {
                        currentMultibufferTint = tint;
                    }
                }
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

                // Diff gutter markers follow-up: a changed line's own
                // number gets colored toward the accent instead of the
                // usual line-number foreground -- real visual signal
                // without ever touching the code text's own contrast
                // (revised away from a whole-line background wash, which
                // by definition fights contrast against similarly-hued
                // foreground text; a user-reported "wipes out the text"
                // complaint against exactly that approach is what drove
                // this). currentLineDiffTint is only ever set for
                // Added/Modified (never Removed -- see where it's
                // computed just above), matching the diff gutter column's
                // own choice to give Removed a distinct glyph instead.
                const Color gutterForeground = currentLineDiffTint
                                                   ? (currentLineDiffTint == DiffLineKind::Added ? Color::BrightGreen : Color::BrightBlue)
                                               : (line == pointLine) ? theme_.currentLineNumberForeground
                                                                     : theme_.lineNumberForeground;
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
                // DAP client slice 2/4: the debug-marker column -- an
                // execution arrow where the debuggee is stopped (winning
                // over a breakpoint marker on the same line: "you are
                // here" beats "you asked to stop here"), else a glyph by
                // breakpoint kind (plain/conditional/hit-count/logpoint)
                // colored by verified state. Same plain-single-width-Unicode
                // discipline as the diagnostic glyphs (▸ is the sidebar's
                // own proven disclosure triangle; ●/◆/◇/○ are from the same
                // geometric-shapes range as the scroll arrows -- DAP round 3
                // adds ◇, the open-diamond hit-count sibling of ◆'s filled
                // condition glyph).
                if (dapColumnWidth > 0) {
                    Cell& cell = c[{.x = 0, .y = row}];
                    if (currentLineIsExecutionLine) {
                        cell.character = "▸";
                        Brush{.background = theme_.background, .foreground = theme_.executionMarker, .bold = true}.ApplyTo(cell);
                    }
                    else {
                        // DAP round 4: dapBreakpoints stays sorted by the
                        // *requested* line (toggle/condition/logMessage/
                        // hitCondition all still address that) -- so a
                        // linear scan on the *display* line (actualLine when
                        // the adapter snapped it elsewhere, else line) is
                        // what actually shows a moved breakpoint where it
                        // really lands, rather than where it was toggled.
                        // Per-file breakpoint counts are small; a lower_bound
                        // can't be reused once the sort key and lookup key
                        // diverge like this.
                        const auto bpIt = std::find_if(dapBreakpoints.begin(), dapBreakpoints.end(),
                                                       [line](const editor::dap::DapManager::Breakpoint& bp) {
                                                           return (bp.actualLine != 0 ? bp.actualLine : bp.line) == line + 1;
                                                       });
                        if (bpIt != dapBreakpoints.end()) {
                            cell.character    = !bpIt->logMessage.empty()     ? "○"
                                                : !bpIt->condition.empty()    ? "◆"
                                                : !bpIt->hitCondition.empty() ? "◇"
                                                                              : "●";
                            const Color color = bpIt->verified ? theme_.breakpointMarker : theme_.unverifiedBreakpointMarker;
                            Brush{.background = theme_.background, .foreground = color}.ApplyTo(cell);
                        }
                    }
                }

                // Diff gutter markers follow-up: leftmost of the non-debug
                // regions, matching real editors' own git-gutter placement.
                // Direct Color constants, not routed through
                // Theme::BrushFor(SyntaxClass) -- same bypass the blame
                // gutter's own hash coloring already uses, for the same
                // reason (this isn't a tree-sitter capture category).
                // Drawn at diffStart -- the diff column's own x. This used
                // to (wrongly) target statusStart, where the unsaved-change
                // swatch below then unconditionally overwrote it every
                // frame, leaving the reserved diff column permanently
                // blank; found while adding the debug column and fixed on
                // request rather than silently, since the visible diff
                // styling (colored line numbers + content gradient) had
                // been tuned with the swatch invisibly absent.
                if (diffColumnWidth > 0) {
                    const auto it = std::lower_bound(diffLineKinds_.begin(), diffLineKinds_.end(), line,
                                                     [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    if (it != diffLineKinds_.end() && it->first == line) {
                        // diff-gutter-icons follow-up (was a solid color
                        // swatch for Added/Modified): vim-gitgutter's own
                        // classic glyph vocabulary -- the shape says WHAT
                        // changed, not just that something did, same
                        // reasoning as the diagnostic column's severity
                        // icons. ▔ stays for a deletion: it's already
                        // iconographic (the notch marks where the deleted
                        // lines sat, at this line's own top edge).
                        Cell& cell            = c[{.x = static_cast<int>(diffStart), .y = row}];
                        cell.background_color = theme_.background;
                        cell.bold             = true;
                        switch (it->second) {
                            case DiffLineKind::Added:
                                cell.character        = "+";
                                cell.foreground_color = Color::BrightGreen;
                                break;
                            case DiffLineKind::Modified:
                                cell.character        = "~";
                                cell.foreground_color = Color::BrightBlue;
                                break;
                            case DiffLineKind::Removed:
                                cell.character        = "▔"; // UPPER ONE EIGHTH BLOCK
                                cell.foreground_color = Color::BrightRed;
                                break;
                        }
                    }
                }

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
                    Cell&       cell = c[{.x = static_cast<int>(statusStart), .y = row}];
                    cell.character   = " ";
                    statusBrush.ApplyTo(cell);
                }

                // LSP client follow-up (was a solid color swatch like the
                // status column just above; diagnostic-gutter-icons follow-up
                // made it a real glyph): a severity-specific icon in the
                // severity's theme color, so the column says what KIND of
                // diagnostic a line has, not just that one exists. Glyphs are
                // deliberately plain single-width Unicode, not Nerd Font
                // icons or emoji -- same portability/column-math reasoning
                // ProjectSidebar's own glyph-choice comment documents; every
                // pick is from a range this codebase already renders
                // single-width somewhere (geometric shapes: ScrollArrowButton's
                // own arrows; dingbat/ASCII/Latin-1: TabBar's close icon, the
                // gutter digits themselves). See diagnosticLineSeverities_'s
                // own doc comment for why a plain binary search suffices here
                // (at most one entry per line -- the most severe -- already
                // sorted).
                if (static_cast<int>(diagnosticStart) < c.size().width) {
                    const auto it            = std::lower_bound(diagnosticLineSeverities_.begin(), diagnosticLineSeverities_.end(), line,
                                                                [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    const bool hasDiagnostic = it != diagnosticLineSeverities_.end() && it->first == line;
                    Cell&      cell          = c[{.x = static_cast<int>(diagnosticStart), .y = row}];
                    if (!hasDiagnostic) {
                        cell.character = " ";
                        Brush{.background = theme_.background, .foreground = theme_.background}.ApplyTo(cell);
                    }
                    else {
                        // Glyph choice shared with the inline annotation
                        // rows via DiagnosticGlyphFor -- see its own doc
                        // comment (was an inline switch here).
                        const DiagnosticGlyph glyph = DiagnosticGlyphFor(it->second);
                        cell.character              = glyph.glyph;
                        Brush{.background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, it->second), .bold = glyph.bold}
                            .ApplyTo(cell);
                    }
                }

                // Multibuffers follow-up: entirely skipped (not just
                // zero-width) for a buffer whose own composite line numbers
                // would be meaningless noise next to the dual old/new
                // columns already baked into a *vcs diff*-style excerpt's
                // own text -- see LineNumberGutterActive()'s own doc
                // comment. gutterDigits/digitsStart are already computed
                // as 0-width/collapsed in that case (see this function's
                // own gutterDigits/digitsStart derivation above), but the
                // digit string itself (line + 1) is never zero-width, so
                // the write loop below has to be skipped outright rather
                // than trusted to naturally emit nothing.
                if (LineNumberGutterActive()) {
                    // Vim's "relativenumber": current line keeps its real
                    // (1-indexed) number, every other visible line shows its
                    // distance from it instead.
                    const std::string number = editor::RelativeLineNumbersEnabled() && line != pointLine
                                                    ? std::to_string(line > pointLine ? line - pointLine : pointLine - line)
                                                    : std::to_string(line + 1); // 1-indexed, matches ModeLine's L/C convention
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
                // gutter-symbol-kind follow-up: one glyph on each definition
                // line, colored via the matching SyntaxClass (a function
                // definition's glyph is colored the same as a function name
                // would be in the buffer text itself) -- same sorted-by-line
                // lower_bound lookup the blame gutter below already uses.
                // Placed right before fold, matching [digits][gap][symbol]
                // [fold][blame]'s own layout comment above.
                if (symbolColumnWidth > 0 && static_cast<int>(symbolStart) < c.size().width) {
                    const auto it = std::lower_bound(symbolGutterLineKinds_.begin(), symbolGutterLineKinds_.end(), line,
                                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != symbolGutterLineKinds_.end() && it->first == line) {
                        Cell& cell     = c[{.x = static_cast<int>(symbolStart), .y = row}];
                        cell.character = SymbolGlyphFor(it->second);
                        theme_.BrushFor(editor::SyntaxClassFor(it->second)).ApplyTo(cell);
                    }
                }

                // test-runner integration: the pass/fail mark on a discovered
                // test's own first line -- symbol block's exact lookup shape.
                if (testColumnWidth > 0 && static_cast<int>(testStart) < c.size().width) {
                    const auto it = std::lower_bound(testGutterLineStatuses_.begin(), testGutterLineStatuses_.end(), line,
                                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != testGutterLineStatuses_.end() && it->first == line) {
                        Cell& cell            = c[{.x = static_cast<int>(testStart), .y = row}];
                        cell.character        = TestGlyphFor(it->second);
                        cell.foreground_color = TestStatusColor(it->second);
                        cell.bold             = true;
                    }
                }

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

                // VCS blame gutter: an 8-hex-char short commit hash per
                // blamed line, color-interpolated by commit age (newer =
                // brighter) -- a directly computed Color, not routed
                // through Theme::BrushFor(SyntaxClass), same bypass the
                // diagnostic gutter's glyph coloring already uses (see
                // SpanAtOffset's own precedent) since SyntaxClass is
                // tree-sitter-capture-oriented, not a fit for this.
                if (blameColumnWidth > 0) {
                    const auto it = std::lower_bound(blameLineInfo_.begin(), blameLineInfo_.end(), line,
                                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != blameLineInfo_.end() && it->first == line) {
                        const std::string shortHash = it->second.commitHash.substr(0, std::min<std::size_t>(8, it->second.commitHash.size()));
                        const Color       hashColor = BlameHashColor(it->second.date);
                        for (std::size_t i = 0; i < shortHash.size() && static_cast<int>(blameStart + i) < c.size().width; ++i) {
                            Cell& cell            = c[{.x = static_cast<int>(blameStart + i), .y = row}];
                            cell.character        = std::string(1, shortHash[i]);
                            cell.foreground_color = hashColor;
                            cell.background_color = theme_.background;
                        }
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

                // inlayHint follow-up: unlike the link branch above, this
                // does NOT `continue` -- a hint is virtual text alongside
                // the real byte still at offset, not a replacement for it,
                // so rendering falls through to the ordinary per-character
                // path right after. Deliberately not routed through
                // SpanAtOffset/ResolvedBrush -- a fixed dimmed/italic brush
                // (theme_.ghostTextForeground, named for this: synthetic
                // virtual text, not a real SyntaxClass). Never emits a
                // raw control byte, matching every other glyph-writing loop
                // in this function.
                if (const RenderedInlayHint* hint = InlayHintStartingAt(currentLineInlayHints, offset)) {
                    const Brush      hintBrush{.background = theme_.background, .foreground = theme_.ghostTextForeground, .italic = true};
                    std::size_t      hintTextOffset = 0;
                    const text::Rope hintRope(hint->label);
                    while (hintTextOffset < hintRope.ByteLength() && col < c.size().width) {
                        const auto glyph = hintRope.CodepointAt(hintTextOffset);
                        if (glyph.codepoint >= 0x20 && glyph.codepoint != 0x7F) {
                            Cell& cell     = c[{.x = col, .y = row}];
                            cell.character = text::EncodeCodepointUtf8(glyph.codepoint);
                            hintBrush.ApplyTo(cell);
                            ++col;
                        }
                        hintTextOffset += glyph.byteLength;
                    }
                }

                const auto decoded = content.CodepointAt(offset);

                // Multi-cursor phase: a secondary caret renders as an
                // inverted cell (ScrollBar's own thumb technique) -- theme-
                // independent, and visually distinct from the primary's real
                // terminal cursor. Only the first cell of a multi-column
                // glyph (tab expansion, control placeholder) inverts.
                const bool secondaryCaretHere = IsSecondaryCursorAt(offset);

                const editor::HighlightSpan span  = SpanAtOffset(lineSpans, offset);
                Brush                       brush = ResolvedBrush(span.syntaxClass, span.captureId);
                // diagnostics-UX follow-up: underline exactly the span the
                // server flagged -- a non-disruptive "the problem is HERE"
                // cue on top of whatever syntax color the cell already has
                // (foreground deliberately untouched: recoloring would fight
                // the highlighting the way the first diff-tint attempt did).
                for (const auto& [spanStart, spanEnd] : currentLineDiagnosticSpans) {
                    if (offset >= spanStart && offset < spanEnd) {
                        brush.underlined = true;
                        break;
                    }
                }
                if (InIsearchMatch(offset)) {
                    brush.background = theme_.isearchMatchBackground;
                }
                else if (InActiveSnippetField(offset)) {
                    brush.background = theme_.snippetFieldBackground;
                }
                else if (InSelection(offset)) {
                    brush.background = theme_.selectionBackground;
                }
                else if (std::any_of(currentLineDocumentHighlightSpans.begin(), currentLineDocumentHighlightSpans.end(),
                                     [offset](const auto& span) { return offset >= span.first && offset < span.second; })) {
                    // documentHighlight follow-up: a read-only cue on the
                    // symbol under point's other occurrences -- loses to
                    // isearch/snippet-field/selection above (all explicit
                    // user actions), but wins over the execution-line/
                    // multibuffer/trailing-whitespace washes below (this is
                    // still a direct answer to "what does point currently
                    // mean", a stronger signal than those cosmetic washes).
                    brush.background = theme_.documentHighlightBackground;
                }
                else if (currentLineIsExecutionLine) {
                    // DAP client slice 2: the stopped line's own wash --
                    // loses to isearch/selection above (both are explicit
                    // user actions). A changed line's content area gets no
                    // tint of its own anymore (the two-column gradient that
                    // used to live here was removed per user feedback) --
                    // the diff gutter glyph plus the accent-colored line
                    // number carry the whole signal; currentLineDiffTint
                    // survives only for the latter.
                    brush.background = theme_.executionLineBackground;
                }
                else if (currentMultibufferTint) {
                    // Multibuffers follow-up: unlike the live diff gutter's
                    // content area (deliberately left untinted after user
                    // feedback that a whole-line wash fights syntax-
                    // highlighted text -- see the comment two cases above),
                    // this buffer's excerpt lines carry no syntax
                    // highlighting to fight in the first place, so a real
                    // background wash is safe and is the whole point of
                    // this dedicated diff view. Header/Rule get no
                    // background at all -- bold text and box-drawing
                    // glyphs are already visually distinct on their own
                    // (an "ASCII outline" follow-up ask), a wash would just
                    // fight the rule glyph's own default color.
                    switch (*currentMultibufferTint) {
                        case editor::multibuffer::LineTint::Added:
                            brush.background = theme_.diffAddedBackground;
                            break;
                        case editor::multibuffer::LineTint::Removed:
                            brush.background = theme_.diffRemovedBackground;
                            break;
                        case editor::multibuffer::LineTint::Header:
                            brush.bold = true;
                            break;
                        case editor::multibuffer::LineTint::Rule:
                        case editor::multibuffer::LineTint::None:
                            break;
                    }
                }
                else if (editor::TrailingWhitespaceHighlightEnabled() && offset >= currentLineTrailingWhitespaceStart) {
                    // Whitespace-visualization follow-up: lowest priority of
                    // this chain, same reasoning as every case above it --
                    // an active isearch/selection/execution/multibuffer
                    // overlay always wins over this purely cosmetic wash.
                    // offset >= currentLineTrailingWhitespaceStart already
                    // guarantees this cell is a space/tab (see that field's
                    // own doc comment), so no codepoint check is needed here.
                    brush.background = theme_.trailingWhitespaceBackground;
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
                    const int  tabWidth = editor::TabWidth();
                    const bool inIndent = editor::IndentGuidesEnabled() && offset < currentLineIndentEnd;
                    for (int i = 0; i < tabWidth && col < c.size().width; ++i) {
                        Cell&     cell          = c[{.x = col, .y = row}];
                        const int displayColumn = col - static_cast<int>(gutterWidth);
                        if (inIndent && displayColumn > 0 && displayColumn % tabWidth == 0) {
                            // Whitespace-visualization follow-up: a guide
                            // glyph in place of one of the expanded tab's
                            // space cells, at each indent-width column --
                            // see currentLineIndentEnd's own doc comment.
                            cell.character        = text::EncodeCodepointUtf8(kIndentGuide);
                            Brush guideBrush      = brush;
                            guideBrush.foreground = IndentGuideColor(theme_, displayColumn, tabWidth);
                            guideBrush.ApplyTo(cell);
                        }
                        else {
                            cell.character = " ";
                            brush.ApplyTo(cell);
                        }
                        if (i == 0 && secondaryCaretHere) {
                            cell.inverted = true;
                        }
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
                    Brush binaryBrush             = brush;
                    binaryBrush.foreground        = theme_.binaryForeground;
                    const char32_t glyphs[4]      = {kBinaryOpen, HexDigit((decoded.codepoint >> 4) & 0xF),
                                                     HexDigit(decoded.codepoint & 0xF), kBinaryClose};
                    bool           firstGlyphCell = true;
                    for (const char32_t glyph : glyphs) {
                        if (col >= c.size().width) {
                            break;
                        }
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        binaryBrush.ApplyTo(cell);
                        if (firstGlyphCell && secondaryCaretHere) {
                            cell.inverted  = true;
                            firstGlyphCell = false;
                        }
                        ++col;
                    }
                }
                else {
                    Cell&     cell          = c[{.x = col, .y = row}];
                    const int displayColumn = col - static_cast<int>(gutterWidth);
                    // offset < currentLineIndentEnd here only ever holds for
                    // a space cell (see that field's own doc comment: the
                    // scan that computes it stops at the first non-space/tab
                    // byte), so decoded.codepoint is guaranteed U' ' whenever
                    // this substitutes a guide glyph.
                    if (editor::IndentGuidesEnabled() && offset < currentLineIndentEnd && displayColumn > 0 &&
                        displayColumn % editor::TabWidth() == 0) {
                        cell.character        = text::EncodeCodepointUtf8(kIndentGuide);
                        Brush guideBrush      = brush;
                        guideBrush.foreground = IndentGuideColor(theme_, displayColumn, editor::TabWidth());
                        guideBrush.ApplyTo(cell);
                    }
                    else {
                        cell.character = text::EncodeCodepointUtf8(decoded.codepoint);
                        brush.ApplyTo(cell);
                    }
                    if (secondaryCaretHere) {
                        cell.inverted = true;
                    }
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
            // Multi-cursor phase: a secondary caret sitting exactly at this
            // line's content end (the newline position -- end-of-line motion
            // parks cursors there constantly) has no codepoint cell of its
            // own above; invert the first padding cell instead. Last
            // segment only: a mid-wrap segment's endByte is the next
            // segment's startByte, which the content loop already covers.
            if (segmentIndex + 1 == lineSegments.size() && col < c.size().width &&
                IsSecondaryCursorAt(currentSegment.endByte)) {
                c[{.x = col, .y = row}].inverted = true;
            }

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

            // completion-popup follow-up: completion no longer paints
            // anything inline here -- ActiveCompletion renders via a real
            // ListPopup overlay instead (NotifyCompletionChanged, called at
            // every activeCompletion_ mutation site plus once per Paint()
            // below when point's on-screen position has moved).

            // prose-diagnostic-callout follow-up: this row's real content
            // ends at `col` (everything painted above it -- text, truncation
            // indicator, fold ellipsis/preview -- has already advanced it)
            // -- see rowLine/rowContentEndColumn's own doc comment above the
            // row loop.
            rowLine[row]             = line;
            rowContentEndColumn[row] = col;

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
                // inline-diagnostics follow-up: the annotation row renders
                // after the line's LAST wrap row, before the next line.
                if (AnnotationRowsForLine(line) > 0) {
                    pendingAnnotationLine = line;
                }
                segmentIndex = 0;
                line         = NextVisibleLine(line + 1, renderEndLine);
            }
        }
        else {
            // prose-diagnostic-callout follow-up: past end-of-buffer -- this
            // row was already blanked by the top-of-loop wash, genuinely
            // empty past the gutter, so a callout brace may use it as
            // padding.
            rowContentEndColumn[row] = static_cast<int>(gutterWidth);
            line                     = NextVisibleLine(line + 1, renderEndLine);
        }
    }

    PaintProseDiagnosticCallouts(c, rowLine, rowContentEndColumn, gutterWidth);

    // completion-popup follow-up: activeCompletion_ mutation sites already
    // notify onCompletionChanged_ directly (NotifyCompletionChanged), but
    // none of them fire on a pure scroll -- nothing about ActiveCompletion
    // itself changes when the view scrolls, only where point now renders.
    // This is the cheap per-frame catch-up for exactly that case: recompute
    // the anchor and re-notify only when it actually moved (including
    // moving to/from "off screen", i.e. std::nullopt) -- comparing anchors
    // first (cheap: two field reads) avoids rebuilding/resending the whole
    // popup model on every ordinary repaint while nothing about it changed.
    if (activeCompletion_ && CompletionAnchorNow() != lastNotifiedCompletionAnchor_) {
        NotifyCompletionChanged();
    }
}

void BufferView::PaintInlineDiagnosticRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth) {
    const auto it = inlineDiagnosticsByLine_.find(line);
    if (it == inlineDiagnosticsByLine_.end()) {
        return; // shouldn't happen (RowsForLine and Paint share the cache within one frame) -- leave the blanked row
    }
    const InlineDiagnostic& diagnostic = it->second;

    // Annotation styling is deliberately NOT plain severity-colored text --
    // a user report caught exactly that reading as ordinary code (the
    // warning amber sits right next to similarly-warm syntax hues): the
    // message renders in italic, and the severity's own gutter glyph is
    // repeated between the carets and the message, so an annotation row is
    // recognizable as one at a glance in any theme.
    const Color color = DiagnosticSeverityColor(theme_, diagnostic.severity);
    const Brush caretBrush{.background = theme_.background, .foreground = color, .bold = true};
    const Brush messageBrush{.background = theme_.background, .foreground = color, .italic = true};

    const int width = c.size().width;
    int       col   = static_cast<int>(gutterWidth);

    // Carets under the diagnostic's visual span -- only when the column
    // positions on the row above are trustworthy: wrap off (the annotation
    // sits below the line's LAST wrap row, where first-row column math
    // would lie) and the span's start still on-screen horizontally.
    if (!EffectiveWrapLines()) {
        const text::Buffer& buffer    = activeBuffer_.Get();
        const text::ITextStorage&   content   = buffer.Content();
        const std::size_t   lineStart = content.LineToByteOffset(line);
        const std::size_t   lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        EnsureLinkCache();
        const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());

        // Same leftColumn_-aware bound/offset arithmetic CursorPosition uses.
        const int                bound = width + static_cast<int>(leftColumn_);
        const std::optional<int> startCol =
            VisualColumn(content, lineStart, std::min(diagnostic.startByte, lineEnd), bound, lineLinks);
        if (startCol && *startCol >= static_cast<int>(leftColumn_)) {
            const std::optional<int> endCol =
                VisualColumn(content, lineStart, std::min(diagnostic.endByte, lineEnd), bound, lineLinks);
            const int screenStart = static_cast<int>(gutterWidth) + *startCol - static_cast<int>(leftColumn_);
            // A span running past the visual-column bound (endCol nullopt)
            // degrades to a single caret at its start rather than flooding
            // the row -- the message is the more useful content to keep.
            const int caretCount = endCol ? std::max(1, *endCol - *startCol) : 1;
            for (int i = 0; i < caretCount && screenStart + i < width; ++i) {
                Cell& cell     = c[{.x = screenStart + i, .y = row}];
                cell.character = "^";
                caretBrush.ApplyTo(cell);
            }
            col = std::min(screenStart + caretCount, width) + 1;
        }
    }

    // The severity's gutter glyph, repeated here so the annotation carries
    // the same iconography the gutter column uses (see the styling comment
    // above) -- then the message, truncated at the viewport; byte-per-cell,
    // the same ASCII-ish rendering simplification ModeLine's own text
    // documents.
    const DiagnosticGlyph glyph = DiagnosticGlyphFor(diagnostic.severity);
    if (col < width) {
        Cell& cell     = c[{.x = col, .y = row}];
        cell.character = glyph.glyph;
        Brush{.background = theme_.background, .foreground = color, .bold = glyph.bold}.ApplyTo(cell);
        col += 2; // glyph, then one separating space
    }
    for (const char ch : diagnostic.message) {
        if (col >= width) {
            break;
        }
        Cell& cell     = c[{.x = col, .y = row}];
        cell.character = std::string(1, ch);
        messageBrush.ApplyTo(cell);
        ++col;
    }
}

namespace {
    // prose-diagnostic-callout follow-up: one Prose diagnostic reduced to
    // just what PaintProseDiagnosticCallouts' clustering/rendering passes
    // need -- computed once per diagnostic in the gathering pass below, then
    // read repeatedly (never re-derived) by both.
    struct ProseCalloutItem {
        int                                firstRow;
        int                                lastRow;
        int                                messageRow; // the flagged block's own middle row
        text::Buffer::Diagnostic::Severity severity;
        std::string                        message; // first line only, see EnsureInlineDiagnosticCache's own precedent
    };
} // namespace

void BufferView::PaintProseDiagnosticCallouts(Canvas& c, const std::vector<std::size_t>& rowLine,
                                              const std::vector<int>& rowContentEndColumn, std::size_t gutterWidth) {
    const int height = c.size().height;
    const int width  = c.size().width;

    // First/last screen row currently showing each buffer line's content --
    // see this method's own header-comment for why an entry's absence here
    // (a line scrolled/folded out of view) means "skip this diagnostic's
    // callout entirely," not "clamp it."
    std::unordered_map<std::size_t, std::pair<int, int>> lineRowRange;
    for (int row = 0; row < height; ++row) {
        if (rowLine[row] == kNoRowLine) {
            continue;
        }
        auto [it, inserted] = lineRowRange.try_emplace(rowLine[row], row, row);
        if (!inserted) {
            it->second.first  = std::min(it->second.first, row);
            it->second.second = std::max(it->second.second, row);
        }
    }
    if (lineRowRange.empty()) {
        return;
    }

    const text::Buffer& buffer     = activeBuffer_.Get();
    const text::ITextStorage&   content    = buffer.Content();
    const std::size_t   byteLength = content.ByteLength();
    const auto&         glyphs     = RoundedBorderGlyphs();

    // Gathering pass: every on-screen Prose diagnostic reduced to a
    // ProseCalloutItem, sorted by its own firstRow -- the clustering pass
    // right below needs that ordering to do a single linear merge.
    std::vector<ProseCalloutItem> items;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Prose) {
            continue;
        }

        const std::size_t startByte = std::min(diagnostic.startByte, byteLength);
        const std::size_t lastByte =
            diagnostic.endByte > diagnostic.startByte ? std::min(diagnostic.endByte - 1, byteLength) : startByte;
        const std::size_t firstLine = content.ByteOffsetToLine(startByte);
        const std::size_t lastLine  = content.ByteOffsetToLine(lastByte);

        const auto firstIt = lineRowRange.find(firstLine);
        const auto lastIt  = lineRowRange.find(lastLine);
        if (firstIt == lineRowRange.end() || lastIt == lineRowRange.end()) {
            continue; // the flagged block isn't fully on screen -- gutter icon + bottom hint carry it instead
        }

        const int firstRow = firstIt->second.first;
        const int lastRow  = lastIt->second.second;
        if (firstRow > lastRow) {
            continue; // defensive -- shouldn't happen, rows only ever advance with `line`
        }

        items.push_back(ProseCalloutItem{
            .firstRow   = firstRow,
            .lastRow    = lastRow,
            .messageRow = firstRow + (lastRow - firstRow) / 2,
            .severity   = diagnostic.severity,
            .message    = diagnostic.message.substr(0, diagnostic.message.find('\n')),
        });
    }
    if (items.empty()) {
        return;
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.firstRow < b.firstRow; });

    // Clustering pass: a run of items whose own [firstRow, lastRow] blocks
    // sit within one row of each other (touching once each gets its usual
    // 1-row corner padding) shares a single spine instead of each getting
    // its own separate corners -- exactly the "group them together when
    // there's a whole bunch" a page of clustered comment/prose diagnostics
    // otherwise produces. clusterStart/clusterEnd index into `items`
    // ([start, end)); clusterLastRow tracks the running max lastRow seen so
    // far in the open cluster (items are sorted by firstRow, not lastRow,
    // so a later item's own block can still end earlier than an in-progress
    // one -- max, not simply the latest item's own lastRow).
    std::size_t clusterStart   = 0;
    int         clusterLastRow = items[0].lastRow;
    for (std::size_t i = 1; i <= items.size(); ++i) {
        const bool endOfRun = i == items.size() || items[i].firstRow > clusterLastRow + 2;
        if (!endOfRun) {
            clusterLastRow = std::max(clusterLastRow, items[i].lastRow);
            continue;
        }

        const std::size_t clusterEnd      = i;
        const int         clusterFirstRow = items[clusterStart].firstRow;
        const int         topRow          = clusterFirstRow > 0 ? clusterFirstRow - 1 : clusterFirstRow;
        const int         bottomRow       = clusterLastRow + 1 < height ? clusterLastRow + 1 : clusterLastRow;

        int anchorCol = static_cast<int>(gutterWidth);
        for (int row = topRow; row <= bottomRow; ++row) {
            anchorCol = std::max(anchorCol, rowContentEndColumn[row]);
        }
        anchorCol += 2; // small gap between the pane's own text and the callout

        constexpr int kBranchOverhead  = 3; // "├─ "
        constexpr int kMinMessageChars = 6; // not worth drawing a callout that can't show a few real words
        if (anchorCol + kBranchOverhead + kMinMessageChars <= width) {
            // Most-severe item in the cluster colors its shared spine
            // (corners + vertical bar) -- same "most severe wins" precedent
            // EnsureDiagnosticGutterCache's own per-line collapse already
            // uses; each branch row keeps its OWN item's severity color,
            // same as before clustering existed.
            std::size_t mostSevere = clusterStart;
            for (std::size_t i2 = clusterStart + 1; i2 < clusterEnd; ++i2) {
                if (DiagnosticSeverityRank(items[i2].severity) > DiagnosticSeverityRank(items[mostSevere].severity)) {
                    mostSevere = i2;
                }
            }
            const Brush spineBrush{
                .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, items[mostSevere].severity), .bold = true};

            // Row -> the (possibly several, on a messageRow collision)
            // items landing on it -- built once per cluster rather than
            // scanning all of clusterStart..clusterEnd per row.
            std::unordered_map<int, std::vector<std::size_t>> itemsByMessageRow;
            for (std::size_t i2 = clusterStart; i2 < clusterEnd; ++i2) {
                itemsByMessageRow[items[i2].messageRow].push_back(i2);
            }

            for (int row = topRow; row <= bottomRow; ++row) {
                const auto rowIt = itemsByMessageRow.find(row);
                if (rowIt != itemsByMessageRow.end()) {
                    const ProseCalloutItem& first   = items[rowIt->second.front()];
                    std::string             message = first.message;
                    if (rowIt->second.size() > 1) {
                        message += " (+" + std::to_string(rowIt->second.size() - 1) + " more)";
                    }
                    const Brush branchBrush{
                        .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, first.severity), .bold = true};
                    const Brush messageBrush{
                        .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, first.severity), .italic = true};

                    int col = anchorCol;
                    for (const char32_t glyph : {U'├', glyphs.horizontal}) {
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        branchBrush.ApplyTo(cell);
                        ++col;
                    }
                    if (col < width) {
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = " ";
                        branchBrush.ApplyTo(cell);
                        ++col;
                    }
                    for (const char ch : message) {
                        if (col >= width) {
                            break;
                        }
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = std::string(1, ch);
                        messageBrush.ApplyTo(cell);
                        ++col;
                    }
                }
                else {
                    const char32_t glyph = (row == topRow)      ? glyphs.topRight
                                           : (row == bottomRow) ? glyphs.bottomRight
                                                                : glyphs.vertical;
                    Cell&          cell  = c[{.x = anchorCol, .y = row}];
                    cell.character       = text::EncodeCodepointUtf8(glyph);
                    spineBrush.ApplyTo(cell);
                }
            }
        }
        // else: no room -- drop the whole cluster, relying on the gutter
        // icon + bottom hint, matching a single dropped callout's own
        // "otherwise the hint is enough" fallback.

        if (i < items.size()) {
            clusterStart   = i;
            clusterLastRow = items[i].lastRow;
        }
    }
}

std::optional<Point> BufferView::CursorPosition() const {
    // A pure, independently-callable query, deliberately NOT a value cached
    // as a Paint() side effect -- main.cpp's render() calls this once,
    // separately, after the whole widget tree's Paint() pass has already
    // completed for the frame. Cheap enough to recompute on every call --
    // buffer/content access, one GutterWidth() call, one
    // ByteOffsetToLine/LineToByteOffset pair, one bounded VisualColumn scan
    // -- nowhere near Paint()'s own per-visible-row cost.
    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::ITextStorage&   content     = buffer.Content();
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

    // size() is still its default-constructed {0,0} if this is called
    // before SetBox_ has ever run on this widget (e.g. a headless caller
    // querying CursorPosition() before any Paint() pass) -- treating an
    // unknown ({0,0}) size as "don't bound at all" rather than "assume
    // everything is off-screen" is what keeps the cursor visible in that
    // case instead of appearing to not exist.
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

    // main-editor-sticky-scroll follow-up: visibleRow is still relative to
    // topLine_'s own screen row (row 0) -- stickyRowCount_ is added to the
    // final Point below, once, rather than threaded through every
    // intermediate row computation above; the bound check here has to widen
    // by the same amount first, or a point that's genuinely still on screen
    // (just pushed down by the pinned rows) would be wrongly reported as
    // off-screen.
    const std::size_t visibleRow = VisibleRowCountBetween(topLine_, pointLine) + rowWithinLine;
    if (sizeIsKnown && visibleRow + static_cast<std::size_t>(stickyRowCount_) >= static_cast<std::size_t>(sizeNow.height)) {
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
    return Point{.x = static_cast<int>(col), .y = static_cast<int>(visibleRow) + stickyRowCount_};
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
    if (inputMode_ == InputMode::ConfirmOverwriteSave) {
        HandleConfirmOverwriteSaveKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ConfirmSaveWithConflicts) {
        HandleConfirmSaveWithConflictsKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ConfirmOpenBinary) {
        HandleConfirmOpenBinaryKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ConfirmTrustProjectInit) {
        HandleConfirmTrustProjectInitKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::FindFile ||
        inputMode_ == InputMode::ProjectSearch || inputMode_ == InputMode::CreateDirectory ||
        inputMode_ == InputMode::FindScratch || inputMode_ == InputMode::StringRectangle ||
        inputMode_ == InputMode::SetHeadlineTags || inputMode_ == InputMode::TaskName ||
        inputMode_ == InputMode::GotoLine ||
        inputMode_ == InputMode::AcpPromptText ||
        // OnKeyEvent-dispatch-gap follow-up: DapEvaluate/VcsCreateBranch are
        // both handled inside HandlePromptKey (and documented there as
        // routing through it, same shape as TaskName/GotoLine above) but
        // were never actually reachable from a real keystroke -- missing
        // here, so input silently fell through to ordinary self-insert-command
        // instead of the prompt.
        inputMode_ == InputMode::DapEvaluate ||
        inputMode_ == InputMode::VcsCreateBranch || inputMode_ == InputMode::DeleteProperty ||
        inputMode_ == InputMode::OrgSchedule || inputMode_ == InputMode::OrgDeadline ||
        // DAP round 2: same dispatch-gap fix as DapEvaluate above -- these
        // four are HandlePromptKey-routed plain-text prompts too. DAP round
        // 3 adds two more of the same shape.
        inputMode_ == InputMode::DapBreakpointCondition || inputMode_ == InputMode::DapBreakpointLogMessage ||
        inputMode_ == InputMode::DapAddWatch || inputMode_ == InputMode::DapSetVariableValue ||
        inputMode_ == InputMode::DapBreakpointHitCondition || inputMode_ == InputMode::DapFunctionBreakpointName ||
        inputMode_ == InputMode::DapMemoryByteCount ||
        // editor-ergonomics follow-up: BookmarkSetName is a plain-text
        // prompt too, TaskName/GotoLine's own shape.
        inputMode_ == InputMode::BookmarkSetName ||
        // named-projects follow-up: OpenProjectPath/OpenProjectName are both
        // routed through this shared chain too -- FindFile's own shape
        // (plus real path completion) and BookmarkSetName's own shape,
        // respectively.
        inputMode_ == InputMode::OpenProjectPath || inputMode_ == InputMode::OpenProjectName) {
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
    if (inputMode_ == InputMode::SetProperty) {
        HandleSetPropertyKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::RecoverFile) {
        HandleRecoverFileKey(*chord);
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
    if (inputMode_ == InputMode::FindRecentFile) {
        HandleFindRecentFileKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::SwitchProject) {
        HandleSwitchProjectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::SwitchToBuffer) {
        HandleSwitchToBufferKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::VcsSwitchBranch) {
        HandleVcsSwitchBranchKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::AcpAgentName) {
        HandleAcpAgentNameKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::BookmarkJump) {
        HandleBookmarkJumpKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::SelectTheme) {
        HandleSelectThemeKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::PointToRegister || inputMode_ == InputMode::JumpToRegister ||
        inputMode_ == InputMode::CopyToRegister || inputMode_ == InputMode::InsertRegister) {
        HandleRegisterKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::ZapToChar) {
        HandleZapToCharKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::OrgCaptureSelectTemplate) {
        HandleOrgCaptureKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::AcpPermissionPrompt) {
        HandleAcpPermissionPromptKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::DapThreadSelect) {
        HandleDapThreadSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::DapExceptionFilterSelect) {
        HandleDapExceptionFilterSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspCodeActionSelect) {
        HandleCodeActionSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspGotoDefinitionSelect) {
        HandleDefinitionSelectKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspGotoSymbol) {
        // Same "Enter jumps directly, no RunCommandAndHandleOutcome routing"
        // shape as ProjectFindFile above.
        HandleDocumentSymbolKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspWorkspaceSymbol) {
        HandleWorkspaceSymbolKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::LspRenameNewName) {
        HandlePromptKey(*chord);
        ClampPointToNarrowing();
        return true;
    }
    if (inputMode_ == InputMode::PrefixArgument) {
        // No ClampPointToNarrowing(): reading a prefix argument never moves
        // point itself -- a Continue outcome does nothing to the buffer, and
        // a Terminate outcome re-dispatches through DispatchChordNormally,
        // which already runs the same clamp any other normal dispatch does.
        HandlePrefixArgumentKey(*chord);
        return true;
    }
    if (inputMode_ == InputMode::Snippet) {
        // No ClampPointToNarrowing() here for the same PrefixArgument
        // reason: consumed chords clamp inside HandleSnippetKey themselves,
        // and a fall-through chord re-dispatches through
        // DispatchChordNormally, after which *this* may be destroyed.
        HandleSnippetKey(*chord);
        return true;
    }

    // completion-popup follow-up (was hover/completion follow-up): completion
    // state only ever exists while inputMode_ == Normal (every branch above
    // returns before reaching here), so this is the one place it needs
    // handling -- Tab accepts, Up/Down/M-n/M-p cycle (arrows for the popup's
    // own standard navigation, M-n/M-p kept for existing muscle memory --
    // plain arrows are otherwise free here, multi-cursor's add-cursor-above/
    // -below use C-Up/C-Down), and (falling through) any other key dismisses
    // it, then continues to whatever that key would ordinarily do. Checked
    // ahead of the normal dispatch below so none of these ever reach
    // Dispatcher::Feed while the popup is showing.
    if (activeCompletion_) {
        if (chord->Special == editor::SpecialKey::Tab && !chord->Control && !chord->Meta) {
            AcceptActiveCompletion();
            ClampPointToNarrowing();
            return true;
        }
        if ((chord->Special == editor::SpecialKey::Down && !chord->Control && !chord->Meta) ||
            (chord->Meta && !chord->Control && chord->Codepoint == U'n')) {
            CycleActiveCompletion(1);
            return true;
        }
        if ((chord->Special == editor::SpecialKey::Up && !chord->Control && !chord->Meta) ||
            (chord->Meta && !chord->Control && chord->Codepoint == U'p')) {
            CycleActiveCompletion(-1);
            return true;
        }
        activeCompletion_.reset();
        NotifyCompletionChanged();
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

    if (editor::vim::VimModeEnabled()) {
        return HandleVimKey(*chord);
    }
    return DispatchChordNormally(*chord);
}

bool BufferView::HandleVimKey(const editor::KeyChord& chord) {
    if (vimEngine_.CurrentMode() == editor::vim::Mode::Insert) {
        if (IsQuit(chord)) {
            vimEngine_.ExitInsertToNormal(activeBuffer_.Get());
        }
        else {
            vimEngine_.RecordInsertKey(chord);
            // Vim's own Insert-mode Ctrl-chords (C-w/C-u/C-t/C-d/C-r) get first look --
            // otherwise they'd fall through to ned's ordinary Emacs bindings for the same
            // chords (kill-region, universal-argument, isearch-backward, ...), which is
            // not what a vim user typing C-w expects. See VimEngine::HandleInsertModeChord's
            // own doc comment for why this isn't just another KeymapStack layer.
            if (!vimEngine_.HandleInsertModeChord(activeBuffer_.Get(), chord)) {
                DispatchChordNormally(chord);
            }
        }
    }
    else {
        vimEngine_.SetViewport(topLine_, size().height > 0 ? static_cast<std::size_t>(size().height) : 0);
        vimEngine_.HandleKey(activeBuffer_.Get(), chord);
    }

    const editor::vim::PendingIntent intent = vimEngine_.TakePendingIntent();
    if (intent == editor::vim::PendingIntent::Quit) {
        if (eventLoop_) {
            eventLoop_->Exit();
        }
        return true;
    }
    if (intent == editor::vim::PendingIntent::CloseBuffer) {
        RequestCloseBuffer(activeBuffer_.Get()); // may destroy *this* -- nothing after
        return true;
    }

    // vim-global-marks follow-up: an uppercase-mark jump into a different file than the
    // one currently open -- VimEngine can't switch buffers itself (deliberately
    // UI-free), so it hands the target back here. Same open-then-jump shape
    // HandleBookmarkJumpKey already uses for its own (path, line, column) targets.
    if (const auto jump = vimEngine_.TakePendingBufferJump()) {
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(jump->path);
            activeBuffer_.Set(opened);
            opened.SetPoint(opened.ByteOffsetForLineAndColumn(jump->line, jump->column, static_cast<std::size_t>(editor::TabWidth())));
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    }

    if (!vimEngine_.StatusText().empty()) {
        statusMessage_ = vimEngine_.StatusText();
    }
    else if (vimEngine_.CurrentMode() != editor::vim::Mode::Normal) {
        statusMessage_ = "-- " + vimEngine_.ModeIndicator() + " --";
    }
    else {
        statusMessage_.clear();
    }
    ClampPointToNarrowing();
    // Applied before ScrollToShowPoint() -- zz/zt/zb/C-e/C-y request an explicit topLine_
    // independent of point, and ScrollToShowPoint() only nudges topLine_ far enough to
    // keep point visible, so it leaves an already-visible point's explicit recenter alone.
    if (const auto pendingTop = vimEngine_.TakePendingTopLine()) {
        SetTopLine(*pendingTop);
    }
    ScrollToShowPoint();
    return true;
}

bool BufferView::DispatchChordNormally(const editor::KeyChord& chord) {
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
    attemptedSequence.push_back(chord);

    editor::Dispatcher::Outcome outcome = editor::Dispatcher::Outcome::Unbound;
    editor::CommandContext      context = MakeContext();
    context.viewportHeight              = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    // prefix-argument follow-up: hand this one dispatch attempt the pending
    // value and clear the member up front -- Dispatcher::Feed decides
    // whether it was actually consumed (Invoked/NoMatch) or must survive
    // (Pending, a multi-chord sequence still in progress), restored below in
    // the branch the comment beneath already establishes is safe to still
    // touch `this` in. An Invoked outcome can synchronously destroy *this*
    // (window-management commands), so nothing after that may write to a
    // member -- pre-clearing here means the Invoked/consumed case doesn't
    // need to.
    context.prefixArg = pendingPrefixArg_;
    pendingPrefixArg_.reset();
    // UAF follow-up: a copy, not a reference to the member -- the ran==true
    // branch below fires this *after* RunCommandAndHandleOutcome, and an
    // Invoked outcome can synchronously destroy *this* (window-management
    // commands like delete-window closing this very pane's BufferView).
    // Reading onPrefixHintChanged_ off `this` there was a genuine
    // heap-use-after-free, confirmed live under ASan
    // ("delete-window on the focused pane in a 2-window split focuses the
    // survivor" reproduces it deterministically) -- this local copy is safe
    // to call regardless of whether *this* still exists by then.
    const std::function<void(std::optional<WhichKeyHint>)> onPrefixHintChangedCopy = onPrefixHintChanged_;
    const bool ran = RunCommandAndHandleOutcome(
        context,
        [&] {
            outcome = dispatcher_.Feed(chord, context);
            return outcome == editor::Dispatcher::Outcome::Invoked;
        },
        &chord);

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
            pendingPrefixArg_ = context.prefixArg;                                      // Feed leaves it untouched mid-sequence -- keep it alive
            statusMessage_    = editor::FormatKeySequence(dispatcher_.Pending()) + "-"; // matches real Emacs' own "C-x-" while-waiting convention
            if (onPrefixHintChanged_) {
                onPrefixHintChanged_(editor::WhichKeyEnabled() ? std::optional(BuildWhichKeyHint()) : std::nullopt);
            }
        }
        else if (outcome == editor::Dispatcher::Outcome::Unbound) {
            statusMessage_ = editor::FormatKeySequence(attemptedSequence) + " is undefined";
            if (onPrefixHintChanged_) {
                onPrefixHintChanged_(std::nullopt);
            }
        }
    }
    else if (onPrefixHintChangedCopy) {
        onPrefixHintChangedCopy(std::nullopt);
    }
    return true;
}

WhichKeyHint BufferView::BuildWhichKeyHint() const {
    WhichKeyHint hint;
    hint.prefixLabel = editor::FormatKeySequence(dispatcher_.Pending()) + "-"; // matches statusMessage_'s own convention above
    for (const auto& child : dispatcher_.Keymaps().ChildrenAt(dispatcher_.Pending())) {
        hint.bindings.emplace_back(editor::FormatKeyChord(child.chord), child.commandName.value_or("..."));
    }
    return hint;
}

void BufferView::HandlePrefixArgumentKey(const editor::KeyChord& chord) {
    const editor::PrefixArgumentReader::Outcome outcome = prefixArgReader_->HandleKey(chord);
    if (outcome == editor::PrefixArgumentReader::Outcome::Continue) {
        statusMessage_ = prefixArgReader_->StatusText();
        return;
    }
    // Terminate: chord doesn't belong to prefix-argument syntax. Capture the
    // resolved value for the next dispatch, leave PrefixArgument mode, and
    // re-run this same chord through the identical path any other
    // Normal-mode keystroke goes through -- MakeContext() (called inside
    // DispatchChordNormally) picks pendingPrefixArg_ up from there.
    //
    // Keyboard-macro recording note: like isearch/query-replace, keystrokes
    // consumed here (the C-u itself excepted -- it's a normal single-chord
    // binding, so it does go through Dispatcher::Feed and gets recorded)
    // aren't individually recorded into an in-progress macro; only this
    // final re-dispatched chord is. Same pre-existing limitation every other
    // multi-keystroke InputMode session already has.
    pendingPrefixArg_ = prefixArgReader_->Value();
    prefixArgReader_.reset();
    inputMode_ = InputMode::Normal;
    DispatchChordNormally(chord);
}

void BufferView::HandleSnippetKey(const editor::KeyChord& chord) {
    text::Buffer* buffer = ResolveSnippetBuffer();
    if (buffer == nullptr) {
        // Session buffer closed out from under the session (or no session at
        // all -- shouldn't happen, but never leave the mode wedged).
        EndSnippetSession();
        return;
    }

    const bool plainTab = chord.Special == editor::SpecialKey::Tab && !chord.Control && !chord.Meta;
    if ((plainTab && !chord.Shift) || (chord.Meta && !chord.Control && chord.Codepoint == U'n')) {
        if (snippetSession_->NextField(*buffer) == editor::SnippetSession::NavResult::Finished) {
            EndSnippetSession();
            statusMessage_.clear();
        }
        else {
            statusMessage_ = snippetSession_->StatusText();
        }
        ClampPointToNarrowing();
        ScrollToShowPoint();
        return;
    }
    // S-TAB's arrival as a shifted Tab chord is terminal-dependent (see
    // KeyTranslation.cpp's special-key shift handling) -- M-p is the
    // always-available fallback, M-n its forward twin above.
    if ((plainTab && chord.Shift) || (chord.Meta && !chord.Control && chord.Codepoint == U'p')) {
        snippetSession_->PreviousField(*buffer);
        statusMessage_ = snippetSession_->StatusText();
        ClampPointToNarrowing();
        ScrollToShowPoint();
        return;
    }
    if (IsQuit(chord)) {
        // Done -- the expanded text stays exactly as it is.
        EndSnippetSession();
        statusMessage_.clear();
        return;
    }
    if (chord.Special == editor::SpecialKey::Backspace && !chord.Control && !chord.Meta &&
        snippetSession_->Pristine()) {
        // Backspace on a pristine placeholder deletes the whole placeholder
        // and is consumed -- one undo step including the mirror sync.
        buffer->BeginUndoGroup();
        snippetSession_->DeleteActiveFieldContent(*buffer);
        snippetSession_->ClearPristine();
        snippetSession_->SyncMirrors(*buffer);
        buffer->EndUndoGroup();
        ClampPointToNarrowing();
        ScrollToShowPoint();
        return;
    }
    if (snippetSession_->Pristine()) {
        if (IsPlainCharacter(chord)) {
            // First typed character replaces the placeholder: arm the
            // delete for RunCommandAndHandleOutcome's pre-dispatch hook
            // (inside the same undo group as the keystroke itself).
            snippetPendingPristineDelete_ = true;
        }
        else {
            snippetSession_->ClearPristine();
        }
    }
    DispatchChordNormally(chord);
}

bool BufferView::RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<bool()>& invoke,
                                            const editor::KeyChord* triggeringChord) {
    const std::size_t generationBefore    = activeBuffer_.Get().ContentGeneration();
    const std::size_t pointBefore         = activeBuffer_.Get().Point(); // documentHighlight follow-up: see MaybeScheduleDocumentHighlight's call site below
    const std::string statusMessageBefore = statusMessage_; // status-message-lifecycle: see the "clear if unchanged" check below
    // Diff gutter markers follow-up: read alongside generationBefore, for
    // the same "safe as long as this dispatch doesn't itself switch active
    // buffers" reasoning MaybeScheduleAutoCompletion's own generationBefore
    // comparison already relies on -- see this method's tail below.
    const bool wasModifiedBefore = activeBuffer_.Get().Modified();
    // snippet-expansion follow-up (pre-dispatch hook): while a session is
    // live, every buffer-modifying path funnels through here
    // (HandleSnippetKey re-dispatches everything it doesn't consume), so
    // this is the one choke point that can wrap the dispatched command and
    // its mirror sync (post-dispatch hook below) into one undo group --
    // kill-line/yank/C-u-repeats inside a field get identical treatment to
    // plain typing. Buffer re-resolved by name on each side (the command
    // may close it), never a stored pointer; an armed pristine-placeholder
    // delete applies here so it shares the keystroke's own undo step.
    const bool snippetHookArmed = inputMode_ == InputMode::Snippet && snippetSession_.has_value();
    if (snippetHookArmed) {
        if (text::Buffer* snippetBuffer = ResolveSnippetBuffer()) {
            snippetBuffer->BeginUndoGroup();
            if (snippetPendingPristineDelete_) {
                snippetSession_->DeleteActiveFieldContent(*snippetBuffer);
                snippetSession_->ClearPristine();
            }
        }
        snippetPendingPristineDelete_ = false;
    }
    bool ran = false;
    try {
        ran = invoke();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
        ran = true; // a command did run, it just threw -- still "something happened"
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

    // snippet-expansion follow-up (post-dispatch hook): sync mirrors and
    // close the pre-hook's undo group, then decide whether the session
    // survives this dispatch. Placed above the context.quit early return so
    // the group balances on every path, and above the interactiveRequest
    // block so a request that destroys this pane (delete-window) never
    // leaves orphaned ranges behind -- *this* is still alive here
    // regardless of what the command asked for (destruction only happens
    // inside StartInteractiveSession below).
    if (snippetHookArmed && snippetSession_) {
        text::Buffer* snippetBuffer = ResolveSnippetBuffer();
        if (snippetBuffer != nullptr) {
            snippetSession_->SyncMirrors(*snippetBuffer);
            snippetBuffer->EndUndoGroup();
        }
        if (snippetBuffer == nullptr                                             // buffer closed
            || !snippetSession_->RangesValid(*snippetBuffer)                     // undo/redo cleared the ranges
            || &activeBuffer_.Get() != snippetBuffer                             // command switched buffers
            || context.interactiveRequest != editor::InteractiveRequest::None) { // another session starting
            EndSnippetSession();
        }
        else if (statusMessage_.empty()) {
            statusMessage_ = snippetSession_->StatusText();
        }
    }

    if (context.quit) {
        // eventLoop_ is nullptr outside a real, running-editor SetEventLoop
        // call -- every unit test, and any other headless use of
        // BufferView. It is never null during real, running-editor usage
        // (main.cpp is what constructs the EventLoop and wires it in), but
        // the null check stays strict rather than being dropped as a
        // hypothetical risk -- skipping it crashed the whole process the
        // instant a test exercised `quit`, confirmed via a real SIGSEGV.
        // Shown by the final frame EventLoop::Run renders after Exit():
        // teardown (LSP child grace waits, session saves) runs after Run
        // returns but before the terminal is restored, and without this the
        // pause reads as a hang rather than deliberate cleanup.
        statusMessage_ = "Shutting down...";
        if (eventLoop_) {
            eventLoop_->Exit();
        }
        return ran;
    }

    // lsp-format-on-save follow-up: save-buffer/save-buffer-force set this
    // instead of running their own saveBufferBody synchronously when an LSP
    // round trip should format the buffer first -- the actual save hasn't
    // happened yet, so none of the normal post-command refresh below
    // applies until RequestLspFormatThenSaveBuffer's own callback runs it.
    if (context.deferSaveForLspFormat) {
        RequestLspFormatThenSaveBuffer();
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
        // Emacs-keymap-round-2 follow-up: zap-to-char's own invocation is
        // the only place with real access to context.lastCommand (see
        // CommandContext::zapToCharAppend's own doc comment) -- stash its
        // decision here, before the character keystroke that actually
        // performs the kill (which bypasses Dispatcher::Feed, and so never
        // sees a meaningful lastCommand of its own) needs it.
        if (context.interactiveRequest == editor::InteractiveRequest::ZapToChar) {
            pendingZapToCharAppend_ = context.zapToCharAppend;
        }
        // snippet-expansion follow-up: same stash shape as zapToCharAppend
        // just above -- the requesting command's context is a caller local,
        // so what to expand has to ride a member into
        // StartInteractiveSession's own SnippetExpand case.
        if (context.interactiveRequest == editor::InteractiveRequest::SnippetExpand) {
            pendingSnippetExpansion_ = context.snippetExpansion;
        }
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
        MaybeScheduleSignatureHelp(*triggeringChord, generationBefore);
        MaybeScheduleOnTypeFormatting(*triggeringChord, generationBefore);
    }

    // Diff gutter markers follow-up: unlike MaybeScheduleAutoCompletion
    // above, not gated on triggeringChord/plain-self-insert -- deletions,
    // undo, paste, and format-on-save should all refresh the gutter too,
    // not just organic typing. A save (Modified() transitioning true ->
    // false) bypasses the debounce entirely and refreshes right away, the
    // same "this is a natural point to want it fresh" reasoning a real
    // save deserves; anything else just re-arms the debounce timer.
    if (wasModifiedBefore && !activeBuffer_.Get().Modified()) {
        RequestDiffForCurrentBuffer();
    }
    else if (activeBuffer_.Get().ContentGeneration() != generationBefore) {
        ScheduleDiffRefresh();
    }

    // Editable-multibuffer follow-up: an edit inside the *diagnostics*
    // multibuffer leaves its Diagnostic entries pointing at stale composite
    // bytes -- Diagnostic is deliberately not relocated (a real LSP server
    // re-publishes its full set after every change; a synthetic multibuffer
    // has no server to do that for it). Clearing loses the severity
    // coloring after the first edit, but never shows a diagnostic
    // underline pointing at the wrong bytes. Gated on ExcerptRanges() being
    // non-empty, so this is a no-op for every ordinary buffer -- and it's
    // deliberately not gated on any "this is specifically the diagnostics
    // buffer" marker: any multibuffer with both excerpt ranges and
    // diagnostics set is, today, only ever the one RequestDiagnosticsBuffer
    // builds.
    if (activeBuffer_.Get().ContentGeneration() != generationBefore && !activeBuffer_.Get().ExcerptRanges().empty() &&
        !activeBuffer_.Get().Diagnostics().empty()) {
        activeBuffer_.Get().SetDiagnostics({});
    }

    // documentHighlight follow-up: not gated on triggeringChord/plain-self-
    // insert -- arrow motion, search, and any other point-moving command
    // must refresh the highlighted-occurrences set too, not just organic
    // typing (the completion-popup precedent this otherwise mirrors).
    MaybeScheduleDocumentHighlight(pointBefore, generationBefore);

    ClampPointToNarrowing();
    // multi-cursor-round-2 follow-up: a command that just added a secondary
    // cursor (add-cursor-above/-below, select-next-occurrence) reports its
    // new offset here instead of moving point itself -- scroll to show that
    // cursor rather than the ordinary (unmoved) primary point.
    if (context.newlyAddedCursorPoint) {
        ScrollToShowOffset(*context.newlyAddedCursorPoint);
    }
    else {
        ScrollToShowPoint();
    }
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
    // to usefully guard here. Must also cancel any deadline already armed
    // before this session started (a Normal-mode message that was showing
    // when the session opened): statusMessageTimer_'s fire callback reads
    // statusMessage_/statusMessageSnapshot_ live rather than a value
    // captured at arm time, and the snapshot sync just below keeps them
    // equal throughout the session -- so an uncancelled old timer would
    // fire mid-session and find them "unchanged," wiping the session's own
    // live text (e.g. the isearch query) right out from under the user.
    if (inputMode_ != InputMode::Normal) {
        statusMessageSnapshot_ = statusMessage_;
        statusMessageChangedAt_.reset();
        statusMessageTimer_.Cancel(); // drop any deadline armed before this session started -- see the comment above
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

    // DeadlineTimer::Arm starts a real background thread that wakes exactly
    // once, kStatusMessageTimeout from now, and Posts the clear back onto
    // the loop thread -- no polling needed. Guarded by statusMessageSnapshot_
    // still matching statusMessage_ at fire time: if something else wrote a
    // new message before this deadline elapsed, this fire must not clear
    // text it didn't set (a fresh deadline for that newer text was armed
    // by this same method's own next call, which re-armed
    // statusMessageTimer_; Arm() cancels any not-yet-fired previous callback,
    // so only the latest deadline ever actually fires).
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

void BufferView::ReportError(std::string message, editor::LogCategory category) {
    editor::LogMessage(category, editor::LogSeverity::Error, message);
    statusMessage_ = std::move(message);
}

void BufferView::RequestCompletionAtPoint() {
    // Completion is a Normal-mode-only construct (see OnKeyEvent's
    // activeCompletion_ block): a debounce timer armed by Normal-mode typing
    // can fire after an interactive session has since started -- found live
    // with the snippet session, where typing a trigger word armed the timer
    // and TAB's expansion won the race, leaving a completion popped over the
    // active field that the session's own TAB could then never accept.
    // MaybeScheduleAutoCompletion's scheduling-side gate can't cover an
    // already-armed timer, so the fire path bails here too.
    if (inputMode_ != InputMode::Normal) {
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    // embedded-language-documents follow-up: an empty serverKey means point
    // isn't inside an embedded region -- use the host language's own key,
    // same as before this feature existed. A non-empty one routes to that
    // language's own server (e.g. "javascript" inside an HTML <script>
    // block) for both the status check below and the request itself.
    const std::string serverKey   = ResolvedLspServerKey(point);
    const std::string languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;

    // dabbrev-fallback follow-up: StatusForLanguage never spawns a client,
    // so this is a pure "is one currently usable" check -- NotConfigured/
    // SpawnFailed/Disconnected all fall back to scanning the buffer itself
    // rather than asking a server that isn't there.
    const bool hasRunningLsp =
        lspManager_ && lspManager_->StatusForLanguage(languageKey) == editor::lsp::LspManager::LspStatus::Running;
    if (!hasRunningLsp) {
        // Self-hosting-completion follow-up: tried ahead of plain
        // dabbrev-expand for a Janet-mode buffer, falling through to it when
        // there's no janetEnv_ wired or nothing fuzzy-matches (e.g. a local
        // variable name rather than a "ned/*" binding).
        if (languageKey == "janet" && ApplyJanetBindingCompletion(buffer, point)) {
            return;
        }
        ApplyDabbrevCompletion(buffer, point);
        return;
    }

    text::Buffer* const bufferPtr   = &buffer;
    const std::size_t   generation  = ++completionRequestGeneration_;
    const std::size_t   prefixStart = WordPrefixStart(buffer.Content(), point);

    lspManager_->RequestCompletion(
        buffer, point,
        [this, bufferPtr, point, prefixStart, generation](std::vector<editor::lsp::CompletionItem> items) {
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
                activeCompletion_.reset();
                NotifyCompletionChanged();
                return;
            }
            activeCompletion_ = ActiveCompletion{
                .requestPoint = point, .items = std::move(items), .selectedIndex = 0, .prefixStart = prefixStart};
            NotifyCompletionChanged();
        },
        serverKey);
}

void BufferView::ApplyDabbrevCompletion(text::Buffer& buffer, std::size_t point) {
    const text::ITextStorage& content     = buffer.Content();
    const std::size_t prefixStart = WordPrefixStart(content, point);
    const std::string prefix      = content.Substring(prefixStart, point - prefixStart);

    std::vector<std::string> words = editor::CollectDabbrevCandidates(buffer.Text(), point, prefix);
    if (words.empty()) {
        activeCompletion_.reset();
        NotifyCompletionChanged();
        return;
    }
    std::vector<editor::lsp::CompletionItem> items;
    items.reserve(words.size());
    for (std::string& word : words) {
        // completion-popup follow-up: kind 1 == LSP CompletionItemKind::Text
        // -- a buffer-scanned word has no real semantic category, but a
        // fixed, sensible glyph beats the popup's "unrecognized kind"
        // fallback for every dabbrev row.
        items.push_back(editor::lsp::CompletionItem{.label = word, .insertText = word, .kind = 1});
    }
    activeCompletion_ =
        ActiveCompletion{.requestPoint = point, .items = std::move(items), .selectedIndex = 0, .prefixStart = prefixStart};
    NotifyCompletionChanged();
}

bool BufferView::ApplyJanetBindingCompletion(text::Buffer& buffer, std::size_t point) {
    if (!janetEnv_) {
        return false;
    }

    const std::string text        = buffer.Text();
    const std::size_t prefixStart = editor::JanetSymbolPrefixStart(text, point);
    const std::string prefix      = text.substr(prefixStart, point - prefixStart);
    if (prefix.empty()) {
        return false;
    }

    const std::vector<std::string> names  = janetEnv_->BindingNamesWithPrefix("ned/");
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(names, prefix);

    std::vector<editor::lsp::CompletionItem> items;
    items.reserve(ranked.size());
    for (const std::string& name : ranked) {
        // A subsequence match can never be shorter than the query it matched
        // against, so name.size() == prefix.size() here only when name IS
        // prefix verbatim -- point already sits right after a complete
        // binding name, nothing left to suggest (DabbrevComplete.h's own
        // "exact-length matches excluded" rule, applied here for the same
        // reason: CompletionInsertSuffix's insertText-doesn't-share-prefix
        // fallback would otherwise show the whole name again as a bogus
        // duplicate suffix).
        if (name.size() == prefix.size()) {
            continue;
        }
        // completion-popup follow-up: kind 3 == LSP CompletionItemKind::
        // Function -- every "ned/*" binding is, semantically, a callable.
        items.push_back(editor::lsp::CompletionItem{.label = name, .insertText = name, .kind = 3});
    }
    if (items.empty()) {
        return false;
    }
    activeCompletion_ =
        ActiveCompletion{.requestPoint = point, .items = std::move(items), .selectedIndex = 0, .prefixStart = prefixStart};
    NotifyCompletionChanged();
    return true;
}

bool BufferView::ShouldSuppressAutoCompletion() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    const std::size_t   point  = buffer.Point();
    if (point == 0) {
        return false;
    }
    const text::ITextStorage& content = buffer.Content();

    if (highlightCacheBuffer_ == &buffer) {
        const std::size_t                        line        = content.ByteOffsetToLine(point);
        const std::size_t                        lineStart   = content.LineToByteOffset(line);
        const std::size_t                        lineEnd     = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::vector<editor::HighlightSpan> lineSpans   = SpansForLine(highlightCacheSpans_, lineStart, lineEnd);
        const std::size_t                        priorOffset = content.PreviousCodepointBoundary(point);
        switch (SpanAtOffset(lineSpans, priorOffset).syntaxClass) {
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
    activeCompletion_.reset(); // typing invalidates any currently-shown suggestion
    NotifyCompletionChanged();
    // snippet-expansion follow-up: completion is a Normal-mode-only
    // construct (see OnKeyEvent's activeCompletion_ block), but typing
    // inside a snippet field reaches here through HandleSnippetKey's
    // re-dispatch -- without this gate the debounce timer could pop a
    // suggestion mid-session that TAB (consumed by the session) could then
    // never accept.
    if (inputMode_ == InputMode::Snippet) {
        return;
    }
    // dabbrev-fallback follow-up: no longer gated on lspManager_ being set at
    // all -- RequestCompletionAtPoint (fired once this debounce elapses)
    // itself decides between an LSP request and the buffer-word fallback.
    // The auto-popup toggle still governs both sources uniformly.
    if (!editor::lsp::LspAutoCompleteEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta || chord.Special != editor::SpecialKey::None) {
        return; // only plain self-insert keystrokes schedule automatic completion
    }
    // completion-auto-trigger-gate follow-up: only a word-continuation
    // keystroke (an identifier the user is actively typing) or a small,
    // hardcoded set of real completion trigger characters (member access
    // ".", C++ scope resolution "::"/arrow "->", both ending in one of
    // ":"/">" ) schedules a request -- everything else (";", ")", "}", ",",
    // whitespace, ...) is a statement/expression boundary, not a place
    // completions are useful. Same "small fixed set, not real server-
    // declared triggerCharacters capability negotiation" precedent
    // MaybeScheduleSignatureHelp's own "(" / "," gate already establishes
    // just below -- this codebase has no completionProvider.triggerCharacters
    // plumbing to consult instead. Found live: typing ";" was popping the
    // completion popup because clangd (like most servers) happily answers
    // textDocument/completion with general in-scope symbols even for an
    // empty/non-identifier prefix -- nothing here previously looked at
    // *which* character was typed, only the syntax class already at point.
    if (!IsWordCodepoint(chord.Codepoint) && chord.Codepoint != U'.' && chord.Codepoint != U':' && chord.Codepoint != U'>') {
        return;
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    if (ShouldSuppressAutoCompletion()) {
        return;
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    completionDebounceDeadline_ = std::chrono::steady_clock::now() + delay;
    // DeadlineTimer::Arm fires exactly once, delay from now -- re-typing
    // before it fires re-arms it via this same call site on the very next
    // qualifying keystroke, cancelling the stale one outright, which is what
    // makes this a debounce rather than a fixed-interval repeat.
    if (eventLoop_) {
        completionDebounceTimer_.Arm(*eventLoop_, delay, [this] {
            completionDebounceDeadline_.reset();
            RequestCompletionAtPoint();
        });
    }
}

void BufferView::MaybeScheduleDocumentHighlight(std::size_t pointBefore, std::size_t generationBefore) {
    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.Point() == pointBefore && buffer.ContentGeneration() == generationBefore) {
        return; // nothing moved -- no reason to re-request
    }
    if (documentHighlight_ && (documentHighlight_->buffer != &buffer || documentHighlight_->contentGeneration != buffer.ContentGeneration())) {
        documentHighlight_.reset(); // stale: buffer switched under us, or content changed since the last response
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    if (eventLoop_) {
        documentHighlightDebounceTimer_.Arm(*eventLoop_, delay, [this] { RequestDocumentHighlightAtPoint(); });
    }
}

void BufferView::RequestDocumentHighlightAtPoint() {
    if (inputMode_ != InputMode::Normal) {
        documentHighlight_.reset();
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    const std::string serverKey   = ResolvedLspServerKey(point);
    const std::string languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const bool        hasRunningLsp =
        lspManager_ && lspManager_->StatusForLanguage(languageKey) == editor::lsp::LspManager::LspStatus::Running;
    if (!hasRunningLsp) {
        documentHighlight_.reset();
        return;
    }

    text::Buffer* const bufferPtr                = &buffer;
    const std::size_t   generation               = ++documentHighlightRequestGeneration_;
    const std::size_t   contentGenerationAtRequest = buffer.ContentGeneration();
    lspManager_->RequestDocumentHighlight(
        buffer, point,
        [this, bufferPtr, point, generation, contentGenerationAtRequest](std::vector<editor::lsp::DocumentHighlight> highlights) {
            if (generation != documentHighlightRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point ||
                activeBuffer_.Get().ContentGeneration() != contentGenerationAtRequest) {
                return; // buffer/point/content changed since the request was sent
            }
            if (highlights.empty()) {
                documentHighlight_.reset();
                return;
            }
            const text::ITextStorage&                                 content = bufferPtr->Content();
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            ranges.reserve(highlights.size());
            for (const editor::lsp::DocumentHighlight& highlight : highlights) {
                ranges.emplace_back(editor::lsp::LspPositionToByte(content, highlight.start),
                                    editor::lsp::LspPositionToByte(content, highlight.end));
            }
            documentHighlight_ = DocumentHighlightState{
                .buffer = bufferPtr, .contentGeneration = contentGenerationAtRequest, .requestPoint = point, .ranges = std::move(ranges)};
        },
        serverKey);
}

void BufferView::MaybeScheduleSignatureHelp(const editor::KeyChord& chord, std::size_t generationBefore) {
    // Deliberately not gated on InputMode::Snippet the way
    // MaybeScheduleAutoCompletion is -- typing "(" or "," while filling a
    // snippet tabstop argument is exactly when signature help is most
    // useful, and unlike the completion popup it never competes with TAB.
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    if (!editor::lsp::LspSignatureHelpAutoTriggerEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta || chord.Special != editor::SpecialKey::None) {
        return;
    }
    if (chord.Codepoint != U'(' && chord.Codepoint != U',') {
        return; // only these two trigger characters schedule automatic signature help
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    if (eventLoop_) {
        signatureHelpDebounceTimer_.Arm(*eventLoop_, delay, [this] { RequestSignatureHelpAtPoint(); });
    }
}

void BufferView::RequestSignatureHelpAtPoint() {
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    const std::string serverKey   = ResolvedLspServerKey(point);
    const std::string languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    if (!lspManager_ || lspManager_->StatusForLanguage(languageKey) != editor::lsp::LspManager::LspStatus::Running) {
        return;
    }

    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++signatureHelpRequestGeneration_;
    lspManager_->RequestSignatureHelp(
        buffer, point,
        [this, bufferPtr, point, generation](std::optional<std::string> text) {
            if (generation != signatureHelpRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (text) {
                statusMessage_ = *text; // EnsureStatusMessageFreshness() handles the auto-clear/timeout
            }
        },
        serverKey);
}

void BufferView::AcceptActiveCompletion() {
    if (!activeCompletion_) {
        return;
    }
    text::Buffer&                     buffer = activeBuffer_.Get();
    const editor::lsp::CompletionItem item   = activeCompletion_->items[activeCompletion_->selectedIndex];
    if (item.isSnippet) {
        // snippet-expansion follow-up: a snippet-format item's insertText
        // is TextMate syntax, never literal text -- replace the typed
        // prefix with the parsed expansion and start a tabstop session,
        // the exact InteractiveRequest::SnippetExpand path.
        const std::size_t prefixStart = activeCompletion_->prefixStart;
        activeCompletion_.reset();
        NotifyCompletionChanged();
        BeginSnippetExpansion(prefixStart, buffer.Point(), item.insertText);
        return;
    }
    const std::string suffix = CompletionInsertSuffix(item);
    activeCompletion_.reset();
    NotifyCompletionChanged();
    if (!suffix.empty()) {
        buffer.InsertAtPoint(suffix);
    }
}

void BufferView::AcceptActiveCompletionAt(std::size_t index) {
    if (!activeCompletion_ || index >= activeCompletion_->items.size()) {
        return; // stale click racing a just-cleared/just-replaced popup
    }
    activeCompletion_->selectedIndex = index;
    AcceptActiveCompletion();
}

void BufferView::TriggerSwitchProject() {
    StartInteractiveSession(editor::InteractiveRequest::SwitchProject);
}

void BufferView::CycleActiveCompletion(int direction) {
    if (!activeCompletion_ || activeCompletion_->items.empty()) {
        return;
    }
    const std::size_t count          = activeCompletion_->items.size();
    const std::size_t current        = activeCompletion_->selectedIndex;
    activeCompletion_->selectedIndex = (direction > 0) ? (current + 1) % count : (current + count - 1) % count;
    NotifyCompletionChanged();
}

std::string BufferView::CompletionInsertSuffix(const editor::lsp::CompletionItem& item) const {
    // activeCompletion_ is guaranteed set by its one call site
    // (AcceptActiveCompletion, for a non-snippet item) -- this method only
    // ever runs against one of its own items. Uses the prefixStart captured
    // when the suggestion was populated (see ActiveCompletion's own doc
    // comment) rather than recomputing it here:
    // WordPrefixStart's ASCII alnum/'_' rule is wrong for a "ned/*" binding
    // item, which was ranked against JanetSymbolPrefixStart's wider rule
    // instead.
    const text::Buffer&       buffer      = activeBuffer_.Get();
    const text::ITextStorage& content     = buffer.Content();
    const std::size_t         point       = buffer.Point();
    const std::size_t         prefixStart = activeCompletion_->prefixStart;
    const std::string         prefix      = content.Substring(prefixStart, point - prefixStart);

    // snippet-expansion follow-up: a snippet-format item's raw insertText
    // carries ${1:...} markers -- AcceptActiveCompletion never reaches this
    // method for a snippet item (it expands via BeginSnippetExpansion
    // instead), but item.isSnippet is still checked defensively here for
    // the same reason the original ghost-text path did.
    const std::string effectiveText =
        item.isSnippet ? editor::ParseSnippet(item.insertText).text : item.insertText;
    if (effectiveText.size() > prefix.size() && effectiveText.compare(0, prefix.size(), prefix) == 0) {
        return effectiveText.substr(prefix.size());
    }
    // The server's insertText doesn't share our naively-computed word
    // prefix (e.g. it used a textEdit range instead) -- shown in full
    // rather than guessed at; a documented v1 limitation, not a crash risk.
    return effectiveText;
}

std::optional<Point> BufferView::CompletionAnchorNow() const {
    const std::optional<Point> local = CursorPosition();
    if (!local) {
        return std::nullopt;
    }
    // Same local-to-absolute conversion main.cpp's own render() callback
    // uses to place the real terminal cursor -- one row below point, so the
    // popup opens under the text being typed rather than over it.
    const Box& box = Box_();
    return Point{.x = box.x_min + local->x, .y = box.y_min + local->y + 1};
}

void BufferView::NotifyCompletionChanged() {
    if (!onCompletionChanged_) {
        return;
    }
    if (!activeCompletion_) {
        lastNotifiedCompletionAnchor_.reset();
        onCompletionChanged_(std::nullopt);
        return;
    }

    const std::optional<Point> anchor = CompletionAnchorNow();
    lastNotifiedCompletionAnchor_     = anchor;
    if (!anchor) {
        onCompletionChanged_(std::nullopt); // point's row isn't on screen this frame
        return;
    }

    ListPopupModel model;
    model.selectedIndex = activeCompletion_->selectedIndex;
    model.anchor        = anchor;
    model.rows.reserve(activeCompletion_->items.size());
    for (const editor::lsp::CompletionItem& item : activeCompletion_->items) {
        ListPopupRow row;
        row.main  = item.label;
        row.right = item.detail;
        if (const std::optional<editor::SymbolKind> bucket = CompletionKindBucket(item.kind)) {
            row.left           = SymbolGlyphFor(*bucket);
            row.leftForeground = theme_.BrushFor(editor::SyntaxClassFor(*bucket)).foreground;
        }
        else {
            // Unrecognized/absent kind (a bare keyword, snippet, file path,
            // ... -- see CompletionKindBucket's own doc comment): a dim,
            // generic marker rather than no glyph at all, so every row
            // still has a visual anchor in this column.
            row.left           = "·"; // MIDDLE DOT
            row.leftForeground = theme_.ghostTextForeground;
        }
        model.rows.push_back(std::move(row));
    }
    // completion-popup-preview follow-up: the *selected* item's own
    // documentation, not every item's -- ListPopup renders it as a footer
    // below the row list, updated for free on every selection change since
    // this method already runs then (CycleActiveCompletion, a click, ...).
    if (const std::string& documentation = activeCompletion_->items[activeCompletion_->selectedIndex].documentation;
        !documentation.empty()) {
        model.previewText = documentation;
    }
    onCompletionChanged_(std::move(model));
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
    // already does), else a zero-length range at point. executeCommand/
    // prose-code-actions follow-up: a Prose-origin diagnostic at point
    // routes the whole request to the prose checker connection instead of
    // the primary language server -- that server has no notion of a
    // harper-ls-flagged word, only harper-ls's own connection does.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    // embedded-language-documents follow-up: defaults to point's own
    // embedded server (e.g. "javascript" inside an HTML <script> block), ""
    // meaning the host language -- RequestCodeActions's own default -- when
    // point isn't inside one. A Prose-origin diagnostic at point still wins
    // below, unchanged priority.
    std::string serverKey = ResolvedLspServerKey(point);
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const bool atPoint = (diagnostic.startByte == diagnostic.endByte) ? (point == diagnostic.startByte)
                                                                          : (diagnostic.startByte <= point && point < diagnostic.endByte);
        if (atPoint) {
            rangeStart = diagnostic.startByte;
            rangeEnd   = diagnostic.endByte;
            if (diagnostic.origin == text::Buffer::Diagnostic::Origin::Prose) {
                serverKey = editor::lsp::kProseLanguageKey;
            }
            break;
        }
    }

    statusMessage_ = "Requesting code actions...";
    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::CodeAction> actions) {
            if (generation != codeActionRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- see RequestCompletionAtPoint's own identical guard
            }
            pendingCodeActions_  = std::move(actions);
            codeActionServerKey_ = serverKey;
            if (pendingCodeActions_.empty()) {
                statusMessage_ = "No code actions available.";
                return;
            }
            codeActionSelection_ = 0;
            if (pendingCodeActions_.size() == 1) {
                ResolveAndApplyCodeAction(pendingCodeActions_[0]);
                return;
            }
            inputMode_ = InputMode::LspCodeActionSelect;
            RefreshCodeActionSelectStatus();
        },
        serverKey);
}

void BufferView::RequestQuickFixAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++codeActionRequestGeneration_;

    // Same diagnostic-at-point range/server-routing preference as
    // RequestCodeActionsAtPoint -- see that method's own doc comment.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    // embedded-language-documents follow-up: see RequestCodeActionsAtPoint's
    // identical comment above.
    std::string serverKey = ResolvedLspServerKey(point);
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const bool atPoint = (diagnostic.startByte == diagnostic.endByte) ? (point == diagnostic.startByte)
                                                                          : (diagnostic.startByte <= point && point < diagnostic.endByte);
        if (atPoint) {
            rangeStart = diagnostic.startByte;
            rangeEnd   = diagnostic.endByte;
            if (diagnostic.origin == text::Buffer::Diagnostic::Origin::Prose) {
                serverKey = editor::lsp::kProseLanguageKey;
            }
            break;
        }
    }

    statusMessage_ = "Requesting quick fix...";
    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::CodeAction> actions) {
            if (generation != codeActionRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            codeActionServerKey_ = serverKey;
            if (actions.empty()) {
                statusMessage_ = "No quick fix available.";
                return;
            }
            // Pick without asking only when the choice is unambiguous: a
            // lone action, else a lone isPreferred one (the server's own
            // "this is the auto-fix" marker), else a lone quickfix-kind one.
            // Anything murkier falls back to the ordinary selection list --
            // silently applying one of several plausible fixes would be
            // worse than one extra keystroke.
            const editor::lsp::CodeAction* pick = nullptr;
            if (actions.size() == 1) {
                pick = &actions[0];
            }
            for (const auto selector : {+[](const editor::lsp::CodeAction& a) { return a.isPreferred; },
                                        +[](const editor::lsp::CodeAction& a) { return a.kind.rfind("quickfix", 0) == 0; }}) {
                if (pick != nullptr) {
                    break;
                }
                const editor::lsp::CodeAction* sole = nullptr;
                for (const editor::lsp::CodeAction& action : actions) {
                    if (!selector(action)) {
                        continue;
                    }
                    if (sole != nullptr) {
                        sole = nullptr; // more than one match -- ambiguous, try the next selector
                        break;
                    }
                    sole = &action;
                }
                pick = sole;
            }
            if (pick != nullptr) {
                ResolveAndApplyCodeAction(*pick);
                return;
            }
            pendingCodeActions_  = std::move(actions);
            codeActionSelection_ = 0;
            inputMode_           = InputMode::LspCodeActionSelect;
            RefreshCodeActionSelectStatus();
        },
        serverKey);
}

void BufferView::RequestCodeLensAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         point     = buffer.Point();
    const std::size_t         line      = content.ByteOffsetToLine(point);
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    const editor::lsp::LspManager::ResolvedCodeLens* found = nullptr;
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        const bool onThisLine =
            (lens.startByte >= lineStart && lens.startByte < lineEnd) || (lineStart == lineEnd && lens.startByte == lineStart);
        if (onThisLine) {
            found = &lens; // v1: only the first lens on the line -- see this method's own doc comment in BufferView.h
            break;
        }
    }
    if (!found) {
        statusMessage_ = "No code lens at point.";
        return;
    }

    const std::string   serverKey = editor::LanguageKeyForMode(mode_);
    text::Buffer* const bufferPtr = &buffer;

    if (found->hasCommand) {
        statusMessage_ = "Running " + (found->title.empty() ? found->commandName : found->title) + "...";
        lspManager_->ExecuteCommand(buffer, serverKey, found->commandName, found->commandArguments,
                                    [this](bool ok) { statusMessage_ = ok ? "Code lens command executed." : "Code lens command failed."; });
        return;
    }

    // Copied, not a held pointer -- ResolveCodeLens's own callback runs
    // later, by which time a fresh RequestCodeLenses response (this
    // buffer's own per-Paint() background sync) could have replaced
    // CodeLensSpans' underlying vector out from under a raw pointer into it.
    const editor::lsp::LspManager::ResolvedCodeLens lensCopy = *found;
    statusMessage_                                           = "Resolving code lens...";
    lspManager_->ResolveCodeLens(
        buffer, lensCopy,
        [this, bufferPtr, serverKey](std::optional<editor::lsp::LspManager::ResolvedCodeLens> resolved) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // buffer changed under us
            }
            if (!resolved || !resolved->hasCommand) {
                statusMessage_ = "Code lens has no runnable command.";
                return;
            }
            statusMessage_ = "Running " + (resolved->title.empty() ? resolved->commandName : resolved->title) + "...";
            lspManager_->ExecuteCommand(
                *bufferPtr, serverKey, resolved->commandName, resolved->commandArguments,
                [this](bool ok) { statusMessage_ = ok ? "Code lens command executed." : "Code lens command failed."; });
        });
}

void BufferView::RefreshCodeActionSelectStatus() {
    statusMessage_ = "Code action: ";
    if (!onCandidatesChanged_) {
        return;
    }
    ListPopupModel model;
    model.title = "Code action";
    model.rows.reserve(pendingCodeActions_.size());
    for (std::size_t i = 0; i < pendingCodeActions_.size(); ++i) {
        model.rows.push_back({.left = std::to_string(i + 1) + ")", .main = pendingCodeActions_[i].title});
    }
    if (!pendingCodeActions_.empty()) {
        model.selectedIndex = codeActionSelection_;
    }
    onCandidatesChanged_(std::move(model));
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

    const editor::lsp::CodeAction action = pendingCodeActions_[codeActionSelection_];
    EndInteractiveSession();
    ResolveAndApplyCodeAction(action);
}

void BufferView::ResolveAndApplyCodeAction(const editor::lsp::CodeAction& action) {
    // code-actions-resolve follow-up: a server (clangd included)
    // advertising resolveProvider deliberately sends this action back
    // without an edit yet -- codeAction/resolve fills it in, only now
    // that the user has actually chosen to apply it (see CodeAction::
    // resolvable's own doc comment in LspContent.h for why this isn't
    // done eagerly for every listed action). Fire-and-forget, same
    // async shape as every other LSP request here: the caller continues
    // immediately, ApplyCodeAction runs later from inside the callback
    // once the resolved edit actually arrives.
    if (action.resolvable && lspManager_) {
        text::Buffer* const bufferPtr = &activeBuffer_.Get();
        statusMessage_                = "Resolving \"" + action.title + "\"...";
        lspManager_->ResolveCodeAction(
            activeBuffer_.Get(), action,
            [this, bufferPtr, action](std::optional<editor::lsp::CodeAction> resolved) {
                if (bufferPtr != &activeBuffer_.Get()) {
                    return; // active buffer changed since the resolve request was sent
                }
                if (!resolved || (!resolved->hasEdit && !resolved->command)) {
                    statusMessage_ = "\"" + action.title + "\" could not be resolved.";
                    return;
                }
                // executeCommand follow-up: the spec only guarantees resolve
                // fills in "edit" -- a compliant server that doesn't echo
                // back a "command" the original action already carried must
                // not silently drop it here.
                if (!resolved->command && action.command) {
                    resolved->command = action.command;
                }
                ApplyCodeAction(*resolved);
            },
            codeActionServerKey_);
        return;
    }
    ApplyCodeAction(action);
}

namespace {

    // Shared by ApplyCodeAction, ApplyRename, and LSP-formatting: resolves
    // each edit's LspPositions to byte offsets against buffer's CURRENT
    // content, sorts descending by start byte (keeps an edit not yet applied
    // valid as an earlier-in-the-buffer one shifts positions -- LSP
    // guarantees edits within one WorkspaceEdit/formatting response don't
    // overlap, so a plain sort suffices), and applies each via
    // Buffer::DeleteRange + Buffer::InsertAt as one undo group -- a
    // formatting response can carry dozens/hundreds of edits, and without
    // grouping each pair records its own undo step (undoing would take one
    // press per edit instead of one for the whole operation).
    void ApplyWorkspaceTextEdits(text::Buffer& buffer, const std::vector<editor::lsp::WorkspaceTextEdit>& edits) {
        const text::ITextStorage& content = buffer.Content();

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

        buffer.BeginUndoGroup();
        for (const ResolvedEdit& edit : resolved) {
            buffer.DeleteRange(edit.startByte, edit.endByte - edit.startByte);
            buffer.InsertAt(edit.startByte, edit.newText);
        }
        buffer.EndUndoGroup();
    }

} // namespace

void BufferView::ApplyProjectEdit(const std::vector<std::pair<text::Buffer*, std::vector<editor::lsp::WorkspaceTextEdit>>>& perBufferEdits,
                                  std::string description) {
    editor::ProjectEditTransaction transaction;
    transaction.description = description;
    transaction.records.reserve(perBufferEdits.size());
    for (const auto& [buffer, edits] : perBufferEdits) {
        const std::size_t beforeSequence = buffer->CurrentUndoSequence();
        ApplyWorkspaceTextEdits(*buffer, edits);
        if (buffer->Path()) {
            transaction.records.push_back(editor::ProjectUndoRecord{
                .path           = *buffer->Path(),
                .beforeSequence = beforeSequence,
                .afterSequence  = buffer->CurrentUndoSequence(),
            });
        }
        // A path-less (unsaved, never-saved-to-disk) buffer can't be
        // re-resolved by ProjectUndoManager::Undo/Redo later (they key on
        // BufferList::FindByPath) -- left out of the transaction, so its
        // own edit still undoes fine via plain per-buffer undo, it's just
        // not folded into the group.
    }
    if (projectUndo_) {
        projectUndo_->RecordTransaction(std::move(transaction));
    }
    statusMessage_ = description;
}

void BufferView::MaybeScheduleOnTypeFormatting(const editor::KeyChord& chord, std::size_t generationBefore) {
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    if (!editor::lsp::LspOnTypeFormattingEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta) {
        return;
    }
    std::string ch;
    if (chord.Special == editor::SpecialKey::Enter) {
        // "newline"/"open-line" insert a real '\n' -- Enter never carries a
        // literal codepoint the way an ordinary self-insert keystroke does,
        // but it's exactly clangd's own firstTriggerCharacter (reindent
        // after a newline), so it needs its own mapping here rather than
        // being excluded the way MaybeScheduleSignatureHelp excludes every
        // SpecialKey.
        ch = "\n";
    }
    else if (chord.Special == editor::SpecialKey::None) {
        ch = text::EncodeCodepointUtf8(chord.Codepoint);
    }
    else {
        return; // no other special key inserts trigger-shaped text
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    if (!lspManager_) {
        return;
    }

    text::Buffer&      buffer      = activeBuffer_.Get();
    const std::size_t  point       = buffer.Point();
    const std::string  serverKey   = ResolvedLspServerKey(point);
    const std::string  languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const std::optional<editor::lsp::OnTypeFormattingTriggers> triggers = lspManager_->OnTypeFormattingTriggersFor(languageKey);
    if (!triggers) {
        return; // this server never advertised documentOnTypeFormattingProvider at all
    }
    if (ch != triggers->first && std::find(triggers->more.begin(), triggers->more.end(), ch) == triggers->more.end()) {
        return; // not one of this server's own declared trigger characters
    }

    // Deliberately no debounce timer (unlike MaybeScheduleAutoCompletion/
    // MaybeScheduleSignatureHelp above): this only fires once per matching
    // trigger keystroke, not repeatedly while typing, so the request goes
    // out right away.
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++onTypeFormattingRequestGeneration_;
    lspManager_->RequestOnTypeFormatting(
        buffer, point, ch,
        [this, bufferPtr, generation](std::optional<std::vector<editor::lsp::WorkspaceTextEdit>> edits) {
            if (generation != onTypeFormattingRequestGeneration_ || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded, or the active buffer changed under us
            }
            if (edits && !edits->empty()) {
                // A second, separate undo step from the keystroke that
                // triggered this -- the LSP round trip can't complete
                // synchronously within that keystroke's own dispatch, so
                // there's no existing mechanism to join the two (see this
                // method's own doc comment in BufferView.h).
                ApplyWorkspaceTextEdits(*bufferPtr, *edits);
            }
        },
        serverKey);
}

void BufferView::RequestLspFormatThenSaveBuffer() {
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++lspFormatOnSaveRequestGeneration_;
    statusMessage_                 = "Formatting...";
    lspManager_->RequestFormatting(
        buffer,
        [this, bufferPtr, generation](std::optional<std::vector<editor::lsp::WorkspaceTextEdit>> edits) {
            if (generation != lspFormatOnSaveRequestGeneration_ || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded, or the active buffer changed under us
            }
            text::Buffer& buffer       = *bufferPtr;
            const bool    formatFailed = !edits.has_value();
            if (edits && !edits->empty()) {
                ApplyWorkspaceTextEdits(buffer, *edits); // one undo group -- see Step 0's fix
            }
            try {
                editor::WriteBufferToDisk(buffer);
                statusMessage_ = "Wrote " + buffer.Name() + (formatFailed ? " (LSP format failed)" : "");
            }
            catch (const std::exception& e) {
                statusMessage_ = e.what();
            }
            // Mirrors RunCommandAndHandleOutcome's own post-command refresh,
            // which the synchronous saveBufferBody path gets for free but
            // this async continuation must do itself, since it returns to
            // that method well before the save actually happens.
            RequestDiffForCurrentBuffer();
            ScrollToShowPoint();
        },
        std::string{});
}

void BufferView::ApplyCodeAction(const editor::lsp::CodeAction& action) {
    if (action.touchesUnsupportedForm) {
        statusMessage_ = "\"" + action.title + "\" uses an unsupported edit form -- not applied.";
        return;
    }
    if (!action.hasEdit && !action.command) {
        statusMessage_ = "\"" + action.title + "\" has no edit or command to apply.";
        return;
    }

    // executeCommand follow-up: apply the edit first, then execute the
    // command -- spec order (some of harper-ls's own quickfixes carry
    // both). Neither step depends on the other's outcome; the edit is
    // synchronous, the command async, so the two failure paths report
    // independently rather than one gating the other.
    //
    // project-undo follow-up: a code action's edit can now touch more than
    // one file (previously refused via touchesOtherFiles above) -- resolved
    // and applied the same all-or-nothing, one-project-transaction way
    // ApplyRename already does. edit-application-gaps follow-up: a
    // "documentChanges" edit (file create/rename/delete alongside a symbol's
    // own edits -- a real refactor.extract/rewrite shape, not just a rename)
    // resolves and applies through the same ApplyResolvedWorkspaceEdit path.
    if (action.hasEdit && (!action.edits.empty() || !action.documentChangeOps.empty())) {
        editor::lsp::LspManager::ResolvedRename resolved;
        if (!action.edits.empty()) {
            const std::optional<std::vector<editor::lsp::LspManager::ResolvedRenameEdit>> resolvedEdits =
                editor::lsp::LspManager::ResolveCodeActionEdits(action);
            if (!resolvedEdits) {
                statusMessage_ = "\"" + action.title + "\" names a file this editor can't resolve -- not applied.";
                return;
            }
            resolved.edits = std::move(*resolvedEdits);
        }
        if (!action.documentChangeOps.empty()) {
            const std::optional<std::vector<editor::lsp::LspManager::ResolvedDocumentChangeOp>> resolvedOps =
                editor::lsp::LspManager::ResolveDocumentChangeOps(action.documentChangeOps);
            if (!resolvedOps) {
                statusMessage_ = "\"" + action.title + "\" names a file this editor can't resolve -- not applied.";
                return;
            }
            resolved.documentChangeOps = std::move(*resolvedOps);
        }
        resolved.hasEdit = !resolved.edits.empty() || !resolved.documentChangeOps.empty();
        if (!ApplyResolvedWorkspaceEdit(resolved, "Applied \"" + action.title + "\".")) {
            return; // ReportError/statusMessage_ already surfaced the failure
        }
    }
    if (!action.command) {
        statusMessage_ = "Applied \"" + action.title + "\".";
        return;
    }

    text::Buffer* const bufferPtr = &activeBuffer_.Get();
    const std::string   title     = action.title;
    statusMessage_                = "Applying \"" + title + "\"...";
    lspManager_->ExecuteCommand(activeBuffer_.Get(), codeActionServerKey_, action.command->name, action.command->arguments,
                                [this, bufferPtr, title](bool ok) {
                                    if (bufferPtr != &activeBuffer_.Get()) {
                                        return; // active buffer changed since the request was sent
                                    }
                                    statusMessage_ = ok ? "Applied \"" + title + "\"." : "\"" + title + "\" command failed.";
                                });
}

void BufferView::RequestDefinitionAtPoint(LspLocationKind kind) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++definitionRequestGeneration_;
    // embedded-language-documents follow-up: routes to point's own embedded
    // server when it's inside one (e.g. an HTML <script> block), else "" ->
    // the host language, unchanged from before this feature existed.
    const std::string serverKey = ResolvedLspServerKey(point);
    // declaration/typeDefinition/implementation follow-up: the lowercase
    // word this request's status wording and pendingLocationLabel_ use.
    std::string label;
    switch (kind) {
        case LspLocationKind::Definition:
            label = "definition";
            break;
        case LspLocationKind::Declaration:
            label = "declaration";
            break;
        case LspLocationKind::TypeDefinition:
            label = "type definition";
            break;
        case LspLocationKind::Implementation:
            label = "implementation";
            break;
    }

    statusMessage_ = "Requesting " + label + "...";
    auto callback  = [this, bufferPtr, point, generation, label](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
        if (generation != definitionRequestGeneration_) {
            return; // superseded by a newer request
        }
        if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
            return; // buffer/point changed since the request was sent -- see RequestCodeActionsAtPoint's own identical guard
        }
        pendingDefinitions_   = std::move(locations);
        pendingLocationLabel_ = label;
        if (pendingDefinitions_.empty()) {
            statusMessage_ = "No " + label + " found.";
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
    };

    switch (kind) {
        case LspLocationKind::Definition:
            lspManager_->RequestDefinition(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::Declaration:
            lspManager_->RequestDeclaration(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::TypeDefinition:
            lspManager_->RequestTypeDefinition(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::Implementation:
            lspManager_->RequestImplementation(buffer, point, std::move(callback), serverKey);
            break;
    }
}

void BufferView::RefreshDefinitionSelectStatus() {
    std::string label = pendingLocationLabel_;
    if (!label.empty()) {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    }
    std::string status = label + ": ";
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
        PushJumpMark(); // before mutating point/activeBuffer_ -- see JumpMark's own doc comment
        activeBuffer_.Set(opened);
        opened.SetPoint(editor::lsp::LspPositionToByte(opened.Content(), location.position));
        statusMessage_.clear();
        ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

// call/type-hierarchy follow-up. See HierarchyDirection/HierarchySession's
// own doc comments in BufferView.h for the overall session shape.
void BufferView::RequestHierarchyAtPoint(HierarchyDirection direction) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = ++hierarchyRequestGeneration_;
    const std::string   serverKey  = ResolvedLspServerKey(point);

    std::string subjectLabel; // for "No <subjectLabel> at point." on an empty prepare result
    switch (direction) {
        case HierarchyDirection::IncomingCalls:
        case HierarchyDirection::OutgoingCalls:
            subjectLabel = "callable symbol";
            break;
        case HierarchyDirection::Supertypes:
        case HierarchyDirection::Subtypes:
            subjectLabel = "type";
            break;
    }

    statusMessage_ = "Requesting hierarchy...";
    auto onPrepared = [this, bufferPtr, point, generation, direction, serverKey,
                       subjectLabel](std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> items) {
        if (generation != hierarchyRequestGeneration_) {
            return; // superseded by a newer request
        }
        if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
            return; // buffer/point changed since the request was sent -- see RequestDefinitionAtPoint's own identical guard
        }
        if (items.empty()) {
            statusMessage_ = "No " + subjectLabel + " at point.";
            return;
        }
        // overload-set follow-up (see this method's own doc comment in
        // BufferView.h): more than one match is rare enough that taking
        // the first is an acceptable v1 cut.
        editor::lsp::LspManager::ResolvedHierarchyItem root = std::move(items.front());
        HierarchySession                               session{.direction = direction, .buffer = bufferPtr, .serverKey = serverKey, .rootName = root.item.name};
        session.tree.Reset({std::move(root)});
        hierarchySession_      = std::move(session);
        hierarchySelectedIndex_ = 0;
        ExpandHierarchyNode(0); // auto-expand the root -- see this method's own doc comment in BufferView.h
    };

    switch (direction) {
        case HierarchyDirection::IncomingCalls:
        case HierarchyDirection::OutgoingCalls:
            lspManager_->RequestPrepareCallHierarchy(buffer, point, std::move(onPrepared), serverKey);
            break;
        case HierarchyDirection::Supertypes:
        case HierarchyDirection::Subtypes:
            lspManager_->RequestPrepareTypeHierarchy(buffer, point, std::move(onPrepared), serverKey);
            break;
    }
}

void BufferView::ExpandHierarchyNode(std::size_t index) {
    if (!hierarchySession_ || !lspManager_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    HierarchySession& session = *hierarchySession_;
    if (session.tree.IsLoading(index)) {
        return;
    }
    if (session.tree.ChildrenFetched(index)) {
        // Already explored -- just reveal it again, no request needed (see
        // this method's own doc comment in BufferView.h).
        session.tree.SetExpanded(index, true);
        PushHierarchyModel();
        return;
    }

    session.tree.BeginLoading(index);
    PushHierarchyModel(); // shows the loading glyph immediately

    const editor::lsp::HierarchyItem item       = session.tree.At(index).data.item;
    text::Buffer&                    buffer     = *session.buffer;
    const std::string                serverKey  = session.serverKey;
    const HierarchyDirection         direction  = session.direction;
    const std::size_t                generation = ++hierarchyRequestGeneration_;

    // find-references follow-up's own reasoning applies here too: a
    // superseded/stale response is simply dropped, not applied to
    // whatever the tree has become by the time it arrives.
    auto onItems = [this, index, generation](std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> children) {
        if (!hierarchySession_ || generation != hierarchyRequestGeneration_) {
            return;
        }
        hierarchySession_->tree.Expand(index, std::move(children));
        PushHierarchyModel();
    };
    // callHierarchy/incomingCalls and .../outgoingCalls respond with the
    // extra fromRanges wrapper (LspManager::ResolvedHierarchyCall) --
    // call.callSites isn't surfaced in the tree yet (see LspManager.h's own
    // ResolvedHierarchyCall doc comment on that v1 cut), so this just
    // unwraps each entry's item and reuses onItems above.
    auto onCalls = [this, index, generation](std::vector<editor::lsp::LspManager::ResolvedHierarchyCall> calls) {
        if (!hierarchySession_ || generation != hierarchyRequestGeneration_) {
            return;
        }
        std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> children;
        children.reserve(calls.size());
        for (editor::lsp::LspManager::ResolvedHierarchyCall& call : calls) {
            children.push_back(std::move(call.item));
        }
        hierarchySession_->tree.Expand(index, std::move(children));
        PushHierarchyModel();
    };

    switch (direction) {
        case HierarchyDirection::IncomingCalls:
            lspManager_->RequestIncomingCalls(buffer, item, std::move(onCalls), serverKey);
            break;
        case HierarchyDirection::OutgoingCalls:
            lspManager_->RequestOutgoingCalls(buffer, item, std::move(onCalls), serverKey);
            break;
        case HierarchyDirection::Supertypes:
            lspManager_->RequestSupertypes(buffer, item, std::move(onItems), serverKey);
            break;
        case HierarchyDirection::Subtypes:
            lspManager_->RequestSubtypes(buffer, item, std::move(onItems), serverKey);
            break;
    }
}

void BufferView::PushHierarchyModel() {
    if (!hierarchySession_) {
        if (onHierarchyChanged_) {
            onHierarchyChanged_(std::nullopt);
        }
        return;
    }

    const HierarchySession& session = *hierarchySession_;
    std::string             verb;
    switch (session.direction) {
        case HierarchyDirection::IncomingCalls:
            verb = "Callers of ";
            break;
        case HierarchyDirection::OutgoingCalls:
            verb = "Calls from ";
            break;
        case HierarchyDirection::Supertypes:
            verb = "Supertypes of ";
            break;
        case HierarchyDirection::Subtypes:
            verb = "Subtypes of ";
            break;
    }

    ui::TreeViewModel model;
    model.title = verb + session.rootName;

    const std::vector<editor::ExpandableTree<editor::lsp::LspManager::ResolvedHierarchyItem>::VisibleRow> rows =
        session.tree.FlattenVisible();
    model.rows.reserve(rows.size());
    for (const auto& row : rows) {
        const auto& node = session.tree.At(row.index);
        model.rows.push_back(ui::TreeRow{
            .label       = BuildHierarchyRowLabel(node.data),
            .depth       = row.depth,
            .hasChildren = !node.childrenFetched || !node.children.empty(),
            .expanded    = node.expanded,
            .loading     = node.loading,
        });
    }
    if (!model.rows.empty()) {
        model.selectedIndex = std::min(hierarchySelectedIndex_, model.rows.size() - 1);
    }

    if (onHierarchyChanged_) {
        onHierarchyChanged_(std::move(model));
    }
}

void BufferView::EndHierarchySession() {
    hierarchySession_.reset();
    hierarchySelectedIndex_ = 0;
    if (onHierarchyChanged_) {
        onHierarchyChanged_(std::nullopt);
    }
    TakeFocus(); // reclaim keyboard focus from the TreeView overlay
}

void BufferView::SetOnHierarchyChanged(std::function<void(std::optional<ui::TreeViewModel>)> handler) {
    onHierarchyChanged_ = std::move(handler);
}

void BufferView::HierarchyActivate(std::size_t index) {
    if (!hierarchySession_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    const editor::lsp::LspManager::ResolvedHierarchyItem& resolved = hierarchySession_->tree.At(index).data;
    const editor::lsp::LspManager::ResolvedLocation       location{.path = resolved.path, .position = resolved.item.position};
    EndHierarchySession();
    JumpToDefinition(location);
}

void BufferView::HierarchyToggleExpand(std::size_t index) {
    hierarchySelectedIndex_ = index;
    ExpandHierarchyNode(index);
}

void BufferView::HierarchyCollapse(std::size_t index) {
    if (!hierarchySession_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    hierarchySelectedIndex_ = index;
    hierarchySession_->tree.SetExpanded(index, false);
    PushHierarchyModel();
}

void BufferView::HierarchyCancel() {
    EndHierarchySession();
}

void BufferView::HierarchySelectionChanged(std::size_t index) {
    hierarchySelectedIndex_ = index;
}

void BufferView::PushJumpMark() {
    jumpBackStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
    if (jumpBackStack_.size() > kMaxJumpBackStack) {
        jumpBackStack_.erase(jumpBackStack_.begin());
    }
    // A fresh jump branches off the navigation history -- the old forward
    // path is no longer reachable, same as a browser discarding forward
    // history on a new navigation.
    jumpForwardStack_.clear();
}

void BufferView::JumpBack() {
    while (!jumpBackStack_.empty()) {
        const JumpMark mark = jumpBackStack_.back();
        jumpBackStack_.pop_back();
        if (text::Buffer* target = bufferList_.Find(mark.bufferName)) {
            // Leave a trail for jump-forward to retrace -- pushed directly,
            // not via PushJumpMark, since that would clear the very stack
            // being populated here.
            jumpForwardStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
            if (jumpForwardStack_.size() > kMaxJumpBackStack) {
                jumpForwardStack_.erase(jumpForwardStack_.begin());
            }
            activeBuffer_.Set(*target);
            target->SetPoint(mark.byteOffset); // Buffer::SetPoint already clamps out-of-range offsets
            statusMessage_.clear();
            ScrollToShowPoint();
            return;
        }
        // buffer closed since the mark was pushed -- skip it, try the next one
    }
    statusMessage_ = "No more jump history.";
}

void BufferView::JumpForward() {
    while (!jumpForwardStack_.empty()) {
        const JumpMark mark = jumpForwardStack_.back();
        jumpForwardStack_.pop_back();
        if (text::Buffer* target = bufferList_.Find(mark.bufferName)) {
            jumpBackStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
            if (jumpBackStack_.size() > kMaxJumpBackStack) {
                jumpBackStack_.erase(jumpBackStack_.begin());
            }
            activeBuffer_.Set(*target);
            target->SetPoint(mark.byteOffset);
            statusMessage_.clear();
            ScrollToShowPoint();
            return;
        }
        // buffer closed since the mark was pushed -- skip it, try the next one
    }
    statusMessage_ = "No further jump history.";
}

void BufferView::RequestDocumentSymbolsAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++documentSymbolRequestGeneration_;
    const std::string   serverKey  = ResolvedLspServerKey(buffer.Point());

    statusMessage_ = "Requesting symbols...";
    lspManager_->RequestDocumentSymbols(
        buffer,
        [this, bufferPtr, generation](std::vector<editor::lsp::LspManager::SymbolResult> symbols) {
            if (generation != documentSymbolRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // buffer switched since the request was sent
            }
            if (symbols.empty()) {
                statusMessage_ = "No symbols found.";
                return;
            }
            documentSymbolCandidates_ = std::move(symbols);
            documentSymbolLabels_.clear();
            documentSymbolLabels_.reserve(documentSymbolCandidates_.size());
            for (const editor::lsp::LspManager::SymbolResult& symbol : documentSymbolCandidates_) {
                documentSymbolLabels_.push_back(BuildSymbolLabel(symbol, /*includePath=*/false));
            }
            documentSymbolSelection_ = 0;
            inputMode_               = InputMode::LspGotoSymbol;
            prompt_.emplace("Go to symbol (fuzzy): ");
            RefreshDocumentSymbolStatus();
        },
        serverKey);
}

void BufferView::RefreshDocumentSymbolStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
    documentSymbolSelection_              = ranked.empty() ? 0 : std::min(documentSymbolSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, documentSymbolSelection_)));
    }
}

void BufferView::HandleDocumentSymbolKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No symbol matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }
        const std::string& label = ranked[std::min(documentSymbolSelection_, ranked.size() - 1)];
        // Maps the ranked label back to its SymbolResult -- see
        // documentSymbolLabels_'s own doc comment for why exact-string
        // lookup is safe here.
        const auto it = std::find(documentSymbolLabels_.begin(), documentSymbolLabels_.end(), label);
        EndInteractiveSession();
        if (it == documentSymbolLabels_.end()) {
            return; // unreachable: every ranked label comes from documentSymbolLabels_ itself
        }
        const std::size_t                            index  = static_cast<std::size_t>(it - documentSymbolLabels_.begin());
        const editor::lsp::LspManager::SymbolResult& symbol = documentSymbolCandidates_[index];
        JumpToDefinition(editor::lsp::LspManager::ResolvedLocation{.path = symbol.path, .position = symbol.position});
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Go to symbol cancelled.";
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
        if (!ranked.empty()) {
            documentSymbolSelection_ = chord.Special == editor::SpecialKey::Down
                                           ? (documentSymbolSelection_ + 1) % ranked.size()
                                           : (documentSymbolSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshDocumentSymbolStatus();
        return;
    }
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        documentSymbolSelection_ = 0;
        RefreshDocumentSymbolStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::RequestWorkspaceSymbolsForCurrentQuery() {
    // MaybeScheduleAutoCompletion/RequestCompletionAtPoint's own guard
    // shape: the debounce timer that leads here doesn't get cancelled when
    // the session ends, so this re-checks the session is still live first.
    if (inputMode_ != InputMode::LspWorkspaceSymbol || !lspManager_) {
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++workspaceSymbolRequestGeneration_;
    const std::string   serverKey  = ResolvedLspServerKey(buffer.Point());
    const std::string   query      = prompt_->Text();

    lspManager_->RequestWorkspaceSymbols(
        buffer, query,
        [this, bufferPtr, generation](std::vector<editor::lsp::LspManager::SymbolResult> symbols) {
            if (generation != workspaceSymbolRequestGeneration_) {
                return; // superseded by a newer request
            }
            if (inputMode_ != InputMode::LspWorkspaceSymbol || bufferPtr != &activeBuffer_.Get()) {
                return; // session ended, or buffer switched, while this was in flight
            }
            pendingWorkspaceSymbols_ = std::move(symbols);
            workspaceSymbolLabels_.clear();
            workspaceSymbolLabels_.reserve(pendingWorkspaceSymbols_.size());
            for (const editor::lsp::LspManager::SymbolResult& symbol : pendingWorkspaceSymbols_) {
                workspaceSymbolLabels_.push_back(BuildSymbolLabel(symbol, /*includePath=*/true));
            }
            workspaceSymbolSelection_ =
                pendingWorkspaceSymbols_.empty() ? 0 : std::min(workspaceSymbolSelection_, pendingWorkspaceSymbols_.size() - 1);
            RefreshWorkspaceSymbolStatus();
        },
        serverKey);
}

void BufferView::RefreshWorkspaceSymbolStatus() {
    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(workspaceSymbolLabels_.empty()
                                 ? std::nullopt
                                 : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), workspaceSymbolLabels_,
                                                                               workspaceSymbolSelection_)));
    }
}

void BufferView::HandleWorkspaceSymbolKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        if (pendingWorkspaceSymbols_.empty()) {
            statusMessage_ = "No symbol matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }
        const editor::lsp::LspManager::SymbolResult symbol =
            pendingWorkspaceSymbols_[std::min(workspaceSymbolSelection_, pendingWorkspaceSymbols_.size() - 1)];
        EndInteractiveSession();
        JumpToDefinition(editor::lsp::LspManager::ResolvedLocation{.path = symbol.path, .position = symbol.position});
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Workspace symbol search cancelled.";
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        if (!pendingWorkspaceSymbols_.empty()) {
            workspaceSymbolSelection_ = chord.Special == editor::SpecialKey::Down
                                            ? (workspaceSymbolSelection_ + 1) % pendingWorkspaceSymbols_.size()
                                            : (workspaceSymbolSelection_ + pendingWorkspaceSymbols_.size() - 1) %
                                                  pendingWorkspaceSymbols_.size();
        }
        RefreshWorkspaceSymbolStatus();
        return;
    }
    // Typing doesn't re-request immediately -- the server does its own
    // query-matching, so hammering it on every keystroke is real,
    // avoidable request volume (unlike the local-list pickers above, where
    // FuzzyFilterAndRank is nearly free). Echo the prompt text right away
    // regardless -- only the result list itself waits for the debounce.
    // Pure cursor movement (CursorMoved) needs neither.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        workspaceSymbolSelection_ = 0;
        statusMessage_            = prompt_->StatusText();
        if (eventLoop_) {
            workspaceSymbolDebounceTimer_.Arm(*eventLoop_, std::chrono::milliseconds(editor::lsp::LspCompletionDebounceMs()),
                                              [this] { RequestWorkspaceSymbolsForCurrentQuery(); });
        }
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::SwitchHeaderSource() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "Buffer has no associated file.";
        return;
    }
    const std::filesystem::path path = *buffer.Path();

    // header-source-switching follow-up: LSP absence (no manager, no client
    // running for this buffer's language) falls straight to the filesystem
    // heuristic -- unlike lsp-goto-definition, that's the normal path for
    // every language except C/C++, not an error.
    if (!lspManager_) {
        OpenHeaderSourceCounterpartOrReport(path);
        return;
    }

    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = ++switchHeaderSourceRequestGeneration_;
    statusMessage_                 = "Switching header/source...";
    lspManager_->RequestSwitchSourceHeader(
        buffer, [this, bufferPtr, generation, path](std::optional<std::filesystem::path> counterpart) {
            if (generation != switchHeaderSourceRequestGeneration_ || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded/buffer switched since the request was sent
            }
            if (counterpart) {
                OpenHeaderSourceCounterpart(*counterpart);
                return;
            }
            OpenHeaderSourceCounterpartOrReport(path);
        });
}

void BufferView::OpenHeaderSourceCounterpartOrReport(const std::filesystem::path& path) {
    if (const auto counterpart = editor::headersource::FindCounterpart(path)) {
        OpenHeaderSourceCounterpart(*counterpart);
        return;
    }
    statusMessage_ = "No corresponding header/source file found.";
}

void BufferView::OpenHeaderSourceCounterpart(const std::filesystem::path& path) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBuffer_.Set(opened);
        statusMessage_.clear();
        ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
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
    // embedded-language-documents follow-up: see RequestDefinitionAtPoint's
    // identical comment above.
    const std::string serverKey = ResolvedLspServerKey(point);

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
            if (!result->hasEdit) {
                statusMessage_ = "No rename edits available.";
                return;
            }
            std::size_t fileCount = result->edits.size();
            std::size_t editCount = 0;
            for (const auto& edit : result->edits) {
                editCount += edit.edits.size();
            }
            // edit-application-gaps follow-up: a "documentChanges" rename
            // can also carry pure resource ops (a plain file rename/create/
            // delete with no text edits attached) alongside or instead of
            // EditFile entries -- counted separately so "0 edits across 0
            // files" doesn't read as a no-op when the rename is really just
            // moving a file.
            std::size_t opCount = 0;
            for (const auto& op : result->documentChangeOps) {
                if (op.kind == editor::lsp::DocumentChangeOp::Kind::EditFile) {
                    ++fileCount;
                    editCount += op.edits.size();
                }
                else {
                    ++opCount;
                }
            }
            renameTitle_ = std::to_string(editCount) + " edit" + (editCount == 1 ? "" : "s") + " across " +
                           std::to_string(fileCount) + " file" + (fileCount == 1 ? "" : "s");
            if (opCount > 0) {
                renameTitle_ += ", " + std::to_string(opCount) + " file op" + (opCount == 1 ? "" : "s");
            }

            ApplyRename(*result);
        },
        serverKey);
}

void BufferView::ApplyRename(const editor::lsp::LspManager::ResolvedRename& result) {
    ApplyResolvedWorkspaceEdit(result, "Renamed (" + renameTitle_ + ").");
}

bool BufferView::ApplyServerPushedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, const std::string& label) {
    return ApplyResolvedWorkspaceEdit(edit, "Applied \"" + label + "\" (server request).");
}

bool BufferView::ApplyResolvedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, std::string description) {
    if (edit.touchesUnsupportedForm || !edit.hasEdit) {
        statusMessage_ = description + " has no edit to apply.";
        return false;
    }

    // Resolve/apply everything through one try block -- a resource op
    // (Create/Rename/DeleteFile) that fails partway (an unresolvable path,
    // an already-existing create/rename target with neither overwrite nor
    // ignoreIfExists set, a missing delete target) leaves earlier steps'
    // filesystem side effects in place (they're not transactional the way
    // in-memory buffer edits are), but stops before touching anything else
    // -- the same "fail loud, don't guess" precedent HandleRenameFileKey/
    // HandleDeleteFileKey already establish for the equivalent
    // user-initiated commands.
    std::vector<std::pair<text::Buffer*, std::vector<editor::lsp::WorkspaceTextEdit>>> perBufferEdits;
    bool                                                                               touchedFilesystem = false;
    try {
        // Resolve (find-or-open) every "changes"-form buffer FIRST, same
        // all-or-nothing-open guarantee this method has always had.
        perBufferEdits.reserve(edit.edits.size() + edit.documentChangeOps.size());
        for (const editor::lsp::LspManager::ResolvedRenameEdit& renameEdit : edit.edits) {
            text::Buffer* buffer = bufferList_.FindByPath(renameEdit.path);
            if (!buffer) {
                buffer = &bufferList_.OpenFile(renameEdit.path);
            }
            perBufferEdits.emplace_back(buffer, renameEdit.edits);
        }

        // documentChangeOps apply strictly in order -- a resource op's
        // filesystem side effect must land before a later EditFile op that
        // targets the file it just created/renamed.
        using Kind = editor::lsp::DocumentChangeOp::Kind;
        for (const editor::lsp::LspManager::ResolvedDocumentChangeOp& op : edit.documentChangeOps) {
            switch (op.kind) {
                case Kind::CreateFile: {
                    const bool exists = std::filesystem::exists(op.path);
                    if (exists && op.ignoreIfExists) {
                        break;
                    }
                    if (exists && !op.overwrite) {
                        throw std::runtime_error("create: " + op.path.string() + " already exists");
                    }
                    text::Buffer& buffer = bufferList_.OpenOrCreateFile(op.path);
                    if (exists && op.overwrite) {
                        buffer.BeginUndoGroup();
                        buffer.DeleteRange(0, buffer.Content().ByteLength());
                        buffer.EndUndoGroup();
                    }
                    touchedFilesystem = true;
                    break;
                }
                case Kind::DeleteFile: {
                    if (!std::filesystem::exists(op.path)) {
                        if (op.ignoreIfNotExists) {
                            break;
                        }
                        throw std::runtime_error("delete: " + op.path.string() + " does not exist");
                    }
                    editor::DeleteProjectPath(op.path);
                    touchedFilesystem = true;
                    break;
                }
                case Kind::RenameFile: {
                    const bool targetExists = std::filesystem::exists(op.path);
                    if (targetExists && !op.overwrite && !op.ignoreIfExists) {
                        throw std::runtime_error("rename: " + op.path.string() + " already exists");
                    }
                    if (targetExists && op.ignoreIfExists && !op.overwrite) {
                        break; // target already there, told to leave it alone
                    }
                    if (targetExists && op.overwrite) {
                        editor::DeleteProjectPath(op.path); // RenameProjectPath refuses onto an existing target
                    }
                    editor::RenameProjectPath(op.oldPath, op.path);
                    if (text::Buffer* buffer = bufferList_.FindByPath(op.oldPath)) {
                        buffer->SetPath(op.path);
                        buffer->Rename(op.path.filename().string());
                        editor::ClearModeCacheFor(*buffer);
                    }
                    touchedFilesystem = true;
                    break;
                }
                case Kind::EditFile: {
                    text::Buffer* buffer = bufferList_.FindByPath(op.path);
                    if (!buffer) {
                        buffer = &bufferList_.OpenFile(op.path);
                    }
                    perBufferEdits.emplace_back(buffer, op.edits);
                    break;
                }
            }
        }
    }
    catch (const std::exception& e) {
        ReportError(description + " failed: " + e.what(), editor::LogCategory::Lsp);
        return false;
    }

    ApplyProjectEdit(perBufferEdits, description);
    if (touchedFilesystem && projectSidebar_) {
        projectSidebar_->InvalidateTree();
    }
    return true;
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
        case editor::InteractiveRequest::UniversalArgument:
            inputMode_ = InputMode::PrefixArgument;
            prefixArgReader_.emplace();
            statusMessage_ = prefixArgReader_->StatusText();
            return;
        case editor::InteractiveRequest::SnippetExpand:
            if (pendingSnippetExpansion_) {
                const auto request = std::move(*pendingSnippetExpansion_);
                pendingSnippetExpansion_.reset();
                BeginSnippetExpansion(request.replaceStart, request.replaceEnd, request.body);
            }
            return;
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
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        case editor::InteractiveRequest::SwitchToBuffer:
            inputMode_ = InputMode::SwitchToBuffer;
            prompt_.emplace("Switch to buffer: ");
            switchToBufferSelection_ = 0;
            RefreshSwitchToBufferStatus();
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
            // Chrome-redesign follow-up: hiding is a *collapse* now (the
            // sidebar stays active and paints a 1-column strip so the
            // double-click-to-expand affordance never vanishes -- see
            // ProjectSidebar.h), not a Widget::active flip.
            // VCS side panel follow-up: expanding the sidebar collapses
            // vcsPanel_ first (plain SetCollapsed, not a committing
            // CommitCollapsed -- this is a side effect of the *sidebar's*
            // own toggle, not a deliberate vcsPanel_ toggle, so it must not
            // overwrite vcsPanel_'s own persisted visibility preference) --
            // see SetVcsPanel's own doc comment for why the two stay
            // mutually exclusive on the shared left dock slot.
            if (projectSidebar_ != nullptr) {
                if (projectSidebar_->Collapsed() && vcsPanel_ != nullptr) {
                    vcsPanel_->SetCollapsed(true);
                }
                projectSidebar_->ToggleCollapsed();
            }
            return;
        case editor::InteractiveRequest::ToggleVcsPanel:
            // Same shape as ToggleProjectSidebar above, mirrored.
            if (vcsPanel_ != nullptr) {
                if (vcsPanel_->Collapsed() && projectSidebar_ != nullptr) {
                    projectSidebar_->SetCollapsed(true);
                }
                vcsPanel_->ToggleCollapsed();
            }
            return;
        case editor::InteractiveRequest::ToggleTerminal:
            // terminal-panel follow-up: one-shot direct action, same shape
            // as the window-management requests just below -- the panel
            // lives above this class, so only forward.
            if (onTerminalToggle_) {
                onTerminalToggle_();
            }
            return;
        case editor::InteractiveRequest::ListBuffers:
            // generic-popup follow-up: one-shot direct action, same shape as
            // ToggleTerminal above -- the buffer-list panel lives above this
            // class, so only forward.
            if (onBufferListToggle_) {
                onBufferListToggle_();
            }
            return;
        case editor::InteractiveRequest::FocusProjectSidebar:
            // sidebar-keyboard-focus follow-up: expands if collapsed (focus
            // into an invisible tree would be meaningless) and hands over
            // the keyboard -- ProjectSidebar's own OnEvent drives the
            // selection until it returns focus, re-collapsing then if it
            // was collapsed on entry (see TakeKeyboardFocus's own comment).
            // VCS side panel follow-up: same mutual-exclusion side effect
            // ToggleProjectSidebar above applies.
            if (projectSidebar_ != nullptr) {
                if (projectSidebar_->Collapsed() && vcsPanel_ != nullptr) {
                    vcsPanel_->SetCollapsed(true);
                }
                projectSidebar_->TakeKeyboardFocus();
            }
            return;
        case editor::InteractiveRequest::FocusVcsPanel:
            // Same shape as FocusProjectSidebar above, mirrored.
            if (vcsPanel_ != nullptr) {
                if (vcsPanel_->Collapsed() && projectSidebar_ != nullptr) {
                    projectSidebar_->SetCollapsed(true);
                }
                vcsPanel_->TakeKeyboardFocus();
            }
            return;
        case editor::InteractiveRequest::ToggleMinimap:
            // Same one-shot direct action shape as ToggleProjectSidebar
            // above, but flips both halves of the lockstep pair (see
            // BufferView.h's own comment on SetMinimap) -- exactly one of
            // the minimap/scroll-bar column ever occupies that screen
            // real estate.
            if (minimap_ != nullptr) {
                minimap_->active = !minimap_->active;
                if (!minimap_->active) {
                    // Paint() never runs again once active is false (Layout.h's
                    // Container skips inactive widgets outright), so this is the
                    // only place that can tear down a live pixel-blitter plane
                    // (Minimap.h's own ReleasePlane() doc comment).
                    minimap_->ReleasePlane();
                }
                editor::SetVariable("minimap-enabled", minimap_->active ? "true" : "false");
            }
            if (minimapScrollColumn_ != nullptr) {
                minimapScrollColumn_->active = !minimapScrollColumn_->active;
            }
            return;
        case editor::InteractiveRequest::ProjectAgenda:
            BuildAgendaMultibuffer();
            return;
        case editor::InteractiveRequest::ShowMessages:
            ShowMessagesBuffer();
            return;
        case editor::InteractiveRequest::OrgClockReport:
            BuildClockReportMultibuffer();
            return;
        case editor::InteractiveRequest::LspGotoDefinition:
            RequestDefinitionAtPoint(LspLocationKind::Definition);
            return;
        case editor::InteractiveRequest::LspGotoDeclaration:
            RequestDefinitionAtPoint(LspLocationKind::Declaration);
            return;
        case editor::InteractiveRequest::LspGotoTypeDefinition:
            RequestDefinitionAtPoint(LspLocationKind::TypeDefinition);
            return;
        case editor::InteractiveRequest::LspGotoImplementation:
            RequestDefinitionAtPoint(LspLocationKind::Implementation);
            return;
        case editor::InteractiveRequest::LspGotoSymbol:
            RequestDocumentSymbolsAtPoint();
            return;
        // symbol-search follow-up: unlike LspGotoSymbol, this session opens
        // right away (workspace/symbol has no local candidate list to wait
        // on, only a live server round trip) -- mirrors ExecuteCommand's own
        // "populate right away" shape, just via an async request instead of
        // a free in-memory lookup.
        case editor::InteractiveRequest::LspWorkspaceSymbol:
            if (!lspManager_) {
                statusMessage_ = "No LSP manager available.";
                return;
            }
            pendingWorkspaceSymbols_.clear();
            workspaceSymbolLabels_.clear();
            inputMode_ = InputMode::LspWorkspaceSymbol;
            prompt_.emplace("Workspace symbol: ");
            workspaceSymbolSelection_ = 0;
            RequestWorkspaceSymbolsForCurrentQuery();
            return;
        // call/type-hierarchy follow-up: four more one-shot direct actions,
        // same shape as LspGotoSymbol above -- RequestHierarchyAtPoint owns
        // the actual prepare/expand/browse session.
        case editor::InteractiveRequest::LspCallHierarchyIncoming:
            RequestHierarchyAtPoint(HierarchyDirection::IncomingCalls);
            return;
        case editor::InteractiveRequest::LspCallHierarchyOutgoing:
            RequestHierarchyAtPoint(HierarchyDirection::OutgoingCalls);
            return;
        case editor::InteractiveRequest::LspTypeHierarchySupertypes:
            RequestHierarchyAtPoint(HierarchyDirection::Supertypes);
            return;
        case editor::InteractiveRequest::LspTypeHierarchySubtypes:
            RequestHierarchyAtPoint(HierarchyDirection::Subtypes);
            return;
        case editor::InteractiveRequest::SwitchHeaderSource:
            SwitchHeaderSource();
            return;
        case editor::InteractiveRequest::JumpBack:
            JumpBack();
            return;
        case editor::InteractiveRequest::JumpForward:
            JumpForward();
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
        case editor::InteractiveRequest::TabNext:
        case editor::InteractiveRequest::TabPrevious: {
            // Tab-cycling follow-up: one-shot direct action -- next/previous
            // in Buffers() (tab bar) order, wrapping at either end. Set()
            // fires the MRU touch hook like any other switch, so tab-cycling
            // and MRU close stay consistent for free.
            const auto&         buffers = bufferList_.Buffers();
            const text::Buffer* active  = &activeBuffer_.Get();
            for (std::size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i].get() != active) {
                    continue;
                }
                const std::size_t count = buffers.size();
                const std::size_t next  = (request == editor::InteractiveRequest::TabNext)
                                              ? (i + 1) % count
                                              : (i + count - 1) % count;
                activeBuffer_.Set(*buffers[next]);
                break;
            }
            return;
        }
        case editor::InteractiveRequest::Recenter: {
            // One-shot direct action, same shape as ToggleProjectSidebar --
            // topLine_ is this widget's own state, so the command can only
            // request the scroll. SetTopLine clamps via MaxTopLine.
            text::Buffer&     buffer    = activeBuffer_.Get();
            const std::size_t pointLine = buffer.Content().ByteOffsetToLine(buffer.Point());
            const std::size_t half      = static_cast<std::size_t>(std::max(0, size().height)) / 2;
            SetTopLine(pointLine > half ? pointLine - half : 0);
            return;
        }
        case editor::InteractiveRequest::GotoLine:
            inputMode_ = InputMode::GotoLine;
            prompt_.emplace("Goto line: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::ConfirmOverwriteSave:
            inputMode_     = InputMode::ConfirmOverwriteSave;
            statusMessage_ = activeBuffer_.Get().Name() + " changed on disk since it was read; save anyway? (y/n)";
            return;
        case editor::InteractiveRequest::ConfirmSaveWithConflicts:
            inputMode_     = InputMode::ConfirmSaveWithConflicts;
            statusMessage_ = activeBuffer_.Get().Name() + " still has unresolved <<<<<<< conflict markers; save anyway? (y/n)";
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
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        case editor::InteractiveRequest::RecoverFile: {
            // backup-and-recovery follow-up: both no-session outcomes
            // (pathless buffer, nothing backed up) report and stay Normal.
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path().has_value()) {
                statusMessage_ = "Buffer " + buffer.Name() + " has no file to recover";
                return;
            }
            recoverVersions_ = editor::ListBackupVersions(*buffer.Path());
            if (recoverVersions_.empty()) {
                statusMessage_ = "No backups for " + buffer.Name();
                return;
            }
            inputMode_    = InputMode::RecoverFile;
            recoverStage_ = RecoverFileStage::PickingVersion;
            prompt_.emplace("Recover " + buffer.Name() + " -- version (1-" + std::to_string(recoverVersions_.size()) + ", Enter=1): ");
            std::string candidates;
            for (std::size_t index = 0; index < recoverVersions_.size(); ++index) {
                candidates += (index == 0 ? "" : ", ") + std::to_string(index + 1) + ": " + recoverVersions_[index].label;
            }
            statusMessage_ = prompt_->StatusText() + "  {" + candidates + "}";
            return;
        }
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
        // test-runner integration: one-shot direct actions (no prompt --
        // one project-wide test command, see the enum's own comment).
        case editor::InteractiveRequest::RunTests: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            const bool alreadyRunning = testRunner_->IsRunning();
            if (text::Buffer* buffer = testRunner_->RunAll()) {
                activeBuffer_.Set(*buffer);
            }
            statusMessage_ = alreadyRunning ? "Tests already running." : (testRunner_->IsRunning() ? "Running tests..." : "");
            return;
        }
        case editor::InteractiveRequest::CancelTests:
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
            }
            else if (testRunner_->IsRunning()) {
                testRunner_->Cancel();
                statusMessage_ = "Cancelling tests...";
            }
            else {
                statusMessage_ = "No test run in progress.";
            }
            return;
        case editor::InteractiveRequest::ShowTestResults: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            const std::optional<editor::testrun::TestRunOutcome>& outcome = testRunner_->LatestOutcome();
            if (!outcome) {
                statusMessage_ = "No test results yet -- run-tests (C-c T t) first.";
                return;
            }
            editor::SetLastResultsBuffer(editor::testrun::TestResultsBufferName());
            activeBuffer_.Set(editor::testrun::RebuildTestResultsBuffer(bufferList_, *outcome));
            return;
        }
        case editor::InteractiveRequest::RunTestAtPoint: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            if (!mode_.testDiscovery) {
                statusMessage_ = "No test discovery configured for " + mode_.name + ".";
                return;
            }
            if (!editor::testrun::TestFilterCommand()) {
                statusMessage_ = "No test filter command configured (see ned/set-test-filter-command).";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            // Innermost (smallest-span) discovered definition containing
            // point -- a describe() picks the it() under point, a PHPUnit
            // class its method.
            const std::size_t                 point = buffer.Point();
            std::optional<editor::TestMarker> target;
            for (editor::TestMarker& marker : mode_.testDiscovery(buffer.Text())) {
                if (marker.startByte <= point && point < marker.endByte &&
                    (!target || (marker.endByte - marker.startByte) < (target->endByte - target->startByte))) {
                    target = std::move(marker);
                }
            }
            if (!target) {
                statusMessage_ = "No test definition at point.";
                return;
            }
            const std::string file = buffer.Path() ? buffer.Path()->string() : std::string();
            if (text::Buffer* output = testRunner_->RunFiltered(target->name, file)) {
                activeBuffer_.Set(*output);
            }
            statusMessage_ = "Running \"" + target->name + "\"...";
            return;
        }
        case editor::InteractiveRequest::RerunFailedTests: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            if (testRunner_->IsRunning()) {
                statusMessage_ = "Tests already running.";
                return;
            }
            if (!editor::testrun::TestFilterCommand()) {
                statusMessage_ = "No test filter command configured (see ned/set-test-filter-command).";
                return;
            }
            const std::size_t queued = testRunner_->RerunFailed();
            if (queued == 0) {
                statusMessage_ = "No failed tests to re-run.";
                return;
            }
            if (text::Buffer* output = bufferList_.Find(editor::testrun::TestOutputBufferName())) {
                activeBuffer_.Set(*output);
            }
            statusMessage_ = "Re-running " + std::to_string(queued) + " failed test" + (queued == 1 ? "" : "s") +
                             " sequentially...";
            return;
        }
        // DAP client slice 1: one-shot direct actions, same shape as
        // VcsShowBlame just below -- the synchronous half of each answer
        // (DapManager's returned status string) lands in statusMessage_
        // immediately; async outcomes (a breakpoint hit, the session
        // ending) arrive later through the WindowManager-wired
        // SetOnStopped/SetOnSessionEnded callbacks, not here.
        case editor::InteractiveRequest::DapContinue:
            statusMessage_ = dapManager_ ? dapManager_->StartOrContinue(editor::LanguageKeyForMode(mode_)) : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapAttach:
            statusMessage_ = dapManager_ ? dapManager_->Attach(editor::LanguageKeyForMode(mode_)) : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStop:
            statusMessage_ = dapManager_ ? dapManager_->StopSession() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapPause:
            statusMessage_ = dapManager_ ? dapManager_->Pause() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapToggleBreakpoint: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to set a breakpoint in.";
                return;
            }
            const std::size_t line   = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-based, DAP's own convention
            const bool        nowSet = dapManager_->ToggleBreakpoint(*buffer.Path(), line);
            statusMessage_           = (nowSet ? "Breakpoint set at " : "Breakpoint removed at ") + buffer.Path()->filename().string() +
                                       ":" + std::to_string(line);
            return;
        }
        // DAP slices 2/3: stepping is the same immediate-status shape as
        // DapContinue above; DapShowDebug/DapExpandVariable chain async
        // requests (see each method's own doc comment); DapEvaluate is the
        // family's one prompt session.
        case editor::InteractiveRequest::DapStepOver:
            statusMessage_ = dapManager_ ? dapManager_->StepOver() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStepInto:
            statusMessage_ = dapManager_ ? dapManager_->StepInto() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStepOut:
            statusMessage_ = dapManager_ ? dapManager_->StepOut() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapShowDebug:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowDebugInfo();
            }
            return;
        case editor::InteractiveRequest::DapExpandVariable:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ExpandVariableAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapRestartFrame:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                RestartFrameAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapShowDisassembly:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowDisassemblyAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapShowMemoryAtPoint:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowMemoryAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapEvaluate:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapEvaluate;
            prompt_.emplace("Evaluate: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // DAP round 2: SetBreakpointCondition/LogMessage capture point's own
        // line (dap-toggle-breakpoint's own convention) into
        // pendingDapBreakpointTarget_ before entering their prompt --
        // HandlePromptKey's DapBreakpointCondition/DapBreakpointLogMessage/
        // DapBreakpointHitCondition (round 3) branches consume it on Enter.
        case editor::InteractiveRequest::DapSetBreakpointCondition:
        case editor::InteractiveRequest::DapSetBreakpointLogMessage:
        case editor::InteractiveRequest::DapSetBreakpointHitCondition: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to set a breakpoint in.";
                return;
            }
            const std::size_t line = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1;
            pendingDapBreakpointTarget_ = PendingDapBreakpointTarget{.path = *buffer.Path(), .line = line};
            switch (request) {
                case editor::InteractiveRequest::DapSetBreakpointCondition:
                    inputMode_ = InputMode::DapBreakpointCondition;
                    prompt_.emplace("Condition (empty to clear): ");
                    break;
                case editor::InteractiveRequest::DapSetBreakpointLogMessage:
                    inputMode_ = InputMode::DapBreakpointLogMessage;
                    prompt_.emplace("Log message (empty to clear): ");
                    break;
                default: // DapSetBreakpointHitCondition
                    inputMode_ = InputMode::DapBreakpointHitCondition;
                    prompt_.emplace("Hit condition (empty to clear): ");
                    break;
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
        case editor::InteractiveRequest::DapToggleFunctionBreakpoint:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapFunctionBreakpointName;
            prompt_.emplace("Function breakpoint name: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DapSelectExceptionBreakpoints:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                BeginDapExceptionFilterSelect();
            }
            return;
        case editor::InteractiveRequest::DapAddWatch:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapAddWatch;
            prompt_.emplace("Add watch: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DapRemoveWatch:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                RemoveWatchAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapSelectThread:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                BeginDapThreadSelect();
            }
            return;
        case editor::InteractiveRequest::DapSetVariable:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                SetVariableAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapToggleConsole:
            // The debug console is an OverlayHost overlay above even
            // WindowManager's level, same shape as ToggleTerminal/
            // AcpTogglePanel above -- only forward.
            if (onDapConsoleToggle_) {
                onDapConsoleToggle_();
            }
            return;
        // ACP client slice 2: AcpStartSession/AcpSendPrompt are prompt-shaped
        // (HandlePromptKey), same "just enter the mode and prime the
        // prompt" shape as DapEvaluate above; AcpStopSession is a one-shot
        // direct action, same shape as DapStop.
        case editor::InteractiveRequest::AcpStartSession:
            inputMode_ = InputMode::AcpAgentName;
            prompt_.emplace("ACP agent: ");
            acpAgentNameSelection_ = 0;
            RefreshAcpAgentNameStatus();
            return;
        case editor::InteractiveRequest::AcpSendPrompt:
            if (!acpManager_) {
                statusMessage_ = "No ACP manager available.";
                return;
            }
            inputMode_ = InputMode::AcpPromptText;
            prompt_.emplace("ACP prompt: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::AcpStopSession:
            statusMessage_ = acpManager_ ? acpManager_->StopSession() : "No ACP manager available.";
            return;
        case editor::InteractiveRequest::AcpTogglePanel:
            // ACP chat panel: one-shot direct action, same shape as
            // ToggleTerminal above -- the panel lives above this class,
            // only forward.
            if (onAcpPanelToggle_) {
                onAcpPanelToggle_();
            }
            return;
        case editor::InteractiveRequest::AcpRewind:
            // ACP checkpoint/rewind follow-up: one-shot direct action, same
            // "just forward, the target lives above this class" shape as
            // AcpTogglePanel immediately above -- the picker itself lives in
            // AcpPanel. Refused while a prompt is in flight: the agent could
            // still be mid-write, and rewinding out from under that would
            // race the very undo-sequence bookkeeping RewindTo relies on.
            if (!acpManager_) {
                statusMessage_ = "No ACP manager available.";
            }
            else if (acpManager_->PromptInFlight()) {
                statusMessage_ = "Can't rewind while a prompt is in flight.";
            }
            else if (onAcpRewindRequest_) {
                onAcpRewindRequest_();
            }
            return;
        // VCS blame gutter follow-up: one-shot direct actions, same shape
        // as ProjectAgenda/LspGotoDefinition above -- doesn't touch
        // inputMode_, the async result (or a status-message error) arrives
        // later via VcsRunner's own callback. VcsShowBlame stays on the
        // current buffer (see Command.h's own doc comment for why);
        // VcsBlameDetailAtPoint is synchronous, no async request at all.
        case editor::InteractiveRequest::VcsShowBlame:
            RequestBlameForCurrentBuffer();
            return;
        case editor::InteractiveRequest::VcsBlameDetailAtPoint:
            ShowBlameDetailAtPoint();
            return;
        case editor::InteractiveRequest::VcsBlameBuffer:
            RequestVcsBlameBuffer();
            return;
        case editor::InteractiveRequest::VcsShowLog:
            RequestVcsLogBuffer();
            return;
        case editor::InteractiveRequest::VisitVcsResult:
            VisitVcsResult();
            return;
        // VCS vocabulary-completion follow-up: status/stage/unstage/
        // branches are one-shot direct actions (async results via
        // VcsRunner callbacks, same as VcsShowLog above); commit and
        // create-branch open a prompt directly; switch-branch defers its
        // prompt until the branch list arrives (see
        // BeginVcsSwitchBranchPrompt's own doc comment).
        case editor::InteractiveRequest::VcsStatus:
            RequestVcsStatusBuffer();
            return;
        case editor::InteractiveRequest::VcsStageFile:
            StageOrUnstageFileAtPoint(true);
            return;
        case editor::InteractiveRequest::VcsUnstageFile:
            StageOrUnstageFileAtPoint(false);
            return;
        case editor::InteractiveRequest::VcsStageHunk:
            StageOrUnstageHunkAtPoint(true);
            return;
        case editor::InteractiveRequest::VcsUnstageHunk:
            StageOrUnstageHunkAtPoint(false);
            return;
        // Hunk-navigation follow-up: pure point motion against the
        // already-cached diffHunkStartLines_, no VcsRunner round trip and
        // no Modified() gate (see JumpToNextHunk/JumpToPreviousHunk's own
        // doc comments).
        case editor::InteractiveRequest::VcsNextHunk:
            JumpToNextHunk();
            return;
        case editor::InteractiveRequest::VcsPreviousHunk:
            JumpToPreviousHunk();
            return;
        // next-error follow-up: same point-motion shape as VcsNextHunk/
        // VcsPreviousHunk just above, generalized over Editor/NextError.h's
        // "last results buffer" instead of a private per-pane cache.
        case editor::InteractiveRequest::NextError:
            NextError();
            return;
        case editor::InteractiveRequest::PreviousError:
            PreviousError();
            return;
        case editor::InteractiveRequest::VcsFullDiffBuffer:
            RequestVcsFullDiffBuffer();
            return;
        case editor::InteractiveRequest::DiagnosticsBuffer:
            RequestDiagnosticsBuffer();
            return;
        case editor::InteractiveRequest::ProjectFindReferences:
            RequestProjectFindReferences();
            return;
        case editor::InteractiveRequest::VcsCommit:
            BeginVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsCommitFinish:
            FinishVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsCommitAbort:
            AbortVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsBranches:
            RequestVcsBranchesBuffer();
            return;
        case editor::InteractiveRequest::VcsSwitchBranch:
            BeginVcsSwitchBranchPrompt();
            return;
        case editor::InteractiveRequest::VcsCreateBranch:
            BeginVcsCreateBranchPrompt();
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
        // property-drawers follow-up: the first stage of org-set-property's
        // two-stage session -- see HandleSetPropertyKey for the second.
        case editor::InteractiveRequest::SetProperty:
            inputMode_     = InputMode::SetProperty;
            propertyStage_ = PropertyPromptStage::EnteringName;
            prompt_.emplace("Property name: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // org-delete-property already checked HeadlineAtPoint before setting
        // this request (org-set-tags's own precedent) -- one prompt, fits
        // the shared HandlePromptKey else-chain directly.
        case editor::InteractiveRequest::DeleteProperty:
            inputMode_ = InputMode::DeleteProperty;
            prompt_.emplace("Delete property: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // scheduling/recurrence follow-up: org-schedule/org-deadline already
        // checked HeadlineAtPoint before setting this request (org-set-tags's
        // own precedent) -- resolving it again here (point can't have moved
        // since dispatch, same reasoning SetHeadlineTags's own case above
        // states) is what lets the prompt pre-fill with the headline's
        // *current* SCHEDULED:/DEADLINE: timestamp, if it has one.
        case editor::InteractiveRequest::OrgSchedule:
        case editor::InteractiveRequest::OrgDeadline: {
            const bool isDeadline = (request == editor::InteractiveRequest::OrgDeadline);
            inputMode_            = isDeadline ? InputMode::OrgDeadline : InputMode::OrgSchedule;
            prompt_.emplace(isDeadline ? "Deadline: " : "Schedule: ");
            const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get());
            if (headline) {
                const auto  planning = editor::org::ParsePlanning(activeBuffer_.Get().Text(), *headline);
                const auto& existing = isDeadline ? (planning ? planning->deadline : std::nullopt)
                                                  : (planning ? planning->scheduled : std::nullopt);
                if (existing) {
                    prompt_->SetText(editor::org::FormatTimestamp(*existing));
                }
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
        // org-capture follow-up: same one-character-read shape as
        // PointToRegister/etc. above -- no MinibufferPrompt, the status line
        // itself lists every registered template so the key isn't something
        // the user has to memorize blind.
        case editor::InteractiveRequest::OrgCapture: {
            const std::vector<editor::org::CaptureTemplate> templates = editor::org::CaptureTemplates();
            if (templates.empty()) {
                statusMessage_ = "No capture templates configured.";
                return;
            }
            std::string label = "Capture: ";
            for (const editor::org::CaptureTemplate& tmpl : templates) {
                label += "[";
                label += tmpl.key;
                label += "] " + tmpl.name + "  ";
            }
            inputMode_     = InputMode::OrgCaptureSelectTemplate;
            statusMessage_ = label;
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
        // editor-ergonomics follow-up: ProjectFindFile's own "populate and
        // show the full candidate list right away" shape, over
        // editor::RecentFilePaths() (a cheap in-memory read, not a
        // directory walk) instead of BuildProjectTree.
        case editor::InteractiveRequest::FindRecentFile: {
            recentFileCandidates_ = editor::RecentFilePaths();
            if (recentFileCandidates_.empty()) {
                statusMessage_ = "No recently opened files";
                return;
            }
            inputMode_ = InputMode::FindRecentFile;
            prompt_.emplace("Find recent file (fuzzy): ");
            recentFileSelection_ = 0;
            RefreshFindRecentFileStatus();
            return;
        }
        // named-projects follow-up: ProjectFindFile/FindRecentFile's own
        // "populate and show the full candidate list right away" shape.
        case editor::InteractiveRequest::SwitchProject: {
            switchProjectEntries_ = editor::ListProjects();
            if (switchProjectEntries_.empty()) {
                statusMessage_ = "No registered projects yet -- try open-project.";
                return;
            }
            inputMode_ = InputMode::SwitchProject;
            prompt_.emplace("Switch to project (fuzzy): ");
            switchProjectSelection_ = 0;
            RefreshSwitchProjectStatus();
            return;
        }
        // named-projects follow-up: FindFile's own plain path-entry prompt
        // shape -- Enter (HandlePromptKey's own OpenProjectPath branch)
        // decides whether a second (name) prompt is needed.
        case editor::InteractiveRequest::OpenProject:
            inputMode_ = InputMode::OpenProjectPath;
            prompt_.emplace("Open project (path): ");
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        // editor-ergonomics follow-up: bookmark-set already checked
        // context.buffer.Path() before setting this (Commands.cpp), so
        // activeBuffer_.Get() is guaranteed file-backed here -- pre-fills
        // with the buffer's own display name, the common case (one
        // bookmark per file) needing no typing at all, just Enter.
        case editor::InteractiveRequest::BookmarkSet:
            inputMode_ = InputMode::BookmarkSetName;
            prompt_.emplace("Bookmark name: ");
            prompt_->SetText(activeBuffer_.Get().Name());
            statusMessage_ = prompt_->StatusText();
            return;
        // editor-ergonomics follow-up: BookmarkJump/BookmarkDelete share
        // one picker (InputMode::BookmarkJump) over editor::BookmarkNames(),
        // TaskName's own RunTask/CancelTask precedent for two
        // InteractiveRequests resolving to the same InputMode.
        case editor::InteractiveRequest::BookmarkJump:
        case editor::InteractiveRequest::BookmarkDelete: {
            const bool isDelete = (request == editor::InteractiveRequest::BookmarkDelete);
            bookmarkCandidates_ = editor::BookmarkNames();
            if (bookmarkCandidates_.empty()) {
                statusMessage_ = "No bookmarks set";
                return;
            }
            bookmarkPromptAction_ = isDelete ? BookmarkPromptAction::Delete : BookmarkPromptAction::Jump;
            inputMode_            = InputMode::BookmarkJump;
            prompt_.emplace(isDelete ? "Delete bookmark (fuzzy): " : "Jump to bookmark (fuzzy): ");
            bookmarkSelection_ = 0;
            RefreshBookmarkJumpStatus();
            return;
        }
        // rich-theme-set follow-up (Phase 1): ProjectFindFile's "populate
        // and show the full candidate list right away" shape over
        // ui::ThemeNames(). No applier wired (headless BufferView tests
        // that never call SetThemeApplier) means there's nothing a picked
        // theme could be applied *to*, so report instead of opening a
        // session whose Enter would silently do nothing.
        //
        // select-theme-current-row follow-up: selectThemeCandidates_ gets
        // one synthetic entry (kCurrentThemeLabel) prepended ahead of every
        // real registry name, and the session opens on it -- so the very
        // top of the list, always, rather than trying to locate the active
        // theme's own row among the real ones (which meant either
        // previewing it immediately on open, a destructive no-op that
        // strips init.janet's own overrides -- see kCurrentThemeLabel's own
        // doc comment -- or leaving a highlighted-but-unapplied row that
        // looked like nothing had happened). Opening the picker now
        // previews nothing and touches theme_ not at all, full stop.
        case editor::InteractiveRequest::SelectTheme: {
            if (!themeApplier_) {
                statusMessage_ = "Theme switching is not wired up.";
                return;
            }
            selectThemeCandidates_ = ThemeNames();
            selectThemeCandidates_.insert(selectThemeCandidates_.begin(), std::string(kCurrentThemeLabel));
            themeBeforePreview_    = theme_;
            inputMode_             = InputMode::SelectTheme;
            prompt_.emplace("Theme (fuzzy): ");
            selectThemeSelection_ = 0;
            RefreshSelectThemeStatus();
            return;
        }
        // theme-editing follow-up: one-shot direct action, ToggleProjectSidebar's
        // shape. Writes whatever theme is *currently showing* -- picker-
        // committed, ned/set-theme'd, override-adjusted, or the ANSI
        // fallback -- as runnable Janet, so "pick something close, save it,
        // edit the file" is the whole theme-authoring workflow.
        case editor::InteractiveRequest::SaveTheme:
            try {
                const std::filesystem::path path = ThemeJanetFilePath();
                SaveThemeJanetFile(theme_, path);
                statusMessage_ = "Saved theme to " + path.string();
            }
            catch (const std::exception& e) {
                ReportError(e.what());
            }
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
        // Emacs-keymap-round-2 follow-up: same one-character-read shape as
        // the register requests just above.
        case editor::InteractiveRequest::ZapToChar:
            inputMode_     = InputMode::ZapToChar;
            statusMessage_ = "Zap to char: ";
            return;
        // kill-rectangle/delete-rectangle/yank-rectangle follow-up: one-shot
        // direct actions, same shape as ToggleProjectSidebar -- inputMode_
        // stays Normal, no prompt session. See Editor/Rectangle.h for where
        // the actual operations live.
        case editor::InteractiveRequest::KillRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No rectangle region selected.";
                }
                else {
                    editor::KillRectangle(buffer, editor::TabWidth());
                    statusMessage_.clear();
                }
                return;
            }
            // multi-cursor-round-2 follow-up: each cursor already carries
            // its own optional mark, so KillRectangle itself needs no
            // change -- just run it per cursor and collect what it
            // published to the (single-slot) clipboard each time into one
            // multi-block entry afterward.
            std::vector<std::vector<std::string>> blocks;
            bool                                  any = false;
            buffer.ForEachCursor([&] {
                if (buffer.HasMark()) {
                    editor::KillRectangle(buffer, editor::TabWidth());
                    blocks.push_back(editor::GlobalRectangleClipboard().Lines());
                    any = true;
                }
                else {
                    blocks.emplace_back();
                }
            });
            if (any) {
                editor::SetRectangleClipboardBlocks(std::move(blocks));
                statusMessage_.clear();
            }
            else {
                statusMessage_ = "No rectangle region selected.";
            }
            return;
        }
        case editor::InteractiveRequest::DeleteRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No rectangle region selected.";
                }
                else {
                    editor::DeleteRectangle(buffer, editor::TabWidth());
                    statusMessage_.clear();
                }
                return;
            }
            bool any = false;
            buffer.ForEachCursor([&] {
                if (buffer.HasMark()) {
                    editor::DeleteRectangle(buffer, editor::TabWidth());
                    any = true;
                }
            });
            statusMessage_ = any ? std::string() : "No rectangle region selected.";
            return;
        }
        case editor::InteractiveRequest::YankRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (editor::GlobalRectangleClipboard().Empty()) {
                statusMessage_ = "No rectangle to yank.";
                return;
            }
            if (!buffer.HasSecondaryCursors()) {
                editor::YankRectangle(buffer, editor::TabWidth());
                statusMessage_.clear();
                return;
            }
            // multi-cursor-round-2 follow-up: 1:1 block-per-cursor when the
            // block count matches how many cursors are live right now, else
            // fall back to the single most-recent block (Lines()) at every
            // cursor -- narrower than KillRing/RegisterTable's own "join
            // into one blob" mismatch fallback, see RectangleClipboard's
            // own doc comment for why.
            const auto&       blocks      = editor::GlobalRectangleClipboard().Blocks();
            const std::size_t cursorCount = 1 + buffer.SecondaryCursors().size();
            const bool        perCursor   = blocks.size() == cursorCount;
            std::size_t       i           = 0;
            buffer.ForEachCursor([&] {
                editor::YankRectangleLines(buffer, perCursor ? blocks[i] : editor::GlobalRectangleClipboard().Lines(),
                                           editor::TabWidth());
                ++i;
            });
            statusMessage_.clear();
            return;
        }
        // string-rectangle follow-up: the one rectangle command that's a
        // real prompt session (needs one line of typed replacement text) --
        // HasMark() is checked here, before ever opening the prompt, so
        // there's nothing to cancel out of if there's no region at all.
        // multi-cursor-round-2 follow-up: with secondary cursors active,
        // this optimistically opens the prompt without pre-scanning every
        // cursor for a mark (that scan would need its own ForEachCursor
        // pass just to answer a yes/no gate) -- if it turns out none of
        // them have one, the confirm handler below silently does nothing,
        // a narrow, accepted v1 gap rather than a full pre-check.
        case editor::InteractiveRequest::StringRectangle:
            if (!activeBuffer_.Get().HasMark() && !activeBuffer_.Get().HasSecondaryCursors()) {
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
        // Split-resize follow-up: same forward-only shape as the five
        // above -- unlike those, none of these ever reshape the tree (a
        // resize only mutates a WindowNode's own ratio), so `this` stays
        // valid afterward and these are deliberately NOT part of
        // IsWindowManagementRequest's own "the pane may be gone" check.
        case editor::InteractiveRequest::EnlargeWindow:
        case editor::InteractiveRequest::ShrinkWindow:
        case editor::InteractiveRequest::EnlargeWindowHorizontally:
        case editor::InteractiveRequest::ShrinkWindowHorizontally:
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
        // doesn't touch inputMode_, completion state coexists with ordinary
        // Normal-mode editing rather than replacing it (see
        // ActiveCompletion's own doc comment in BufferView.h).
        case editor::InteractiveRequest::LspComplete:
            RequestCompletionAtPoint();
            return;
        // documentHighlight follow-up: manual M-x entry point into the same
        // RequestDocumentHighlightAtPoint the live-on-cursor-move path (see
        // MaybeScheduleDocumentHighlight) already drives.
        case editor::InteractiveRequest::LspDocumentHighlight:
            RequestDocumentHighlightAtPoint();
            return;
        // code-actions follow-up: also a one-shot direct action -- inputMode_
        // is deliberately left untouched here, only changed later, from
        // inside RequestCodeActionsAtPoint's own async callback once the
        // response actually arrives (see that method's own doc comment).
        case editor::InteractiveRequest::LspCodeAction:
            RequestCodeActionsAtPoint();
            return;
        // quick-fix follow-up: same one-shot shape as LspCodeAction just
        // above; only enters an InputMode when the fix choice turns out to
        // be genuinely ambiguous (see RequestQuickFixAtPoint's doc comment).
        case editor::InteractiveRequest::LspQuickFix:
            RequestQuickFixAtPoint();
            return;
        // codeLens follow-up: same one-shot shape as LspQuickFix just
        // above -- never enters an InputMode at all.
        case editor::InteractiveRequest::LspRunCodeLensAtPoint:
            RequestCodeLensAtPoint();
            return;
    }

    statusMessage_ = (inputMode_ == InputMode::QueryReplace) ? queryReplace_->StatusText() : SearchStatusText();
}

std::string BufferView::SearchStatusText() const {
    std::string text = search_->StatusText();
    if (search_->Query().empty() && !lastSearchQuery_.empty()) {
        text += GhostForEchoArea(lastSearchQuery_);
    }
    return text;
}

void BufferView::BeginSnippetExpansion(std::size_t replaceStart, std::size_t replaceEnd, const std::string& body) {
    // Any prior session's ranges must never leak into a fresh expansion
    // (unreachable through TAB, which a live session consumes, but the LSP
    // accept path and M-x expand-snippet land here too).
    EndSnippetSession();
    text::Buffer& buffer  = activeBuffer_.Get();
    auto          session = editor::SnippetSession::Start(buffer, buffer.Name(), replaceStart, replaceEnd,
                                                          editor::ParseSnippet(body));
    if (session) {
        snippetSession_.emplace(std::move(*session));
        inputMode_     = InputMode::Snippet;
        statusMessage_ = snippetSession_->StatusText();
    }
    ClampPointToNarrowing();
    ScrollToShowPoint();
}

text::Buffer* BufferView::ResolveSnippetBuffer() {
    if (!snippetSession_) {
        return nullptr;
    }
    if (text::Buffer* buffer = bufferList_.Find(snippetSession_->BufferName())) {
        return buffer;
    }
    if (activeBuffer_.Get().Name() == snippetSession_->BufferName()) {
        return &activeBuffer_.Get();
    }
    return nullptr;
}

void BufferView::EndSnippetSession() {
    if (snippetSession_) {
        if (text::Buffer* buffer = ResolveSnippetBuffer()) {
            buffer->ClearSnippetRanges();
        }
        snippetSession_.reset();
    }
    pendingSnippetExpansion_.reset();
    snippetPendingPristineDelete_ = false;
    if (inputMode_ == InputMode::Snippet) {
        inputMode_ = InputMode::Normal;
    }
}

void BufferView::EndInteractiveSession() {
    inputMode_ = InputMode::Normal;
    // generic-popup follow-up (Phase 3): unconditional -- hides whichever
    // candidate popup this session may have been driving, a safe no-op if
    // it wasn't (same tolerance SetOnPrefixHintChanged's own nullopt case
    // has).
    if (onCandidatesChanged_) {
        onCandidatesChanged_(std::nullopt);
    }
    // snippet-expansion follow-up: a snippet session ending through this
    // shared reset (any other session's own end path) clears its
    // buffer-side ranges too, not just the members.
    EndSnippetSession();
    search_.reset();
    queryReplace_.reset();
    prompt_.reset();
    promptHistoryIndex_ = kNoHistoryIndex;
    promptHistoryStash_.clear();
    projectReplace_.reset();
    pendingClose_ = nullptr;
    pendingBinaryOpenPath_.clear();
    pendingZapToCharAppend_ = false;
    pendingTrustInitPath_.clear();
    onTrustDecision_ = nullptr;
    deleteStage_     = DeleteFileStage::EnteringPath;
    deleteTarget_.clear();
    renameStage_ = RenameFileStage::EnteringSource;
    renameSource_.clear();
    propertyStage_ = PropertyPromptStage::EnteringName;
    pendingPropertyName_.clear();
    executeCommandSelection_  = 0;
    projectFindFileSelection_ = 0;
    projectFindFileCandidates_.clear(); // cached only for the duration of one session -- see its own doc comment in BufferView.h
    selectThemeSelection_ = 0;
    selectThemeCandidates_.clear();
    // The cancel path re-applies this snapshot *before* calling here; the
    // commit path applies the selected theme instead and lets this drop.
    themeBeforePreview_.reset();
    pendingCodeActions_.clear();
    codeActionSelection_ = 0;
    pendingDefinitions_.clear();
    definitionSelection_ = 0;
    renameTitle_.clear();
    ScrollToShowPoint();
}

void BufferView::HandleSearchKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Accept();
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Cancel();
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Backspace) {
        search_->DeleteChar();
    }
    else if (chord.Control && chord.Codepoint == U's') {
        // An empty query recalls the last search string outright (real
        // Emacs: C-s/C-r on a fresh isearch reuses the previous one, shown
        // ghosted via SearchStatusText() up to this point). Otherwise, C-s
        // while already searching forward repeats; while searching
        // backward it reverses direction instead (also real Emacs isearch
        // behavior) -- inputMode_ is kept in sync with search_'s own
        // direction since InIsearchMatch reads it to know which end of the
        // match point() is at.
        if (search_->Query().empty() && !lastSearchQuery_.empty()) {
            search_->AppendText(lastSearchQuery_);
        }
        else if (inputMode_ == InputMode::IsearchBackward) {
            search_->ReverseDirection();
            inputMode_ = InputMode::IsearchForward;
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (chord.Control && chord.Codepoint == U'r') {
        if (search_->Query().empty() && !lastSearchQuery_.empty()) {
            search_->AppendText(lastSearchQuery_);
        }
        else if (inputMode_ == InputMode::IsearchForward) {
            search_->ReverseDirection();
            inputMode_ = InputMode::IsearchBackward;
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (chord.Control && chord.Codepoint == U'w') {
        search_->AppendWordAtPoint();
    }
    else if (chord.Control && chord.Codepoint == U'y') {
        search_->AppendText(killRing_.Current());
    }
    else if (IsPlainCharacter(chord)) {
        search_->AppendChar(chord.Codepoint);
    }
    // Anything else (arrow keys, unrelated control combos) is ignored mid-search.

    statusMessage_ = SearchStatusText();
    ScrollToShowPoint();
}

void BufferView::HandleQueryReplaceKeyInner(const editor::KeyChord& chord) {
    const auto stage = queryReplace_->CurrentStage();

    if (stage == editor::QueryReplace::Stage::EnteringPattern || stage == editor::QueryReplace::Stage::EnteringReplacement) {
        if (chord.Special == editor::SpecialKey::Enter) {
            if (stage == editor::QueryReplace::Stage::EnteringPattern) {
                try {
                    queryReplace_->ConfirmPattern();
                }
                catch (const editor::RegexPatternError& e) {
                    ReportError(std::string("Invalid regex: ") + e.what());
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
    return;
}

void BufferView::HandleQueryReplaceKey(const editor::KeyChord& chord) {
    // in-file-regex follow-up: any stage that *searches* (ConfirmReplacement's
    // first find, every y/n/! step) can throw RegexPatternError if PCRE2's
    // match-limit safety net trips on a catastrophically backtracking
    // pattern -- retrying the same key would just trip it again, so end the
    // session with the error visible rather than crash or loop.
    try {
        HandleQueryReplaceKeyInner(chord);
    }
    catch (const editor::RegexPatternError& e) {
        queryReplace_->Cancel();
        ReportError(std::string("Query replace: ") + e.what());
        EndInteractiveSession();
        return;
    }

    if (queryReplace_->CurrentStage() == editor::QueryReplace::Stage::Done) {
        EndInteractiveSession();
        return;
    }

    ScrollToShowPoint();
}

std::string_view BufferView::HistoryKeyForInputMode(InputMode mode) {
    switch (mode) {
        case InputMode::FindFile:
            return "find-file";
        case InputMode::ProjectSearch:
            return "project-search";
        case InputMode::CreateDirectory:
            return "create-directory";
        case InputMode::FindScratch:
            return "find-scratch";
        case InputMode::GotoLine:
            return "goto-line";
        case InputMode::StringRectangle:
            return "string-rectangle";
        case InputMode::SetHeadlineTags:
            return "set-headline-tags";
        case InputMode::DeleteProperty:
            return "delete-property";
        case InputMode::OrgSchedule:
            return "org-schedule";
        case InputMode::OrgDeadline:
            return "org-deadline";
        case InputMode::LspRenameNewName:
            return "lsp-rename";
        case InputMode::TaskName:
            return "task-name";
        case InputMode::DapEvaluate:
            return "dap-evaluate";
        case InputMode::DapBreakpointCondition:
            return "dap-breakpoint-condition";
        case InputMode::DapBreakpointLogMessage:
            return "dap-breakpoint-log-message";
        case InputMode::DapAddWatch:
            return "dap-add-watch";
        case InputMode::DapSetVariableValue:
            return "dap-set-variable";
        case InputMode::DapBreakpointHitCondition:
            return "dap-breakpoint-hit-condition";
        case InputMode::DapFunctionBreakpointName:
            return "dap-function-breakpoint-name";
        case InputMode::DapMemoryByteCount:
            return "dap-memory-byte-count";
        case InputMode::VcsCreateBranch:
            return "vcs-create-branch";
        case InputMode::AcpPromptText:
            return "acp-prompt-text";
        case InputMode::BookmarkSetName:
            return "bookmark-set";
        case InputMode::OpenProjectPath:
            return "open-project-path";
        case InputMode::OpenProjectName:
            return "open-project-name";
        default:
            return "prompt"; // unreachable from HandlePromptKey's own dispatch guard; a safe shared fallback regardless
    }
}

bool BufferView::TryNavigatePromptHistory(const editor::KeyChord& chord, std::string_view key) {
    if (!chord.Meta || chord.Control) {
        return false;
    }
    if (chord.Codepoint != U'p' && chord.Codepoint != U'n') {
        return false;
    }

    // Deliberately never touches statusMessage_ itself, even at a history
    // boundary (no entries yet / already at the oldest or the live edit) --
    // every caller's own post-navigation refresh (plain StatusText() here,
    // a re-ranked fuzzy-candidate line in ExecuteCommand/ProjectFindFile)
    // runs unconditionally right after a true return, and there's no separate
    // echo-area slot to show a transient "no more history" note in without
    // that refresh immediately clobbering it -- so a boundary press is a
    // silent no-op instead, same as Backspace on an already-empty prompt.
    const std::vector<std::string>& entries = promptHistory_.Entries(key);

    if (chord.Codepoint == U'p') { // older
        if (promptHistoryIndex_ == kNoHistoryIndex) {
            if (entries.empty()) {
                return true;
            }
            promptHistoryStash_ = prompt_->Text();
            promptHistoryIndex_ = 0;
        }
        else if (promptHistoryIndex_ + 1 < entries.size()) {
            ++promptHistoryIndex_;
        }
        else {
            return true; // already at the oldest entry
        }
        prompt_->SetText(entries[promptHistoryIndex_]);
        return true;
    }

    // 'n', newer
    if (promptHistoryIndex_ == kNoHistoryIndex) {
        return true; // already at the live edit -- nothing to do
    }
    if (promptHistoryIndex_ > 0) {
        --promptHistoryIndex_;
        prompt_->SetText(entries[promptHistoryIndex_]);
    }
    else {
        prompt_->SetText(promptHistoryStash_);
        promptHistoryIndex_ = kNoHistoryIndex;
    }
    return true;
}

namespace {
    // Forward-declared here (defined below, near ShowDebugInfo) so
    // HandlePromptKey's DapSetVariableValue branch -- which runs earlier in
    // this file -- can reuse it rather than duplicating the "[ref:N]"/
    // "[owner:M]"-marker line format.
    std::string FormatDebugVariableLine(const ned::editor::dap::DapManager::Variable& variable, std::size_t indent, int ownerRef = 0);
} // namespace

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
                ReportError(e.what());
            }
        }
        else if (inputMode_ == InputMode::OpenProjectPath) {
            const std::filesystem::path root = editor::DetectProjectRoot(input);
            if (editor::FindProjectByRoot(root)) {
                // Already registered/named -- nothing to ask, activate directly.
                ActivateProjectAndReport(root);
            }
            else {
                // BookmarkSet's own pre-filled-second-prompt shape --
                // returns rather than falling through to this function's
                // shared EndInteractiveSession() tail below, since this
                // transitions to OpenProjectName instead of finishing.
                pendingOpenProjectRoot_ = root;
                inputMode_              = InputMode::OpenProjectName;
                prompt_.emplace("Project name: ");
                prompt_->SetText(root.filename().string());
                statusMessage_ = prompt_->StatusText();
                return;
            }
        }
        else if (inputMode_ == InputMode::OpenProjectName) {
            const std::string           name = input.empty() ? pendingOpenProjectRoot_.filename().string() : input;
            const std::filesystem::path root = pendingOpenProjectRoot_;
            pendingOpenProjectRoot_.clear();
            editor::RegisterProject(name, root);
            ActivateProjectAndReport(root);
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
                ReportError(e.what());
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
            catch (const editor::SearchPatternError& e) {
                ReportError(std::string("Invalid regex: ") + e.what());
            }
        }
        else if (inputMode_ == InputMode::StringRectangle) {
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.HasSecondaryCursors()) {
                editor::StringRectangle(buffer, input, editor::TabWidth());
            }
            else {
                // multi-cursor-round-2 follow-up: one shared, user-typed
                // replacement string applied to every cursor's own
                // rectangle -- no piece-distribution question here, unlike
                // kill/yank.
                buffer.ForEachCursor([&] {
                    if (buffer.HasMark()) {
                        editor::StringRectangle(buffer, input, editor::TabWidth());
                    }
                });
            }
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
        else if (inputMode_ == InputMode::DeleteProperty) {
            if (editor::org::DeletePropertyAtPoint(activeBuffer_.Get(), input)) {
                statusMessage_.clear();
            }
            else {
                statusMessage_ = "No such property.";
            }
        }
        else if (inputMode_ == InputMode::OrgSchedule || inputMode_ == InputMode::OrgDeadline) {
            // Re-resolved fresh, same reasoning SetHeadlineTags's own branch
            // above states.
            const bool isDeadline = (inputMode_ == InputMode::OrgDeadline);
            if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
                editor::org::Planning planning =
                    editor::org::ParsePlanning(activeBuffer_.Get().Text(), *headline).value_or(editor::org::Planning{});
                if (input.empty()) {
                    // Empty input clears this slot -- same "empty removes
                    // it" precedent SetHeadlineTags's own empty-tags case
                    // and SetProperty's own empty-value case establish.
                    (isDeadline ? planning.deadline : planning.scheduled) = std::nullopt;
                    editor::org::SetPlanning(activeBuffer_.Get(), *headline, planning);
                    statusMessage_.clear();
                }
                else {
                    const auto today = std::chrono::year_month_day{
                        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
                    if (const auto parsed = editor::org::ParseTimestampInput(input, today)) {
                        (isDeadline ? planning.deadline : planning.scheduled) = parsed;
                        editor::org::SetPlanning(activeBuffer_.Get(), *headline, planning);
                        statusMessage_.clear();
                    }
                    else {
                        statusMessage_ = "Unrecognized date -- try \"today\", \"+N\", or \"YYYY-MM-DD[ HH:MM]\".";
                    }
                }
            }
        }
        else if (inputMode_ == InputMode::LspRenameNewName) {
            // Fire-and-forget, same async shape as RequestCodeActionsAtPoint:
            // EndInteractiveSession() below runs immediately, the actual
            // rename applies later, from inside RequestRenameAtPoint's own
            // callback, once the response arrives -- no separate y/n
            // confirmation (worst case, undo).
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
        else if (inputMode_ == InputMode::AcpPromptText) {
            // Fire-and-forget, same async shape as DapEvaluate below: the
            // reply streams into the output buffer asynchronously via
            // AcpManager's own session/update handling, not through this
            // return value.
            statusMessage_ = acpManager_ ? acpManager_->SendPrompt(input) : "No ACP manager available.";
        }
        else if (inputMode_ == InputMode::DapEvaluate) {
            if (input.empty()) {
                statusMessage_.clear();
            }
            else if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                // Fire-and-forget, same async shape as LspRenameNewName
                // above: EndInteractiveSession() below runs immediately,
                // the result lands in statusMessage_ from the callback.
                statusMessage_ = "Evaluating...";
                dapManager_->Evaluate(input, [this, input](bool success, std::string text) {
                    statusMessage_ = success ? (input + " = " + text) : ("Evaluate failed: " + text);
                });
            }
        }
        else if (inputMode_ == InputMode::DapBreakpointCondition || inputMode_ == InputMode::DapBreakpointLogMessage ||
                 inputMode_ == InputMode::DapBreakpointHitCondition) {
            // Empty input is meaningful here (clears the field), unlike
            // DapEvaluate above -- no early-return on it.
            if (!dapManager_ || !pendingDapBreakpointTarget_) {
                statusMessage_ = "No debugger available.";
            }
            else if (inputMode_ == InputMode::DapBreakpointCondition) {
                statusMessage_ = dapManager_->SetBreakpointCondition(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
            }
            else if (inputMode_ == InputMode::DapBreakpointLogMessage) {
                statusMessage_ = dapManager_->SetBreakpointLogMessage(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
            }
            else {
                statusMessage_ = dapManager_->SetBreakpointHitCondition(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
            }
            pendingDapBreakpointTarget_.reset();
        }
        else if (inputMode_ == InputMode::DapFunctionBreakpointName) {
            if (input.empty()) {
                statusMessage_ = "No function name given.";
            }
            else if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                const bool nowSet = dapManager_->ToggleFunctionBreakpoint(input);
                statusMessage_    = (nowSet ? "Function breakpoint added: " : "Function breakpoint removed: ") + input;
            }
        }
        else if (inputMode_ == InputMode::DapAddWatch) {
            if (input.empty()) {
                statusMessage_ = "No expression given.";
            }
            else if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                dapManager_->AddWatch(input);
                statusMessage_ = "Watch added: " + input;
            }
        }
        else if (inputMode_ == InputMode::DapSetVariableValue) {
            if (!dapManager_ || !pendingDapSetVariable_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                text::Buffer* const bufferPtr = pendingDapSetVariable_->buffer;
                const std::size_t   line      = pendingDapSetVariable_->line;
                const std::string   lineText  = pendingDapSetVariable_->lineText;
                const int           ownerRef  = pendingDapSetVariable_->ownerRef;
                const std::string   name      = pendingDapSetVariable_->name;
                statusMessage_                = "Setting " + name + "...";
                dapManager_->SetVariable(
                    ownerRef, name, input,
                    [this, bufferPtr, line, lineText, name](editor::dap::DapManager::SetVariableResult result) {
                        if (bufferPtr != &activeBuffer_.Get()) {
                            return; // switched away while the request was in flight
                        }
                        if (!result.success) {
                            statusMessage_ = "Set variable failed: " + result.errorMessage;
                            return;
                        }
                        text::Buffer&     target        = *bufferPtr;
                        const text::ITextStorage& targetContent = target.Content();
                        if (line >= targetContent.LineCount()) {
                            return;
                        }
                        const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
                        const std::size_t targetLineEnd    = (line + 1 < targetContent.LineCount())
                                                                 ? targetContent.LineToByteOffset(line + 1) - 1
                                                                 : targetContent.ByteLength();
                        if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
                            statusMessage_ = "Debug line changed -- variable set on the adapter, but not re-displayed.";
                            return;
                        }
                        // Rebuild this single line the same way FormatDebugVariableLine
                        // would, preserving indent and the owner marker (whose ref
                        // hasn't changed) but reflecting the possibly-new value/type/
                        // ref -- same programmatic-splice-under-a-lifted-read-only-flag
                        // pattern ExpandVariableAtPoint uses.
                        std::size_t indent = 0;
                        while (indent < lineText.size() && lineText[indent] == ' ') {
                            ++indent;
                        }
                        editor::dap::DapManager::Variable variable;
                        variable.name               = name;
                        variable.value               = result.value;
                        variable.type                = result.type;
                        variable.variablesReference = result.variablesReference;
                        std::string replacement      = FormatDebugVariableLine(variable, indent);
                        const std::size_t ownerMarker = lineText.rfind("[owner:");
                        if (ownerMarker != std::string::npos) {
                            replacement += "  " + lineText.substr(ownerMarker);
                        }
                        const bool wasReadOnly = target.ReadOnly();
                        target.SetReadOnly(false);
                        target.DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
                        target.InsertAt(targetLineStart, replacement);
                        target.SetReadOnly(wasReadOnly);
                        statusMessage_.clear();
                    });
            }
            pendingDapSetVariable_.reset();
        }
        else if (inputMode_ == InputMode::DapMemoryByteCount) {
            if (!dapManager_ || !pendingDapMemoryReference_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                std::size_t count = 128; // DAP round 5's default -- enough for most pointer/array previews
                if (!input.empty()) {
                    try {
                        const long parsed = std::stol(input);
                        if (parsed > 0) {
                            count = static_cast<std::size_t>(parsed);
                        }
                    }
                    catch (const std::exception&) {
                        // Keep the default -- an unparsable count isn't worth failing the request over.
                    }
                }
                const std::string memoryReference = *pendingDapMemoryReference_;
                statusMessage_                    = "Fetching memory...";
                dapManager_->RequestMemory(memoryReference, 0, count,
                                           [this, memoryReference](bool success, editor::dap::DapManager::MemoryBlock block) {
                                               if (!success) {
                                                   statusMessage_ = "Read memory failed (adapter may not support readMemory).";
                                                   return;
                                               }
                                               BuildMemoryBuffer(memoryReference, block);
                                           });
            }
            pendingDapMemoryReference_.reset();
        }
        else if (inputMode_ == InputMode::VcsCreateBranch) {
            if (input.empty()) {
                statusMessage_ = "No branch name given.";
            }
            else if (!vcsRunner_) {
                statusMessage_ = "no vcs runner configured";
            }
            else {
                statusMessage_ = "Creating branch " + input + "...";
                // A branch switch rewrites the working tree underneath any
                // open buffer. Unmodified buffers catch up on the next
                // auto-revert tick (external-modification-safety follow-up,
                // Editor/AutoRevert.h -- this message predates it and used
                // to say "not reloaded"); a *modified* buffer is still left
                // alone, and its save will hit the supersession y/n rather
                // than a confusing stale-content overwrite.
                vcsRunner_->RequestBranchCreate(
                    input,
                    [this, input] {
                        statusMessage_ = "Created and switched to " + input + " (modified buffers not reloaded)";
                        RefreshVcsStatusBuffer();
                        RequestDiffForCurrentBuffer();
                    },
                    [this](std::string error) { statusMessage_ = "vcs branch: " + error; });
            }
        }
        else if (inputMode_ == InputMode::GotoLine) {
            std::size_t parsed   = 0;
            bool        allDigit = !input.empty();
            for (const char ch : input) {
                if (ch < '0' || ch > '9') {
                    allDigit = false;
                    break;
                }
                parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
            }
            if (!allDigit) {
                statusMessage_ = "Not a line number: \"" + input + "\"";
            }
            else {
                // 1-based like Emacs' own goto-line; out-of-range clamps to
                // the last line rather than erroring.
                text::Buffer&     buffer = activeBuffer_.Get();
                const std::size_t target = std::min(std::max<std::size_t>(parsed, 1), buffer.Content().LineCount()) - 1;
                PushJumpMark();
                buffer.SetPoint(buffer.Content().LineToByteOffset(target));
                statusMessage_.clear();
            }
        }
        else if (inputMode_ == InputMode::BookmarkSetName) {
            if (input.empty()) {
                statusMessage_ = "Bookmark name cannot be empty";
            }
            else {
                editor::RecordBookmark(input, activeBuffer_.Get(), static_cast<std::size_t>(editor::TabWidth()));
                editor::SaveBookmarks();
                statusMessage_ = "Bookmark set: " + input;
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
                    ReportError(e.what());
                }
            }
        }

        promptHistory_.Record(HistoryKeyForInputMode(inputMode_), input);
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
            case InputMode::ProjectSearch:
                label = "Project search";
                break;
            case InputMode::CreateDirectory:
                label = "Create directory";
                break;
            case InputMode::FindScratch:
                label = "Find scratch";
                break;
            case InputMode::GotoLine:
                label = "Goto line";
                break;
            case InputMode::StringRectangle:
                label = "String rectangle";
                break;
            case InputMode::SetHeadlineTags:
                label = "Set headline tags";
                break;
            case InputMode::DeleteProperty:
                label = "Delete property";
                break;
            case InputMode::OrgSchedule:
                label = "Schedule";
                break;
            case InputMode::OrgDeadline:
                label = "Deadline";
                break;
            case InputMode::LspRenameNewName:
                label = "Rename";
                break;
            case InputMode::TaskName:
                label = (taskPromptAction_ == TaskPromptAction::Run) ? "Run task" : "Cancel task";
                break;
            case InputMode::DapEvaluate:
                label = "Evaluate";
                break;
            case InputMode::DapBreakpointCondition:
                label = "Breakpoint condition";
                pendingDapBreakpointTarget_.reset();
                break;
            case InputMode::DapBreakpointLogMessage:
                label = "Breakpoint log message";
                pendingDapBreakpointTarget_.reset();
                break;
            case InputMode::DapAddWatch:
                label = "Add watch";
                break;
            case InputMode::DapSetVariableValue:
                label = "Set variable";
                pendingDapSetVariable_.reset();
                break;
            case InputMode::DapBreakpointHitCondition:
                label = "Breakpoint hit condition";
                pendingDapBreakpointTarget_.reset();
                break;
            case InputMode::DapFunctionBreakpointName:
                label = "Function breakpoint name";
                break;
            case InputMode::DapMemoryByteCount:
                label = "Memory byte count";
                pendingDapMemoryReference_.reset();
                break;
            case InputMode::VcsCreateBranch:
                label = "Create branch";
                break;
            case InputMode::AcpPromptText:
                label = "Send ACP prompt";
                break;
            case InputMode::BookmarkSetName:
                label = "Bookmark name";
                break;
            case InputMode::OpenProjectPath:
                label = "Open project";
                break;
            case InputMode::OpenProjectName:
                label = "Project name";
                pendingOpenProjectRoot_.clear();
                break;
            default:
                label = "Prompt";
                break;
        }
        statusMessage_ = label + " cancelled.";
        EndInteractiveSession();
        return;
    }
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath ||
        inputMode_ == InputMode::FindScratch) {
        // dropdown-path-completion follow-up: Up/Down move the live popup's
        // highlight (prompt history stays on M-p/M-n, TryNavigatePromptHistory
        // below -- never plain Up/Down, so there's no conflict); Tab accepts
        // the highlighted candidate's full accumulated value (not the masked
        // display text) into the prompt rather than the old common-prefix-
        // expand-and-list-in-the-echo-area behavior -- a directory candidate's
        // trailing '/' means the very next refresh re-lists that directory's
        // own contents, giving a descend-by-Tab feel. Enter/Quit are handled
        // by the generic chains above/below, unchanged -- this mode still
        // finalizes on literal prompt_->Text(), not the popup selection.
        if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
            const std::vector<std::string> candidates = GatherPathCompletionCandidates();
            if (!candidates.empty()) {
                pathCompletionSelection_ = chord.Special == editor::SpecialKey::Down
                                               ? (pathCompletionSelection_ + 1) % candidates.size()
                                               : (pathCompletionSelection_ + candidates.size() - 1) % candidates.size();
            }
            RefreshPathCompletionPopup();
            return;
        }
        if (chord.Special == editor::SpecialKey::Tab) {
            const std::vector<std::string> candidates = GatherPathCompletionCandidates();
            if (!candidates.empty()) {
                prompt_->SetText(candidates[std::min(pathCompletionSelection_, candidates.size() - 1)]);
                pathCompletionSelection_ = 0;
            }
            RefreshPathCompletionPopup();
            return;
        }
    }
    if (chord.Special == editor::SpecialKey::Tab && inputMode_ != InputMode::ProjectSearch &&
        inputMode_ != InputMode::CreateDirectory && inputMode_ != InputMode::StringRectangle &&
        inputMode_ != InputMode::SetHeadlineTags && inputMode_ != InputMode::LspRenameNewName &&
        inputMode_ != InputMode::TaskName && inputMode_ != InputMode::DapEvaluate &&
        inputMode_ != InputMode::VcsCreateBranch && inputMode_ != InputMode::AcpPromptText &&
        inputMode_ != InputMode::DeleteProperty &&
        inputMode_ != InputMode::OrgSchedule && inputMode_ != InputMode::OrgDeadline &&
        inputMode_ != InputMode::DapBreakpointCondition && inputMode_ != InputMode::DapBreakpointLogMessage &&
        inputMode_ != InputMode::DapAddWatch && inputMode_ != InputMode::DapSetVariableValue &&
        inputMode_ != InputMode::DapBreakpointHitCondition && inputMode_ != InputMode::DapFunctionBreakpointName &&
        inputMode_ != InputMode::DapMemoryByteCount &&
        inputMode_ != InputMode::GotoLine &&
        // dropdown-path-completion follow-up: Tab is fully handled by the
        // dedicated block above for these three (accept-highlighted, never
        // reaching this common-prefix/echo-area-list path anymore).
        inputMode_ != InputMode::FindFile && inputMode_ != InputMode::OpenProjectPath &&
        inputMode_ != InputMode::FindScratch) { // completing a line number is meaningless
        // DapEvaluate excluded too: completing a debuggee expression
        // against buffer names would be meaningless, same reasoning as
        // ProjectSearch's regex pattern. VcsCreateBranch likewise
        // (deliberately *new* free text). AcpPromptText stays free-text --
        // it's a message to the agent, not a name. OrgSchedule/OrgDeadline: a
        // typed date/relative-shorthand has no candidate list either.
        // against. DeleteProperty likewise -- completing a property name
        // against file/buffer names would be meaningless. The four new DAP
        // round-2 prompts (condition/log-message/watch expression/variable
        // value) are all free-text against debuggee state, same reasoning.
        CompletePrompt();
        return;
    }

    if (TryNavigatePromptHistory(chord, HistoryKeyForInputMode(inputMode_))) {
        statusMessage_ = prompt_->StatusText();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_ = kNoHistoryIndex; // editing exits history browsing -- see TryNavigatePromptHistory's own doc comment
        if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath ||
            inputMode_ == InputMode::FindScratch) {
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        }
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.

    statusMessage_ = prompt_->StatusText();
}

BufferView::PromptEditOutcome BufferView::HandlePromptEditingKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteBackward();
        return PromptEditOutcome::TextEdited;
    }
    if (chord.Special == editor::SpecialKey::Delete) {
        prompt_->DeleteForward();
        return PromptEditOutcome::TextEdited;
    }
    if (chord.Special == editor::SpecialKey::Left) {
        prompt_->MoveCursorLeft();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::Right) {
        prompt_->MoveCursorRight();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::Home) {
        prompt_->MoveCursorToStart();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::End) {
        prompt_->MoveCursorToEnd();
        return PromptEditOutcome::CursorMoved;
    }
    if (IsPlainCharacter(chord)) {
        prompt_->InsertChar(chord.Codepoint);
        return PromptEditOutcome::TextEdited;
    }
    return PromptEditOutcome::NotHandled;
}

// dropdown-path-completion follow-up: the candidate source
// RefreshPathCompletionPopup/the Up-Down-Tab handling in HandlePromptKey
// below all share -- FindFile/OpenProjectPath/FindScratch's own directory-
// or name-prefix-filtered lists, unchanged from what CompletePrompt used to
// gather inline before Tab was the only trigger.
std::vector<std::string> BufferView::GatherPathCompletionCandidates() const {
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath) {
        return text::CompleteFilePath(prompt_->Text());
    }
    if (inputMode_ == InputMode::FindScratch) {
        return editor::CompleteScratchNames(prompt_->Text());
    }
    return {};
}

// dropdown-path-completion follow-up: FindFile/OpenProjectPath/FindScratch's
// own live dropdown -- unlike the selection-based Handle<Mode>Key sessions
// (switch-project/switch-to-buffer/vcs-switch-branch/acp-agent-name), Enter
// still finalizes on literal prompt_->Text() in HandlePromptKey, unchanged,
// since typing a path/name with no match is a valid "create new" action
// here (a new file, a new scratch pad, a new project root). This popup is a
// visual + Tab-to-accept aid only, called on session start and on every
// keystroke/Up/Down (see HandlePromptKey's own call sites).
void BufferView::RefreshPathCompletionPopup() {
    const std::vector<std::string> candidates = GatherPathCompletionCandidates();
    pathCompletionSelection_ = candidates.empty() ? 0 : std::min(pathCompletionSelection_, candidates.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (!onCandidatesChanged_) {
        return;
    }
    if (candidates.empty()) {
        onCandidatesChanged_(std::nullopt);
        return;
    }
    const bool isPathMode = inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath;
    const std::function<std::string(const std::string&)> display =
        isPathMode ? std::function<std::string(const std::string&)>(MaskPathCandidateToLastSegment)
                   : std::function<std::string(const std::string&)>{};
    onCandidatesChanged_(
        BuildFuzzyCandidatePopupModel(prompt_->StatusText(), candidates, pathCompletionSelection_, display));
}

void BufferView::CompletePrompt() {
    // dropdown-path-completion follow-up: FindFile/OpenProjectPath/FindScratch
    // (path/name modes that still need to finalize on literal typed text --
    // see RefreshPathCompletionPopup's own doc comment) now get a live popup
    // instead of Tab reaching this function; VcsSwitchBranch/AcpAgentName/
    // SwitchToBuffer are fully dedicated Handle<Mode>Key sessions, intercepted
    // before this function is ever reached. Everything left below is the
    // original catch-all default, still used by whichever other prompt modes
    // reach Tab without their own candidate source (e.g. OpenProjectName,
    // BookmarkSetName).
    std::vector<std::string> candidates = text::CompleteBufferNames(bufferList_, prompt_->Text());

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
    VisitResultUnderPoint();
}

void BufferView::JumpToPathLine(const std::filesystem::path& path, std::size_t line) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBuffer_.Set(opened);
        opened.SetPoint(opened.ByteOffsetForLineAndColumn(line - 1, 0)); // 1-indexed -> 0-indexed
        statusMessage_.clear();
        ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::VisitVcsResult() {
    VisitResultUnderPoint();
}

void BufferView::VisitResultUnderPoint() {
    const text::Buffer& buffer = activeBuffer_.Get();

    // Multibuffers follow-up: a *vcs diff*/*diagnostics*/*references* buffer
    // carries a MultibufferIndex mapping the whole composite byte space back
    // to (source path, source line) -- this works from any line inside an
    // excerpt's body, not just a single "path:line:" index line the
    // regex-based fallback below requires, so it takes over entirely once a
    // buffer is one of these (never falls through to the regex path for the
    // same buffer).
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(buffer)) {
        if (const editor::multibuffer::ExcerptSpan* span = index->SpanAtOffset(buffer.Point());
            span && span->sourceStartLine > 0) {
            JumpToPathLine(span->sourcePath, span->sourceStartLine);
        }
        return;
    }

    const text::ITextStorage& content   = buffer.Content();
    const std::size_t point     = buffer.Point();
    const std::size_t line      = content.ByteOffsetToLine(point);
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // Matches every flat "path:line:" results-buffer format written here --
    // project-search/project-replace/agenda's HandlePromptKey/BuildResultsBuffer
    // path and BuildVcsBlameBuffer's own format both write this shape.
    // Greedy .* correctly handles the rare case of a ':' inside the path
    // itself, by backing off to find the *last* plausible ":<digits>:" split.
    // A *vcs log* buffer's lines never match this (no per-line source
    // location), so this is correctly a silent no-op there too.
    static const std::regex resultLinePattern(R"(^(.*):(\d+):)");

    std::smatch match;
    if (!std::regex_search(lineText, match, resultLinePattern)) {
        return; // not a results-shaped line -- silent no-op, see this method's own header comment
    }

    JumpToPathLine(match[1].str(), std::stoul(match[2].str()));
}

void BufferView::RequestVcsBlameBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer* buffer = &activeBuffer_.Get();
    if (!buffer->Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    const std::filesystem::path path = *buffer->Path();
    vcsRunner_->RequestBlame(
        *buffer,
        [this, buffer, path](std::vector<editor::vcs::VcsBlameLine> lines) {
            if (&activeBuffer_.Get() == buffer) {
                // Populates the gutter for the still-active source buffer
                // before BuildVcsBlameBuffer switches activeBuffer_ away
                // from it -- see DispatchBlameForTesting's own doc comment.
                DispatchBlameForTesting(lines);
            }
            BuildVcsBlameBuffer(path, lines);
        },
        [this](std::string error) { statusMessage_ = "vcs blame: " + error; });
}

void BufferView::RequestVcsLogBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    const std::filesystem::path path = *buffer.Path();
    vcsRunner_->RequestLog(
        buffer, [this, path](std::vector<editor::vcs::VcsLogEntry> entries) { BuildVcsLogBuffer(path, entries); },
        [this](std::string error) { statusMessage_ = "vcs log: " + error; });
}

void BufferView::BuildVcsBlameBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsBlameLine>& lines) {
    std::string resultsText;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const editor::vcs::VcsBlameLine& line      = lines[i];
        const std::string                shortHash = line.commitHash.substr(0, std::min<std::size_t>(8, line.commitHash.size()));
        resultsText += path.string() + ":" + std::to_string(i + 1) + ": " + shortHash + " " + line.author + " " + line.date +
                       " | " + line.summary + "\n";
    }

    const std::string bufferName = "*vcs blame " + path.filename().string() + "*";
    text::Buffer&      results   = bufferList_.CreateBuffer(bufferName);
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    results.SetReadOnly(true); // see BuildResultsBuffer's own doc comment for why
    editor::SetLastResultsBuffer(bufferName);
    activeBuffer_.Set(results);
}

void BufferView::BuildVcsLogBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsLogEntry>& entries) {
    std::string resultsText;
    for (const editor::vcs::VcsLogEntry& entry : entries) {
        const std::string shortHash = entry.commitHash.substr(0, std::min<std::size_t>(8, entry.commitHash.size()));
        resultsText += shortHash + " " + entry.date + " " + entry.author + ": " + entry.summary + "\n";
    }

    text::Buffer& results = bufferList_.CreateBuffer("*vcs log " + path.filename().string() + "*");
    results.InsertAtPoint(resultsText);
    results.SetPoint(0);
    results.SetReadOnly(true);
    activeBuffer_.Set(results);
}

namespace {

    // Right-aligns number into a fixed kDiffLineNumberWidth-wide field, or a
    // blank field of the same width if number is unset (the line doesn't
    // exist on that side of the diff) -- widens rather than truncates for a
    // number too large to fit, never lossy.
    constexpr int kDiffLineNumberWidth = 4;

    std::string FormatDiffLineNumber(std::optional<std::size_t> number) {
        if (!number) {
            return std::string(kDiffLineNumberWidth, ' ');
        }
        std::string text = std::to_string(*number);
        return text.size() >= static_cast<std::size_t>(kDiffLineNumberWidth) ? text
                                                                             : std::string(kDiffLineNumberWidth - text.size(), ' ') + text;
    }

    // Reformats one hunk's raw +/-/context body (git's own leading-marker-
    // per-line convention) into "old new marker content" rows -- old/new
    // line numbers side by side (blank on whichever side a line doesn't
    // exist), so a change reads without cross-referencing a separate
    // gutter. Appends one LineTint per emitted row to tints, in order,
    // consumed by BuildMultibuffer's own line-tint zip (ExcerptSource::
    // lineTints). A line this doesn't recognize (git's "\ No newline at end
    // of file" marker, or anything unexpected) passes through with blank
    // numbers and no tint rather than being guessed at.
    std::string FormatDiffHunkBody(const editor::vcs::DiffHunkText& hunk, std::vector<editor::multibuffer::LineTint>& tints) {
        std::string formatted;
        std::size_t oldLine = hunk.oldStart;
        std::size_t newLine = hunk.newStart;
        std::size_t pos     = 0;
        const auto& body    = hunk.bodyText;
        while (pos < body.size()) {
            const std::size_t      eol     = body.find('\n', pos);
            const std::size_t      lineEnd = (eol == std::string::npos) ? body.size() : eol;
            const std::string_view line(body.data() + pos, lineEnd - pos);

            std::optional<std::size_t>    oldNum;
            std::optional<std::size_t>    newNum;
            char                          marker  = ' ';
            editor::multibuffer::LineTint tint    = editor::multibuffer::LineTint::None;
            std::string_view              content = line;

            if (line.starts_with('+')) {
                newNum  = newLine++;
                marker  = '+';
                tint    = editor::multibuffer::LineTint::Added;
                content = line.substr(1);
            }
            else if (line.starts_with('-')) {
                oldNum  = oldLine++;
                marker  = '-';
                tint    = editor::multibuffer::LineTint::Removed;
                content = line.substr(1);
            }
            else if (line.starts_with(' ')) {
                oldNum  = oldLine++;
                newNum  = newLine++;
                content = line.substr(1);
            }

            formatted += FormatDiffLineNumber(oldNum);
            formatted += ' ';
            formatted += FormatDiffLineNumber(newNum);
            formatted += ' ';
            formatted += marker;
            formatted += ' ';
            formatted.append(content);
            formatted += '\n';
            tints.push_back(tint);

            pos = (eol == std::string::npos) ? body.size() : eol + 1;
        }
        return formatted;
    }

} // namespace

void BufferView::RequestVcsFullDiffBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::filesystem::path root = editor::ProjectRoot();
    vcsRunner_->RequestFullDiff(
        [this, root](std::string rawDiff) {
            const std::vector<editor::vcs::DiffHunkText> hunks = editor::vcs::ParseDiffHunks(rawDiff);

            std::vector<editor::multibuffer::ExcerptSource> excerpts;
            excerpts.reserve(hunks.size());
            for (const editor::vcs::DiffHunkText& hunk : hunks) {
                std::vector<editor::multibuffer::LineTint> tints;
                std::string                                formattedBody = FormatDiffHunkBody(hunk, tints);

                // newCount == 0 is a pure deletion (see DiffPatch.h's own
                // doc comment on Covers) -- there's no real new-side line to
                // jump to, so this excerpt's header is still shown but
                // sourceStartLine stays 0 ("no single source line applies",
                // ExcerptSource's own documented convention).
                const std::size_t sourceLine = hunk.newCount > 0 ? hunk.newStart : 0;
                excerpts.push_back(editor::multibuffer::ExcerptSource{
                    root / hunk.filePath, sourceLine, sourceLine + (hunk.newCount > 0 ? hunk.newCount - 1 : 0),
                    "▸ " + hunk.filePath + "  " + hunk.hunkHeader, // U+25B8 -- ProjectSidebar's own disclosure triangle
                    std::move(formattedBody), std::move(tints)});
            }

            text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*vcs diff*", excerpts);
            editor::SetLastResultsBuffer("*vcs diff*");
            activeBuffer_.Set(results);
            statusMessage_ = excerpts.empty() ? "Working tree clean." : std::to_string(excerpts.size()) + " changed hunk" + (excerpts.size() == 1 ? "" : "s");
        },
        [this](std::string error) { statusMessage_ = "vcs full diff: " + error; });
}

void BufferView::RequestDiagnosticsBuffer() {
    // One entry per Code-origin diagnostic across every open, path-backed
    // buffer -- prose/spell-check diagnostics (Editor/Lsp/LspManager.h's
    // kProseLanguageKey) stay out, matching the recent split that moved
    // them out of the code-diagnostic gutter entirely (they get their own
    // review flow, not this one). source is a raw pointer into
    // bufferList_'s own storage, valid for this whole synchronous function
    // (no callback/await in between, unlike RequestVcsFullDiffBuffer).
    struct PendingDiagnostic {
        text::Buffer*            source;
        text::Buffer::Diagnostic diagnostic;
        std::size_t              sourceLineStart;
    };
    std::vector<PendingDiagnostic> pending;
    for (const auto& bufferPtr : bufferList_.Buffers()) {
        text::Buffer& buffer = *bufferPtr;
        if (!buffer.Path() || buffer.Diagnostics().empty() || editor::multibuffer::MultibufferIndexFor(buffer)) {
            continue;
        }
        const text::ITextStorage& content = buffer.Content();
        for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
            if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
                continue;
            }
            const std::size_t line = content.ByteOffsetToLine(std::min(diagnostic.startByte, content.ByteLength()));
            pending.push_back(PendingDiagnostic{&buffer, diagnostic, content.LineToByteOffset(line)});
        }
    }

    // Grouped per file, top-to-bottom within a file -- a scannable
    // "problems list," not whatever order each language server happened to
    // report diagnostics in.
    std::sort(pending.begin(), pending.end(), [](const PendingDiagnostic& a, const PendingDiagnostic& b) {
        const std::filesystem::path& pathA = *a.source->Path();
        const std::filesystem::path& pathB = *b.source->Path();
        return pathA != pathB ? pathA < pathB : a.diagnostic.startByte < b.diagnostic.startByte;
    });

    // One excerpt per diagnostic -- its own single source line, verbatim
    // (not a multi-line context window: the header already names the exact
    // file/line, and this keeps the composite-offset translation below a
    // fixed "past the header" arithmetic instead of needing real line
    // math against the composite buffer).
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(pending.size());
    for (const PendingDiagnostic& item : pending) {
        const text::ITextStorage& content = item.source->Content();
        const std::size_t line    = content.ByteOffsetToLine(item.sourceLineStart);
        const std::size_t lineEnd = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        // U+25B8, ProjectSidebar's own disclosure triangle -- same header
        // glyph vcs-full-diff-buffer's excerpts use, for visual consistency
        // between the two multibuffer consumers.
        std::string header = "▸ " + item.source->Path()->string() + ":" + std::to_string(line + 1);
        std::string body   = content.Substring(item.sourceLineStart, lineEnd - item.sourceLineStart);
        excerpts.push_back(editor::multibuffer::ExcerptSource{
            *item.source->Path(), line + 1, line + 1, std::move(header), std::move(body), {}, /*editable=*/true});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*diagnostics*", excerpts);
    editor::SetLastResultsBuffer("*diagnostics*");
    activeBuffer_.Set(results);
    statusMessage_ =
        excerpts.empty() ? "No diagnostics." : std::to_string(excerpts.size()) + " diagnostic" + (excerpts.size() == 1 ? "" : "s");

    // Re-attaches each original diagnostic's real severity/message onto the
    // composite buffer, translated into composite byte space, instead of a
    // new LineTint -- see this method's own doc comment in BufferView.h for
    // why: it makes the ordinary diagnostic gutter glyph/underline/
    // severity-color/inline-annotation pipeline (Theme::diagnosticError et
    // al., all driven off Buffer::Diagnostics()) light up unmodified. Every
    // excerpt above is exactly one header line + one body line, so the
    // body's own start byte is a fixed offset (header length + its
    // newline) past the excerpt's span start. ExcerptSpan order matches
    // excerpts' own order 1:1 here: BuildMultibuffer appends excerpts
    // sequentially so composite bytes only increase, and
    // MultibufferIndex::SetSpans's sort-by-compositeStartByte is a no-op on
    // an already-increasing sequence.
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(results)) {
        const std::vector<editor::multibuffer::ExcerptSpan>& spans = index->Spans();
        std::vector<text::Buffer::Diagnostic>                composited;
        composited.reserve(pending.size());
        for (std::size_t i = 0; i < pending.size() && i < spans.size(); ++i) {
            const editor::multibuffer::ExcerptSpan& span      = spans[i];
            const std::size_t                       bodyLen   = excerpts[i].bodyText.size();
            const std::size_t                       bodyStart = span.compositeStartByte + excerpts[i].headerText.size() + 1;

            const text::Buffer::Diagnostic& original   = pending[i].diagnostic;
            std::size_t                     startDelta = original.startByte > pending[i].sourceLineStart ? original.startByte - pending[i].sourceLineStart : 0;
            startDelta                                 = std::min(startDelta, bodyLen);
            std::size_t endDelta                       = original.endByte > pending[i].sourceLineStart ? original.endByte - pending[i].sourceLineStart : startDelta;
            endDelta                                   = std::min(endDelta, bodyLen);
            if (endDelta <= startDelta) {
                endDelta = std::min(bodyLen, startDelta + 1); // widen a zero-length span, same as the inline-diagnostic underline pass
            }

            text::Buffer::Diagnostic translated = original;
            translated.startByte                = bodyStart + startDelta;
            translated.endByte                  = bodyStart + endDelta;
            composited.push_back(translated);
        }
        results.SetDiagnostics(std::move(composited));
    }
}

void BufferView::ShowMessagesBuffer() {
    editor::RebuildMessagesBuffer(bufferList_);
    text::Buffer* messages = bufferList_.Find(std::string(editor::MessagesBufferName()));
    if (!messages) {
        return; // unreachable -- RebuildMessagesBuffer always finds-or-creates it
    }
    activeBuffer_.Set(*messages);
    statusMessage_.clear();
}

void BufferView::BuildAgendaMultibuffer() {
    const std::vector<editor::AgendaItem> items = editor::CollectAgendaItems(editor::ProjectRoot());

    auto sectionLabel = [](editor::AgendaSection section) -> const char* {
        switch (section) {
            case editor::AgendaSection::Overdue:
                return "Overdue";
            case editor::AgendaSection::Today:
                return "Today";
            case editor::AgendaSection::Upcoming:
                return "Upcoming";
            case editor::AgendaSection::Undated:
                return "Undated";
        }
        return "";
    };

    // One excerpt per agenda item, its own single (synthesized) body line --
    // same "header names the file/line, body is the content" shape
    // RequestDiagnosticsBuffer's own excerpts use just above. The section
    // label lives in the header text itself (items already arrive grouped
    // by section, CollectAgendaItems' own sort order) rather than a
    // separate divider excerpt -- BuildMultibuffer's own rule line between
    // excerpts already gives every entry a visible top edge.
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(items.size());
    for (const editor::AgendaItem& item : items) {
        const std::size_t line = item.headline.lineNumber + 1; // 1-indexed, matching every other multibuffer consumer
        std::string       header =
            "▸ [" + std::string(sectionLabel(item.section)) + "] " + item.file.string() + ":" + std::to_string(line);
        std::string body = editor::FormatAgendaItemSummary(item);
        excerpts.push_back(editor::multibuffer::ExcerptSource{item.file, line, line, std::move(header), std::move(body), {}});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*agenda*", excerpts);
    editor::SetLastResultsBuffer("*agenda*");
    activeBuffer_.Set(results);
    statusMessage_ = excerpts.empty() ? "No active TODOs." : std::to_string(excerpts.size()) + " agenda item" + (excerpts.size() == 1 ? "" : "s");
}

namespace {

    // org-clock-display follow-up: "H:MM", same unpadded-hour/zero-padded-
    // minute shape Org.cpp's own (file-local) FormatClockEntry uses for a
    // closed clock line's own "=>  H:MM" -- kept as a small local copy here
    // rather than exposed from Org.h, since nothing outside that one clock-
    // line-formatting call and this multibuffer excerpt formatting needs it.
    std::string FormatClockMinutes(std::chrono::minutes minutes) {
        const long long    count = minutes.count();
        std::ostringstream out;
        out << (count / 60) << ':' << std::setfill('0') << std::setw(2) << (count % 60);
        return out.str();
    }

    // Recursively walks tree (BuildHeadlineTree's own shape), appending one
    // excerpt per headline whose own-or-subtree clocked total is nonzero --
    // a headline with no clocked time of its own and no clocked descendant
    // contributes nothing, keeping the report to only what's actually
    // relevant. File order (BuildHeadlineTree's own child order, which is
    // ParseOutline's file order) rather than sorted by duration -- matches
    // the buffer's own outline structure, which a duration sort would
    // scramble.
    void CollectClockedHeadlines(std::string_view bufferText, const editor::org::HeadlineNode& node,
                                 const std::filesystem::path& sourcePath, std::vector<editor::multibuffer::ExcerptSource>& excerpts) {
        if (node.headline) {
            const std::chrono::minutes own     = editor::org::TotalClockedMinutes(bufferText, *node.headline);
            const std::chrono::minutes subtree = editor::org::TotalClockedMinutesForSubtree(bufferText, node);
            if (own.count() > 0 || subtree.count() > 0) {
                const std::size_t line   = node.headline->lineNumber + 1; // 1-indexed, matching every other multibuffer consumer
                std::string       header = "▸ " + node.headline->title + "  " + sourcePath.string() + ":" + std::to_string(line);
                std::string       body   = "own " + FormatClockMinutes(own) + "   subtree " + FormatClockMinutes(subtree);
                excerpts.push_back(editor::multibuffer::ExcerptSource{sourcePath, line, line, std::move(header), std::move(body), {}});
            }
        }
        for (const editor::org::HeadlineNode& child : node.children) {
            CollectClockedHeadlines(bufferText, child, sourcePath, excerpts);
        }
    }

} // namespace

void BufferView::BuildClockReportMultibuffer() {
    text::Buffer&                                buffer     = activeBuffer_.Get();
    const std::string                            bufferText = buffer.Text();
    const std::vector<editor::org::Headline>     headlines  = editor::org::ParseOutline(bufferText, editor::org::TodoKeywords());
    const std::vector<editor::org::HeadlineNode> tree       = editor::org::BuildHeadlineTree(headlines);
    // sourcePath is empty for a not-yet-saved buffer -- jump-to-source is
    // then a silent no-op on any excerpt, the same "no source, no jump"
    // posture every other multibuffer consumer here already has.
    const std::filesystem::path sourcePath = buffer.Path().value_or(std::filesystem::path{});

    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    for (const editor::org::HeadlineNode& root : tree) {
        CollectClockedHeadlines(bufferText, root, sourcePath, excerpts);
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*clock report*", excerpts);
    editor::SetLastResultsBuffer("*clock report*");
    activeBuffer_.Set(results);
    statusMessage_ = excerpts.empty() ? "No clocked time in this buffer."
                                      : std::to_string(excerpts.size()) + " clocked headline" + (excerpts.size() == 1 ? "" : "s");
}

namespace {

    // find-all-references follow-up: [start, end) of the ASCII word/
    // identifier point sits inside or immediately after, or nullopt when
    // point touches no word at all. Same classification Commands.cpp's own
    // (anonymous-namespace-private) WordRegionAt uses for select-next-
    // occurrence/select-all-occurrences -- duplicated here rather than
    // shared, the same "not worth a new seam for something this small" call
    // that function's own doc comment already makes, and the one
    // WordPrefixStart above makes too (that one only scans backward, for a
    // completion prefix -- this needs the full symmetric span).
    std::optional<std::pair<std::size_t, std::size_t>> WordRegionAtPoint(const text::ITextStorage& content, std::size_t point) {
        const auto isWordChar = [](char32_t codepoint) {
            return (codepoint < 0x80) && (std::isalnum(static_cast<unsigned char>(codepoint)) != 0 || codepoint == U'_');
        };

        std::size_t start = std::min(point, content.ByteLength());
        while (start > 0) {
            const std::size_t previous = content.PreviousCodepointBoundary(start);
            if (!isWordChar(content.CodepointAt(previous).codepoint)) {
                break;
            }
            start = previous;
        }
        std::size_t end = std::min(point, content.ByteLength());
        while (end < content.ByteLength()) {
            const auto decoded = content.CodepointAt(end);
            if (!isWordChar(decoded.codepoint)) {
                break;
            }
            end += decoded.byteLength;
        }
        if (start == end) {
            return std::nullopt;
        }
        return std::pair{start, end};
    }

    // find-references follow-up: reads just one 1-indexed line out of path,
    // for building an excerpt from an LSP ResolvedLocation -- unlike
    // ProjectSearch's own SearchOneFile, which already has every line in
    // hand while it's matching, a resolved reference names only a
    // path+position, and the target file need not be an open Buffer (most
    // references live in files the user never opened). Empty string on any
    // failure (unreadable path, line past EOF) rather than throwing --
    // BuildReferencesMultibuffer degrades to a blank excerpt body instead of
    // dropping the whole result.
    std::string ReadFileLine(const std::filesystem::path& path, std::size_t lineNumber) {
        std::ifstream file(path);
        if (!file || lineNumber == 0) {
            return {};
        }
        std::string line;
        for (std::size_t i = 0; i < lineNumber; ++i) {
            if (!std::getline(file, line)) {
                return {};
            }
        }
        return line;
    }

} // namespace

void BufferView::RequestProjectFindReferences() {
    text::Buffer&     buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();

    const std::optional<std::pair<std::size_t, std::size_t>> wordRegion = WordRegionAtPoint(content, buffer.Point());
    if (!wordRegion) {
        statusMessage_ = "No identifier at point.";
        return;
    }
    const std::string word = content.Substring(wordRegion->first, wordRegion->second - wordRegion->first);

    // find-references follow-up: prefer a real semantic answer from the
    // language server when one is actually running for this buffer's
    // language -- the same "is one currently usable" StatusForLanguage
    // check RequestCompletionAtPoint's own dabbrev-fallback already uses,
    // not a guess. Falls through to the plain-text scan below when no
    // server is running (nothing configured, still spawning, crashed, ...),
    // the same "LSP is a nice-to-have accelerant, not the only path"
    // precedent SwitchHeaderSource already established -- unlike
    // lsp-goto-definition/-declaration/etc., which refuse outright with no
    // LSP manager, this command already has a working universal fallback
    // worth keeping.
    const std::size_t point         = buffer.Point();
    const std::string serverKey     = ResolvedLspServerKey(point);
    const std::string languageKey   = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const bool        hasRunningLsp = lspManager_ && lspManager_->StatusForLanguage(languageKey) == editor::lsp::LspManager::LspStatus::Running;

    if (hasRunningLsp) {
        text::Buffer* const bufferPtr  = &buffer;
        const std::size_t   generation = ++referencesRequestGeneration_;
        statusMessage_                 = "Requesting references...";
        lspManager_->RequestReferences(
            buffer, point,
            [this, bufferPtr, point, generation, word](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
                if (generation != referencesRequestGeneration_) {
                    return; // superseded by a newer request
                }
                if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                    return; // buffer/point changed since the request was sent
                }
                if (locations.empty()) {
                    statusMessage_ = "No references to \"" + word + "\" found.";
                    return;
                }

                const std::filesystem::path                     root = editor::ProjectRoot();
                std::vector<editor::multibuffer::ExcerptSource> excerpts;
                excerpts.reserve(locations.size());
                for (const editor::lsp::LspManager::ResolvedLocation& location : locations) {
                    const std::size_t           lineNumber = location.position.line + 1; // LSP is 0-indexed, excerpts/SearchMatch are 1-indexed
                    std::error_code             ec;
                    const std::filesystem::path relative    = std::filesystem::relative(location.path, root, ec);
                    const std::string           displayPath = (!ec && !relative.empty()) ? relative.string() : location.path.string();
                    excerpts.push_back(editor::multibuffer::ExcerptSource{
                        location.path, lineNumber, lineNumber,
                        "▸ " + displayPath + ":" + std::to_string(lineNumber), // same disclosure-triangle convention as the text-search path below
                        ReadFileLine(location.path, lineNumber),
                        {},
                        /*editable=*/true});
                }

                text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*references: " + word + "*", excerpts);
                editor::SetLastResultsBuffer("*references: " + word + "*");
                activeBuffer_.Set(results);
                statusMessage_ = std::to_string(locations.size()) + " reference" + (locations.size() == 1 ? "" : "s") + " to \"" +
                                 word + "\" -- C-c v v to visit";
            },
            serverKey);
        return;
    }

    std::vector<editor::SearchMatch> matches;
    try {
        // "\bword\b" -- safe to embed the word unescaped: WordRegionAtPoint
        // only ever admits [A-Za-z0-9_], none of them RE2 metacharacters.
        matches = editor::SearchDirectory(editor::ProjectRoot(), "\\b" + word + "\\b");
    }
    catch (const editor::SearchPatternError& e) {
        ReportError(std::string("Invalid regex: ") + e.what());
        return;
    }

    if (matches.empty()) {
        statusMessage_ = "No references to \"" + word + "\" found.";
        return;
    }

    const std::filesystem::path                     root = editor::ProjectRoot();
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(matches.size());
    for (const editor::SearchMatch& match : matches) {
        std::error_code             ec;
        const std::filesystem::path relative    = std::filesystem::relative(match.file, root, ec);
        const std::string           displayPath = (!ec && !relative.empty()) ? relative.string() : match.file.string();

        excerpts.push_back(editor::multibuffer::ExcerptSource{
            match.file, match.lineNumber, match.lineNumber,
            "▸ " + displayPath + ":" + std::to_string(match.lineNumber), // U+25B8, same disclosure triangle vcs-full-diff-buffer's headers use
            match.lineText,
            {},
            /*editable=*/true});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*references: " + word + "*", excerpts);
    editor::SetLastResultsBuffer("*references: " + word + "*");
    activeBuffer_.Set(results);
    statusMessage_ = std::to_string(matches.size()) + " reference" + (matches.size() == 1 ? "" : "s") + " to \"" + word +
                     "\" -- C-c v v to visit";
}

namespace {
    // Root-scoped singletons, unlike the per-file "*vcs blame <name>*"/
    // "*vcs log <name>*" buffers -- see BuildVcsStatusBuffer's own header
    // doc comment for why these are found-and-refilled rather than
    // re-created.
    constexpr const char* kVcsStatusBufferName   = "*vcs status*";
    constexpr const char* kVcsBranchesBufferName = "*vcs branches*";

    // The same find-or-create + refill-in-place shape ExpandVariableAtPoint's
    // read-only-lift splice established, for a whole buffer: point survives
    // (clamped/snapped by SetPoint itself) so a stage-at-point refresh
    // doesn't yank the cursor back to the top of the list.
    text::Buffer& RefillSingletonBuffer(text::BufferList& bufferList, const char* name, const std::string& text) {
        text::Buffer* buffer = bufferList.Find(name);
        if (!buffer) {
            buffer = &bufferList.CreateBuffer(name);
        }
        const std::size_t oldPoint = buffer->Point();
        buffer->SetReadOnly(false);
        if (buffer->Content().ByteLength() > 0) {
            buffer->DeleteRange(0, buffer->Content().ByteLength());
        }
        if (!text.empty()) {
            buffer->InsertAt(0, text);
        }
        buffer->SetPoint(oldPoint);
        buffer->SetReadOnly(true);
        return *buffer;
    }
} // namespace

void BufferView::RequestVcsStatusBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestStatus(
        [this](std::vector<editor::vcs::VcsStatusEntry> entries) { BuildVcsStatusBuffer(entries, /*announce=*/true); },
        [this](std::string error) { statusMessage_ = "vcs status: " + error; });
}

void BufferView::BuildVcsStatusBuffer(const std::vector<editor::vcs::VcsStatusEntry>& entries, bool announce) {
    const std::filesystem::path root = editor::ProjectRoot();

    std::string text;
    for (const editor::vcs::VcsStatusEntry& entry : entries) {
        text += (root / entry.path).string() + ":1: " + entry.state + " " + entry.path + "\n";
    }

    text::Buffer& status = RefillSingletonBuffer(bufferList_, kVcsStatusBufferName, text);
    if (announce) {
        editor::SetLastResultsBuffer(kVcsStatusBufferName);
        activeBuffer_.Set(status);
        statusMessage_ = entries.empty()
                             ? "Working tree clean."
                             : std::to_string(entries.size()) + " changed file" + (entries.size() == 1 ? "" : "s") +
                                   " -- C-c v a stages, C-c v u unstages, C-c v v visits";
    }
}

void BufferView::RefreshVcsStatusBuffer() {
    if (!vcsRunner_ || !bufferList_.Find(kVcsStatusBufferName)) {
        return;
    }
    vcsRunner_->RequestStatus(
        [this](std::vector<editor::vcs::VcsStatusEntry> entries) { BuildVcsStatusBuffer(entries, /*announce=*/false); });
}

void BufferView::BeginVcsCommitMessage() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::filesystem::path path = editor::vcs::VcsCommitMessagePath();
    // FindByPath, not the OpenOrCreateFile call below, decides whether this
    // is a genuinely fresh commit (seed the template) or the user re-running
    // vcs-commit while one is already mid-composition (switch to it as-is,
    // preserving whatever they've already typed) -- OpenOrCreateFile itself
    // always returns *some* buffer either way.
    const bool    alreadyOpen  = bufferList_.FindByPath(path) != nullptr;
    text::Buffer& commitBuffer = bufferList_.OpenOrCreateFile(path);
    if (!alreadyOpen) {
        commitBuffer.InsertAtPoint(editor::vcs::kVcsCommitMessageTemplate);
        commitBuffer.SetPoint(0);
    }
    activeBuffer_.Set(commitBuffer);
}

void BufferView::FinishVcsCommitMessage() {
    text::Buffer&     commitBuffer = activeBuffer_.Get();
    const std::string message      = editor::vcs::ExtractCommitMessage(commitBuffer.Text());
    CloseVcsCommitMessageBuffer(commitBuffer);
    if (message.empty()) {
        statusMessage_ = "Empty commit message -- not committing.";
    }
    else if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
    }
    else {
        // Fire-and-forget, DapEvaluate's shape: the buffer's already closed
        // by the time this fires, the summary lands in statusMessage_ from
        // the callback.
        statusMessage_ = "Committing...";
        vcsRunner_->RequestCommit(
            message,
            [this](std::string summary) {
                statusMessage_ = summary.empty() ? "Committed." : summary;
                RefreshVcsStatusBuffer();
                // The comparison point (HEAD for git) just moved, so the
                // current buffer's markers are stale now.
                RequestDiffForCurrentBuffer();
            },
            [this](std::string error) { statusMessage_ = "vcs commit: " + error; });
    }
}

void BufferView::AbortVcsCommitMessage() {
    CloseVcsCommitMessageBuffer(activeBuffer_.Get());
    statusMessage_ = "Commit aborted.";
}

void BufferView::CloseVcsCommitMessageBuffer(text::Buffer& commitBuffer) {
    CloseBufferNow(commitBuffer);
    std::error_code ec;
    std::filesystem::remove(editor::vcs::VcsCommitMessagePath(), ec); // best-effort -- a leftover temp file is harmless
}

std::optional<std::filesystem::path> BufferView::ResolveVcsFileTarget() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.Name() == kVcsStatusBufferName) {
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t line      = content.ByteOffsetToLine(buffer.Point());
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

        // BuildVcsStatusBuffer's own "<absolute path>:1: ..." shape -- the
        // same pattern VisitVcsResult parses, reused for the same reason.
        static const std::regex resultLinePattern(R"(^(.*):(\d+):)");
        std::smatch             match;
        if (std::regex_search(lineText, match, resultLinePattern)) {
            return std::filesystem::path(match[1].str());
        }
        return std::nullopt; // an empty/foreign line in the status buffer
    }
    if (buffer.Path()) {
        return *buffer.Path();
    }
    return std::nullopt;
}

void BufferView::StageOrUnstageFileAtPoint(bool stage) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::optional<std::filesystem::path> target = ResolveVcsFileTarget();
    if (!target) {
        statusMessage_ = "no file to " + std::string(stage ? "stage" : "unstage") + " here";
        return;
    }

    auto onSuccess = [this, stage, target = *target] {
        statusMessage_ = (stage ? "Staged " : "Unstaged ") + target.filename().string();
        RefreshVcsStatusBuffer();
        // Staging moves a file's changes into the index, which the bundled
        // git plugin's worktree-vs-index diff then stops reporting --
        // refresh so the gutter agrees (and the reverse for unstaging).
        RequestDiffForCurrentBuffer();
    };
    auto onError = [this, stage](std::string error) {
        statusMessage_ = std::string("vcs ") + (stage ? "stage" : "unstage") + ": " + error;
    };
    if (stage) {
        vcsRunner_->RequestStage(*target, std::move(onSuccess), std::move(onError));
    }
    else {
        vcsRunner_->RequestUnstage(*target, std::move(onSuccess), std::move(onError));
    }
}

void BufferView::StageOrUnstageHunkAtPoint(bool stage) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "no file associated with this buffer";
        return;
    }
    if (buffer.Modified()) {
        // See this method's header doc comment -- unsaved edits make the
        // buffer's line numbers disagree with the on-disk diff's.
        statusMessage_ = "Buffer has unsaved changes -- save first, hunk staging works from the file on disk.";
        return;
    }

    const std::size_t targetLine = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-indexed, diff's own convention
    vcsRunner_->RequestHunkApply(
        buffer, targetLine, stage,
        [this, stage] {
            statusMessage_ = stage ? "Hunk staged." : "Hunk unstaged.";
            RefreshVcsStatusBuffer();
            RequestDiffForCurrentBuffer();
        },
        [this, stage](std::string error) {
            statusMessage_ = std::string("vcs ") + (stage ? "stage" : "unstage") + " hunk: " + error;
        });
}

void BufferView::StageHunkAtPointForTesting(bool stage) {
    StageOrUnstageHunkAtPoint(stage);
}

void BufferView::JumpToNextHunkForTesting() {
    JumpToNextHunk();
}

void BufferView::JumpToPreviousHunkForTesting() {
    JumpToPreviousHunk();
}

void BufferView::NextErrorForTesting() {
    NextError();
}

void BufferView::PreviousErrorForTesting() {
    PreviousError();
}

void BufferView::RequestDiagnosticsBufferForTesting() {
    RequestDiagnosticsBuffer();
}

void BufferView::RequestProjectFindReferencesForTesting() {
    RequestProjectFindReferences();
}

void BufferView::RequestHierarchyAtPointForTesting(HierarchyDirection direction) {
    RequestHierarchyAtPoint(direction);
}

void BufferView::BeginVcsCommitMessageForTesting() {
    BeginVcsCommitMessage();
}

void BufferView::FinishVcsCommitMessageForTesting() {
    FinishVcsCommitMessage();
}

void BufferView::AbortVcsCommitMessageForTesting() {
    AbortVcsCommitMessage();
}

void BufferView::RequestVcsBranchesBuffer() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestBranchList(
        [this](std::vector<editor::vcs::VcsBranchEntry> entries) { BuildVcsBranchesBuffer(entries); },
        [this](std::string error) { statusMessage_ = "vcs branches: " + error; });
}

void BufferView::BuildVcsBranchesBuffer(const std::vector<editor::vcs::VcsBranchEntry>& entries) {
    std::string text;
    for (const editor::vcs::VcsBranchEntry& entry : entries) {
        text += (entry.current ? "* " : "  ") + entry.name + "\n";
    }

    text::Buffer& branches = RefillSingletonBuffer(bufferList_, kVcsBranchesBufferName, text);
    activeBuffer_.Set(branches);
    statusMessage_ = entries.empty() ? "No branches." : "M-x vcs-switch-branch switches; C-c v n creates.";
}

void BufferView::DispatchStatusForTesting(std::vector<editor::vcs::VcsStatusEntry> entries) {
    BuildVcsStatusBuffer(entries, /*announce=*/true);
}

void BufferView::DispatchBranchesForTesting(std::vector<editor::vcs::VcsBranchEntry> entries) {
    BuildVcsBranchesBuffer(entries);
}

std::optional<std::filesystem::path> BufferView::ResolveVcsFileTargetForTesting() {
    return ResolveVcsFileTarget();
}

void BufferView::BeginVcsSwitchBranchPrompt() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    statusMessage_ = "Fetching branches...";
    vcsRunner_->RequestBranchList(
        [this](std::vector<editor::vcs::VcsBranchEntry> entries) {
            if (inputMode_ != InputMode::Normal) {
                // Another prompt began while the fetch was in flight --
                // don't hijack it (same in-progress guard RequestCloseBuffer
                // applies to its own confirmation).
                return;
            }
            vcsBranchCandidates_.clear();
            for (const editor::vcs::VcsBranchEntry& entry : entries) {
                if (!entry.current) {
                    vcsBranchCandidates_.push_back(entry.name);
                }
            }
            inputMode_ = InputMode::VcsSwitchBranch;
            prompt_.emplace("Switch to branch: ");
            vcsSwitchBranchSelection_ = 0;
            RefreshVcsSwitchBranchStatus();
        },
        [this](std::string error) { statusMessage_ = "vcs branch: " + error; });
}

// VCS side panel follow-up: pulled out of the VcsCreateBranch switch case
// verbatim so VcsPanel's own 'n' key (via RequestVcsAction) can start the
// exact same prompt InteractiveRequest::VcsCreateBranch already does.
void BufferView::BeginVcsCreateBranchPrompt() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    inputMode_ = InputMode::VcsCreateBranch;
    prompt_.emplace("New branch: ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::OpenLinkAtPoint() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (mode_.name == "org-mode") {
        if (const auto orgLink = editor::org::LinkAtPoint(buffer)) {
            if (!orgLink->target.empty() && (orgLink->target.front() == '*' || orgLink->target.front() == '#')) {
                // Real Org's own internal-link forms: "[[*Some Headline]]"
                // (matched against a headline's title) and "[[#custom-id]]"
                // (matched against a headline's own :CUSTOM_ID: property,
                // property-drawers follow-up).
                const bool        isCustomId = orgLink->target.front() == '#';
                const std::string ref        = orgLink->target.substr(1);
                const auto        lineStartByte =
                    isCustomId ? editor::org::FindHeadlineByCustomId(buffer.Text(), ref)
                               : editor::org::FindHeadlineByTitle(buffer.Text(), ref);
                if (lineStartByte) {
                    buffer.ClearMark();
                    buffer.SetPoint(*lineStartByte);
                    statusMessage_.clear();
                    ScrollToShowPoint();
                }
                else {
                    statusMessage_ = "Link target not found: " + ref;
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

    // import-target-tree-sitter follow-up: a mode with an import query
    // configured (Mode::importTarget) gets first refusal, the same
    // "mode-specific first, generic fallback" chain the org-mode block above
    // already established -- generalized across every language uniformly
    // (including a future dynamically-loaded one) since this branches on
    // whether the function is set, never on mode_.name.
    if (mode_.importTarget) {
        if (const auto imported = mode_.importTarget(buffer.Text(), buffer.Point())) {
            std::string target = imported->target;
            if (imported->isModulePath) {
                std::replace(target.begin(), target.end(), '.', '/');
            }
            OpenDetectedLink(editor::link::DetectedLink{
                .kind      = editor::link::LinkKind::File,
                .target    = std::move(target),
                .startByte = imported->startByte,
                .endByte   = imported->endByte,
            });
            return;
        }
        // No import at point under this mode's own query -- fall through to
        // the generic path below (e.g. a URL or bare file path elsewhere on
        // the same line).
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
    const editor::ProjectSettings projectSettings = editor::LoadProjectSettings(editor::ProjectRoot());

    // toolchain-include-paths follow-up: project-configured includePaths
    // always come first (a user override outranks a guessed default), with
    // the real compiler's own system search paths appended as a last-resort
    // fallback for an angle-form/system include ProjectSettings never
    // mentioned at all.
    const std::string                        languageKey    = editor::LanguageKeyForMode(mode_);
    std::vector<std::filesystem::path>       includePaths   = editor::IncludePathsForMode(projectSettings, mode_.name);
    const std::vector<std::filesystem::path> toolchainPaths = editor::ToolchainIncludePathsForLanguage(languageKey);
    includePaths.insert(includePaths.end(), toolchainPaths.begin(), toolchainPaths.end());

    // import-target-tree-sitter follow-up: per-language extension/index-file/
    // package-dir parameters (Editor/ImportResolutionConfig.h) widen what
    // ResolveFileLink can find beyond an exact on-disk match -- a relative
    // JS/TS import written without its real extension, a Python package's
    // __init__.py, a bare "import x from 'lodash'" package specifier.
    const editor::ImportResolutionConfig importConfig =
        editor::ResolveImportResolutionConfig(projectSettings, languageKey);
    if (importConfig.searchPackageDirs) {
        const std::vector<std::filesystem::path> packageDirs =
            editor::NodeModulesSearchPaths(baseDirectory, editor::ProjectRoot());
        includePaths.insert(includePaths.end(), packageDirs.begin(), packageDirs.end());
    }

    const auto resolved = editor::link::ResolveFileLink(detected.target, baseDirectory, includePaths,
                                                        importConfig.extensions, importConfig.indexBasenames);
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
        ReportError(e.what());
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
    editor::SetLastResultsBuffer(name);
    activeBuffer_.Set(results);
}

namespace {

    // One *debug* buffer variable line:
    // "  name: type = value  [ref:N] [owner:M] [mem:<ref>]" -- "[ref:N]" only
    // when the variable is composite (children fetchable via a variables
    // request, ExpandVariableAtPoint's own marker), "[owner:M]" (round 2)
    // whenever ownerRef is given: M is the variablesReference of the
    // *container* (scope or parent composite) the variables request that
    // produced this line was made against -- what SetVariableAtPoint's
    // setVariable request needs, independent of and always present alongside
    // an optional [ref:N]. "[mem:<ref>]" (DAP round 5) whenever the adapter
    // sent a memoryReference for this variable -- ShowMemoryAtPoint's own
    // target marker, an opaque string rather than a small int like the other
    // two (see DapManager::Variable::memoryReference's own doc comment).
    std::string FormatDebugVariableLine(const ned::editor::dap::DapManager::Variable& variable, std::size_t indent, int ownerRef) {
        std::string line(indent, ' ');
        line += variable.name;
        if (!variable.type.empty()) {
            line += ": " + variable.type;
        }
        line += " = " + variable.value;
        if (variable.variablesReference > 0) {
            line += "  [ref:" + std::to_string(variable.variablesReference) + "]";
        }
        if (ownerRef > 0) {
            line += "  [owner:" + std::to_string(ownerRef) + "]";
        }
        if (!variable.memoryReference.empty()) {
            line += "  [mem:" + variable.memoryReference + "]";
        }
        return line;
    }

} // namespace

void BufferView::ShowDebugInfo() {
    statusMessage_ = "Fetching debug info...";
    dapManager_->RequestStackTrace([this](std::vector<editor::dap::DapManager::StackFrame> frames) {
        if (frames.empty()) {
            statusMessage_ = "No stack to show (is the session stopped?).";
            return;
        }
        auto lines = std::make_shared<std::vector<std::string>>();
        lines->push_back("== Stack ==");
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const editor::dap::DapManager::StackFrame& frame = frames[i];
            // DAP round 4: "[frame:N]" is dap-restart-frame's own target
            // marker, RestartFrameAtPoint's counterpart to
            // FormatDebugVariableLine's "[ref:N]"/"[owner:M]".
            const std::string frameMarker = "  [frame:" + std::to_string(frame.id) + "]";
            if (frame.path) {
                // The established "path:line: text" results convention, so
                // C-c C-v (project-search-visit-result) jumps to a frame
                // with zero new navigation plumbing.
                lines->push_back(frame.path->string() + ":" + std::to_string(frame.line) + ": #" + std::to_string(i) +
                                 " " + frame.name + frameMarker);
            }
            else {
                lines->push_back("#" + std::to_string(i) + " " + frame.name + " (no source)" + frameMarker);
            }
        }
        dapManager_->RequestScopes(frames[0].id, [this, lines](std::vector<editor::dap::DapManager::Scope> scopes) {
            const std::vector<std::string>& watches = dapManager_->Watches();
            if (scopes.empty() && watches.empty()) {
                BuildDebugBuffer(*lines);
                return;
            }
            // One variables request per scope plus one evaluate per watch,
            // all in flight at once -- chunks keep each section's own
            // output in a fixed slot regardless of response interleaving,
            // and every callback runs on the main thread (see DapClient.h),
            // so a plain shared counter covering both fan-outs is
            // race-free. Slot 0 is reserved for watches (built even when
            // empty -- skipped below), slots [1, 1+scopes.size()) for scopes.
            auto remaining = std::make_shared<std::size_t>(scopes.size() + watches.size());
            auto chunks    = std::make_shared<std::vector<std::vector<std::string>>>(1 + scopes.size());
            if (!watches.empty()) {
                std::vector<std::string>& watchChunk = (*chunks)[0];
                watchChunk.push_back("== Watches ==");
                watchChunk.resize(1 + watches.size()); // one line per watch, filled in place by index below
                for (std::size_t w = 0; w < watches.size(); ++w) {
                    dapManager_->Evaluate(
                        watches[w],
                        [this, lines, remaining, chunks, w, expression = watches[w]](bool success, std::string text) {
                            (*chunks)[0][1 + w] = "  " + expression + " = " + (success ? text : ("<" + text + ">")) + "  [watch:" +
                                                   std::to_string(w) + "]";
                            if (--*remaining == 0) {
                                for (const std::vector<std::string>& finishedChunk : *chunks) {
                                    lines->insert(lines->end(), finishedChunk.begin(), finishedChunk.end());
                                }
                                BuildDebugBuffer(*lines);
                            }
                        },
                        "watch");
                }
            }
            // scopes.size() == 0 here just means the loop below never runs
            // and *remaining reaches 0 from the watch fan-out above alone.
            for (std::size_t s = 0; s < scopes.size(); ++s) {
                dapManager_->RequestVariables(
                    scopes[s].variablesReference,
                    [this, lines, remaining, chunks, s, scopeVariablesReference = scopes[s].variablesReference,
                     scopeName = scopes[s].name](std::vector<editor::dap::DapManager::Variable> variables) {
                        std::vector<std::string>& chunk = (*chunks)[1 + s];
                        chunk.push_back("");
                        chunk.push_back("== " + scopeName + " ==");
                        for (const editor::dap::DapManager::Variable& variable : variables) {
                            chunk.push_back(FormatDebugVariableLine(variable, 2, scopeVariablesReference));
                        }
                        if (--*remaining == 0) {
                            for (const std::vector<std::string>& finishedChunk : *chunks) {
                                lines->insert(lines->end(), finishedChunk.begin(), finishedChunk.end());
                            }
                            BuildDebugBuffer(*lines);
                        }
                    });
            }
        });
    });
}

void BufferView::BuildDebugBuffer(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    text::Buffer& debug = bufferList_.CreateBuffer("*debug*");
    debug.InsertAtPoint(text);
    debug.SetPoint(0);
    debug.SetReadOnly(true); // same tossable-read-only reasoning as BuildResultsBuffer
    activeBuffer_.Set(debug);
    statusMessage_ = "C-c C-v visits a frame; dap-expand-variable/dap-set-variable/dap-remove-watch act on point's own line.";
}

void BufferView::ExpandVariableAtPoint() {
    text::Buffer&     buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[ref:");
    int               reference = 0;
    if (markerPos != std::string::npos) {
        try {
            reference = std::stoi(lineText.substr(markerPos + 5)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            reference = 0;
        }
    }
    if (reference <= 0) {
        statusMessage_ = "No expandable variable on this line.";
        return;
    }

    std::size_t indent = 0;
    while (indent < lineText.size() && lineText[indent] == ' ') {
        ++indent;
    }

    text::Buffer* const bufferPtr = &buffer;
    statusMessage_                = "Expanding...";
    dapManager_->RequestVariables(
        reference,
        [this, bufferPtr, line, lineText, markerPos, indent, reference](std::vector<editor::dap::DapManager::Variable> variables) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // switched away while the request was in flight
            }
            text::Buffer&     target        = *bufferPtr;
            const text::ITextStorage& targetContent = target.Content();
            if (line >= targetContent.LineCount()) {
                return;
            }
            // Staleness guard, same spirit as RequestRenameAtPoint's own:
            // only splice into the exact line the request was made from.
            const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
            const std::size_t targetLineEnd   = (line + 1 < targetContent.LineCount())
                                                    ? targetContent.LineToByteOffset(line + 1) - 1
                                                    : targetContent.ByteLength();
            if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
                statusMessage_ = "Debug line changed -- not expanding.";
                return;
            }
            if (variables.empty()) {
                statusMessage_ = "No children (or the session already resumed).";
                return;
            }

            // Consume the "[ref:N]" marker (and its separating spaces) so a
            // second expand on the same line can't splice duplicates in --
            // but keep a trailing "[owner:M]" (round 2), if this line had
            // one, so the parent variable itself stays editable via
            // dap-set-variable even after being expanded.
            const std::size_t ownerMarkerPos = lineText.rfind("[owner:");
            const std::string ownerSuffix    = ownerMarkerPos != std::string::npos ? "  " + lineText.substr(ownerMarkerPos) : "";
            std::string       replacement    = lineText.substr(0, markerPos);
            while (!replacement.empty() && replacement.back() == ' ') {
                replacement.pop_back();
            }
            replacement += ownerSuffix;
            for (const editor::dap::DapManager::Variable& variable : variables) {
                replacement += "\n" + FormatDebugVariableLine(variable, indent + 2, reference);
            }

            // The *debug* buffer is read-only against user edits; this is a
            // programmatic splice, so the flag is lifted just around it --
            // same pragmatism as Buffer::AppendWhileReadOnly, which only
            // covers appends and can't do a mid-buffer splice.
            const bool wasReadOnly = target.ReadOnly();
            target.SetReadOnly(false);
            target.DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
            target.InsertAt(targetLineStart, replacement);
            target.SetReadOnly(wasReadOnly);
            statusMessage_.clear();
        });
}

void BufferView::RestartFrameAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[frame:");
    int               frameId   = 0;
    if (markerPos != std::string::npos) {
        try {
            frameId = std::stoi(lineText.substr(markerPos + 7)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            frameId = 0;
        }
    }
    if (markerPos == std::string::npos) {
        statusMessage_ = "Not a stack frame line.";
        return;
    }
    statusMessage_ = dapManager_->RestartFrame(frameId);
}

void BufferView::ShowDisassemblyAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    // "[frame:N]" is optional here (unlike RestartFrameAtPoint, which
    // refuses without one) -- 0 means "no preference," falling back to the
    // top stopped frame below.
    const std::size_t markerPos        = lineText.rfind("[frame:");
    int               requestedFrameId = 0;
    if (markerPos != std::string::npos) {
        try {
            requestedFrameId = std::stoi(lineText.substr(markerPos + 7)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            requestedFrameId = 0;
        }
    }

    statusMessage_ = "Fetching instructions...";
    dapManager_->RequestStackTrace([this, requestedFrameId](std::vector<editor::dap::DapManager::StackFrame> frames) {
        if (frames.empty()) {
            statusMessage_ = "No stack to disassemble (is the session stopped?).";
            return;
        }
        const editor::dap::DapManager::StackFrame* target = &frames[0];
        if (requestedFrameId != 0) {
            for (const editor::dap::DapManager::StackFrame& frame : frames) {
                if (frame.id == requestedFrameId) {
                    target = &frame;
                    break;
                }
            }
        }
        if (target->instructionPointerReference.empty()) {
            statusMessage_ = "No instruction pointer for this frame (adapter didn't report one).";
            return;
        }
        const std::string pcAddress = target->instructionPointerReference;
        // A fixed, generous window centered on the PC -- ShowDebugInfo's own
        // "one shot, re-invoke to refresh" model; no incremental paging.
        dapManager_->RequestDisassembly(
            pcAddress, -32, 64,
            [this, pcAddress](std::vector<editor::dap::DapManager::DisassembledInstruction> instructions) {
                if (instructions.empty()) {
                    statusMessage_ = "No instructions returned (adapter may not support disassembly).";
                    return;
                }
                BuildDisassemblyBuffer(instructions, pcAddress);
            });
    });
}

void BufferView::BuildDisassemblyBuffer(const std::vector<editor::dap::DapManager::DisassembledInstruction>& instructions,
                                        const std::string&                                                   pcAddress) {
    std::string text;
    for (const editor::dap::DapManager::DisassembledInstruction& instruction : instructions) {
        std::string line;
        if (instruction.path) {
            // The established "path:line: text" results convention, so
            // C-c C-v (project-search-visit-result) jumps to a located
            // instruction's source line with zero new navigation plumbing.
            line += instruction.path->string() + ":" + std::to_string(instruction.line) + ": ";
        }
        line += (instruction.address == pcAddress) ? "-> " : "   ";
        line += instruction.address;
        if (!instruction.instructionBytes.empty()) {
            line += "  " + instruction.instructionBytes;
        }
        line += "  " + instruction.instruction;
        text += line + "\n";
    }
    text::Buffer& disassembly = bufferList_.CreateBuffer("*disassembly*");
    disassembly.InsertAtPoint(text);
    disassembly.SetPoint(0);
    disassembly.SetReadOnly(true); // same tossable-read-only reasoning as BuildDebugBuffer
    activeBuffer_.Set(disassembly);
    statusMessage_ = "C-c C-v visits a located instruction's source line.";
}

void BufferView::RemoveWatchAtPoint() {
    text::Buffer&      buffer    = activeBuffer_.Get();
    const text::ITextStorage&  content   = buffer.Content();
    const std::size_t  line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t  lineStart = content.LineToByteOffset(line);
    const std::size_t  lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[watch:");
    if (markerPos == std::string::npos) {
        statusMessage_ = "No watch on this line.";
        return;
    }
    std::size_t index = 0;
    try {
        index = static_cast<std::size_t>(std::stoul(lineText.substr(markerPos + 7))); // stoul stops at the closing ']'
    }
    catch (const std::exception&) {
        statusMessage_ = "No watch on this line.";
        return;
    }
    dapManager_->RemoveWatchAt(index);
    ShowDebugInfo();
}

void BufferView::SetVariableAtPoint() {
    text::Buffer&      buffer    = activeBuffer_.Get();
    const text::ITextStorage&  content   = buffer.Content();
    const std::size_t  line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t  lineStart = content.LineToByteOffset(line);
    const std::size_t  lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t ownerMarkerPos = lineText.rfind("[owner:");
    int                ownerRef      = 0;
    if (ownerMarkerPos != std::string::npos) {
        try {
            ownerRef = std::stoi(lineText.substr(ownerMarkerPos + 7)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            ownerRef = 0;
        }
    }
    if (ownerRef <= 0) {
        statusMessage_ = "Not an editable variable line.";
        return;
    }

    std::size_t indent = 0;
    while (indent < lineText.size() && lineText[indent] == ' ') {
        ++indent;
    }
    const std::size_t colonPos = lineText.find(": ", indent);
    const std::size_t eqPos    = lineText.find(" = ", indent);
    std::size_t       nameEnd  = std::string::npos;
    if (colonPos != std::string::npos && (eqPos == std::string::npos || colonPos < eqPos)) {
        nameEnd = colonPos;
    }
    else {
        nameEnd = eqPos;
    }
    if (nameEnd == std::string::npos || nameEnd <= indent) {
        statusMessage_ = "Not an editable variable line.";
        return;
    }
    const std::string name = lineText.substr(indent, nameEnd - indent);

    pendingDapSetVariable_ = PendingDapSetVariable{
        .buffer = &buffer, .line = line, .lineText = lineText, .ownerRef = ownerRef, .name = name};
    inputMode_ = InputMode::DapSetVariableValue;
    prompt_.emplace("New value for " + name + ": ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::ShowMemoryAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         line      = content.ByteOffsetToLine(buffer.Point());
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    const std::string lineText = content.Substring(lineStart, lineEnd - lineStart);

    const std::size_t markerPos = lineText.rfind("[mem:");
    const std::size_t closePos  = (markerPos == std::string::npos) ? std::string::npos : lineText.find(']', markerPos + 5);
    if (markerPos == std::string::npos || closePos == std::string::npos) {
        statusMessage_ = "No memory reference on this line.";
        return;
    }

    pendingDapMemoryReference_ = lineText.substr(markerPos + 5, closePos - (markerPos + 5));
    inputMode_                 = InputMode::DapMemoryByteCount;
    prompt_.emplace("Byte count (default 128): ");
    statusMessage_ = prompt_->StatusText();
}

void BufferView::BuildMemoryBuffer(const std::string& memoryReference, const editor::dap::DapManager::MemoryBlock& block) {
    constexpr std::size_t kBytesPerRow = 16;

    std::string text = "Memory at " + memoryReference;
    if (!block.address.empty() && block.address != memoryReference) {
        text += " (" + block.address + ")";
    }
    text += "\n";
    for (std::size_t offset = 0; offset < block.data.size(); offset += kBytesPerRow) {
        const std::size_t  rowLen = std::min(kBytesPerRow, block.data.size() - offset);
        std::ostringstream row;
        row << std::hex << std::setfill('0') << std::setw(8) << offset << "  ";
        std::string ascii;
        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            if (i < rowLen) {
                const auto byte = block.data[offset + i];
                row << std::setw(2) << static_cast<unsigned>(byte) << ' ';
                ascii += (byte >= 0x20 && byte < 0x7f) ? static_cast<char>(byte) : '.';
            }
            else {
                row << "   ";
            }
        }
        text += row.str() + " |" + ascii + "|\n";
    }
    if (block.unreadableBytes > 0) {
        text += std::to_string(block.unreadableBytes) + " byte(s) unreadable.\n";
    }

    text::Buffer& memory = bufferList_.CreateBuffer("*memory*");
    memory.InsertAtPoint(text);
    memory.SetPoint(0);
    memory.SetReadOnly(true); // same tossable-read-only reasoning as BuildDebugBuffer
    activeBuffer_.Set(memory);
    statusMessage_.clear();
}

void BufferView::BeginDapThreadSelect() {
    if (dapManager_->State() != editor::dap::DapManager::SessionState::Stopped) {
        statusMessage_ = "Not stopped (nothing to pick a thread in).";
        return;
    }
    statusMessage_ = "Fetching threads...";
    dapManager_->RequestThreads([this](std::vector<editor::dap::DapManager::Thread> threads) {
        if (threads.empty()) {
            statusMessage_ = "No threads reported (or the session already resumed).";
            return;
        }
        pendingDapThreads_  = std::move(threads);
        dapThreadSelection_ = 0;
        inputMode_          = InputMode::DapThreadSelect;
        RefreshDapThreadSelectStatus();
    });
}

void BufferView::RefreshDapThreadSelectStatus() {
    std::string status = "Select thread: ";
    for (std::size_t i = 0; i < pendingDapThreads_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == dapThreadSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingDapThreads_[i].name + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDapThreadSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Thread selection cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        dapThreadSelection_ = (dapThreadSelection_ + 1) % pendingDapThreads_.size();
        RefreshDapThreadSelectStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        dapThreadSelection_ = (dapThreadSelection_ + pendingDapThreads_.size() - 1) % pendingDapThreads_.size();
        RefreshDapThreadSelectStatus();
        return;
    }
    std::size_t chosen = dapThreadSelection_;
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= pendingDapThreads_.size()) {
            return; // out of range -- stay in the selection list
        }
        chosen = index;
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the selection list
    }

    const editor::dap::DapManager::Thread thread = pendingDapThreads_[chosen];
    dapManager_->SelectThread(thread.id, [this, name = thread.name](bool success) {
        statusMessage_ = success ? ("Selected thread: " + name) : "Failed to select thread.";
    });
    EndInteractiveSession();
}

// DAP round 3: BeginDapThreadSelect's own shape, but a live/local toggle set
// (pendingDapEnabledExceptionFilters_) rather than a single pick -- nothing
// reaches the adapter until Enter commits it via
// DapManager::SetExceptionBreakpointFilters.
void BufferView::BeginDapExceptionFilterSelect() {
    const std::vector<editor::dap::DapManager::ExceptionFilter>& filters = dapManager_->AvailableExceptionFilters();
    if (filters.empty()) {
        statusMessage_ = "No exception breakpoint filters available (no session, or the adapter doesn't advertise any).";
        return;
    }
    pendingDapExceptionFilters_        = filters;
    pendingDapEnabledExceptionFilters_ = dapManager_->EnabledExceptionFilters();
    dapExceptionFilterSelection_       = 0;
    inputMode_                         = InputMode::DapExceptionFilterSelect;
    RefreshDapExceptionFilterStatus();
}

void BufferView::RefreshDapExceptionFilterStatus() {
    std::string status = "Exception breakpoints (space/digit toggle, enter apply): ";
    for (std::size_t i = 0; i < pendingDapExceptionFilters_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const editor::dap::DapManager::ExceptionFilter& filter   = pendingDapExceptionFilters_[i];
        const bool                                      checked  = pendingDapEnabledExceptionFilters_.contains(filter.id);
        const bool                                      selected = (i == dapExceptionFilterSelection_);
        status += (selected ? "[" : "") + std::string(checked ? "✓" : " ") + std::to_string(i + 1) + ") " + filter.label +
                  (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDapExceptionFilterSelectKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Exception breakpoint selection cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        dapExceptionFilterSelection_ = (dapExceptionFilterSelection_ + 1) % pendingDapExceptionFilters_.size();
        RefreshDapExceptionFilterStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        dapExceptionFilterSelection_ =
            (dapExceptionFilterSelection_ + pendingDapExceptionFilters_.size() - 1) % pendingDapExceptionFilters_.size();
        RefreshDapExceptionFilterStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Enter) {
        dapManager_->SetExceptionBreakpointFilters(pendingDapEnabledExceptionFilters_);
        statusMessage_ =
            "Exception breakpoints: " + std::to_string(pendingDapEnabledExceptionFilters_.size()) + " enabled.";
        EndInteractiveSession();
        return;
    }
    std::optional<std::size_t> toggled;
    if (IsPlainCharacter(chord) && chord.Codepoint == U' ') {
        toggled = dapExceptionFilterSelection_;
    }
    else if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= pendingDapExceptionFilters_.size()) {
            return; // out of range -- stay in the selection list
        }
        dapExceptionFilterSelection_ = index;
        toggled                      = index;
    }
    else {
        return; // anything else is ignored -- stay in the selection list
    }
    const std::string& id = pendingDapExceptionFilters_[*toggled].id;
    if (!pendingDapEnabledExceptionFilters_.insert(id).second) {
        pendingDapEnabledExceptionFilters_.erase(id); // was already enabled -- insert reported no-op, so toggle off
    }
    RefreshDapExceptionFilterStatus();
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
                catch (const editor::SearchPatternError& e) {
                    ReportError(std::string("Invalid regex: ") + e.what());
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
        // in-file-regex follow-up: ConfirmPattern validated this same
        // pattern text against RE2 (SearchDirectory's engine) and the
        // rewrite now runs on PCRE2 (RegexPattern), which accepts
        // essentially everything RE2 does -- but the rewrite can still
        // throw at match time if the match-limit safety net trips (see
        // RegexPattern.h). Caught here rather than left to propagate, same
        // as every other interactive failure in this file.
        try {
            const editor::ReplaceSummary summary = projectReplace_->Confirm();
            statusMessage_                       = "Replaced " + std::to_string(summary.replacementCount) + " occurrence" +
                                                   (summary.replacementCount == 1 ? "" : "s") + " in " + std::to_string(summary.filesChanged) +
                                                   " file" + (summary.filesChanged == 1 ? "" : "s") + ".";
        }
        catch (const std::exception& e) {
            ReportError(std::string("Project replace: ") + e.what());
        }
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
    const text::ITextStorage&   content    = buffer.Content();
    const std::size_t   totalLines = content.LineCount();

    // main-editor-sticky-scroll follow-up: at.y is in this pane's own local
    // coordinates, unaware that Paint() may have pushed real content down by
    // stickyRowCount_ rows this frame -- subtracted here so a click below
    // the pinned rows still resolves to the buffer line actually drawn
    // there. A click landing IN the pinned band itself (result would be
    // negative) clamps to row 0, same as the existing at.y<0 defensive
    // clamp below.
    std::size_t targetRow     = static_cast<std::size_t>(std::max(at.y - stickyRowCount_, 0));
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
    // comment): [status][diagnostic][gap][digits][gap][symbol][fold], left
    // to right -- status and diagnostic are always reserved; the fold region
    // (generic-code-folding / depth-aware-fold-gutter follow-ups) only when
    // a mode has a real fold query and the feature is enabled, a fixed
    // kMaxFoldDepthColumns-wide reservation (not one that grows with how
    // deep the currently-visible content happens to nest -- an explicit
    // user choice, so the gutter's own width never jumps around while
    // scrolling past a deeply nested region). symbol (gutter-symbol-kind
    // follow-up), unlike fold, IS data-driven -- see SymbolGutterActive's
    // own doc comment for why.
    const std::size_t foldColumn   = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    const std::size_t blameColumn  = BlameGutterActive() ? kBlameWidth : 0;
    const std::size_t diffColumn   = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t dapColumn    = DapGutterActive() ? kDapWidth : 0;
    const std::size_t symbolColumn = SymbolGutterActive() ? kSymbolWidth : 0;
    const std::size_t testColumn   = TestGutterActive() ? kTestWidth : 0;
    // Multibuffers follow-up: the line-number digits + both surrounding
    // gaps collapse to zero width together when LineNumberGutterActive()
    // is false -- see its own doc comment.
    const std::size_t lineNumberColumn =
        LineNumberGutterActive() ? (kLineNumberGap + std::to_string(totalLines).size() + kLineNumberGap) : 0;
    return dapColumn + diffColumn + kStatusWidth + kDiagnosticWidth + lineNumberColumn + testColumn + symbolColumn +
           foldColumn + blameColumn;
}

bool BufferView::SymbolGutterActive() const {
    EnsureSymbolGutterCache();
    return !symbolGutterLineKinds_.empty();
}

bool BufferView::TestGutterActive() const {
    EnsureTestGutterCache();
    return !testGutterLineStatuses_.empty();
}

bool BufferView::DiffGutterActive() const {
    return !diffLineKinds_.empty();
}

bool BufferView::LineNumberGutterActive() const {
    return editor::multibuffer::MultibufferIndexFor(activeBuffer_.Get()) == nullptr;
}

bool BufferView::DapGutterActive() const {
    if (dapManager_ == nullptr) {
        return false;
    }
    EnsureDapPathKey();
    if (dapPathKey_.empty()) {
        return false; // a pathless buffer can't hold breakpoints or be stopped in
    }
    if (!dapManager_->BreakpointLinesForKey(dapPathKey_).empty()) {
        return true;
    }
    const auto stop = dapManager_->CurrentStopKeyAndLine();
    return stop && stop->first == dapPathKey_;
}

void BufferView::EnsureDapPathKey() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (dapPathKeyBuffer_ == &buffer && dapPathKeyRawPath_ == buffer.Path()) {
        return;
    }
    dapPathKeyBuffer_  = &buffer;
    dapPathKeyRawPath_ = buffer.Path();
    dapPathKey_        = buffer.Path() ? editor::dap::DapManager::NormalizePathKey(*buffer.Path()) : std::string();
}

bool BufferView::OnMouseEvent(const Event& event) {
    const MouseEvent rawMouse = event.mouse();
    LogMouseEvent(MouseEventTag(rawMouse), rawMouse);

    // A growing sidebar-resize drag (round-2 sidebar follow-up) can deliver
    // move/release events while the cursor is over BufferView, not
    // ProjectSidebar itself -- checked first, regardless of position (every
    // leaf widget receives every mouse event; see Widget.h's own header
    // comment), taking priority over BufferView's own handling.
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

    // Split-resize follow-up: same "checked first, regardless of position"
    // cooperation as the ProjectSidebar guard just above -- a split divider
    // dragged past a neighboring pane's own edge delivers move/release
    // events here too. The divider itself (not this BufferView) owns the
    // drag; this only needs to stay out of the way so a stray Moved/Released
    // doesn't start or extend a stale text selection using this pane's own
    // (unrelated) dragAnchor_ while some other pane's divider is live.
    if (splitResizeQuery_ && splitResizeQuery_() &&
        (rawMouse.motion == MouseEvent::Motion::Moved || rawMouse.motion == MouseEvent::Motion::Released)) {
        return true;
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

    // horizontal-wheel-scroll follow-up: same unconditional-of-InputMode
    // shape as the vertical wheel case above. A no-op (but still consumed)
    // once EffectiveWrapLines() is true -- a wrapped line never extends past
    // the viewport width, matching ScrollToShowPointHorizontally's own
    // guard, so leftColumn_ has nothing left to scroll.
    if (mouse->button == MouseEvent::Button::WheelLeft || mouse->button == MouseEvent::Button::WheelRight) {
        if (!EffectiveWrapLines()) {
            constexpr std::size_t kWheelScrollColumns = 3;
            if (mouse->button == MouseEvent::Button::WheelLeft) {
                SetLeftColumn((leftColumn_ > kWheelScrollColumns) ? leftColumn_ - kWheelScrollColumns : 0);
            }
            else {
                SetLeftColumn(leftColumn_ + kWheelScrollColumns);
            }
        }
        return true;
    }

    // Middle-click-paste follow-up: X11/Wayland's own "click to insert the
    // primary selection" convention, independent of the kill ring/system
    // clipboard C-y uses. Point still moves to the click (matching plain
    // left-click's own unconditional SetPoint below, ReadOnly included) even
    // when there's nothing to paste (no primary-selection tool resolved, or
    // an empty selection) -- only the insert itself is skipped, and only for
    // a read-only buffer (InsertAtPoint throws there, with no command-
    // registry catch net to fall back on at this call site).
    if (mouse->button == MouseEvent::Button::Middle && mouse->motion == MouseEvent::Motion::Pressed) {
        if (inputMode_ != InputMode::Normal) {
            return false;
        }
        TakeFocus();
        text::Buffer& buffer = activeBuffer_.Get();
        buffer.ClearMark();
        buffer.SetPoint(ByteOffsetForPoint(mouse->at));
        if (!buffer.ReadOnly()) {
            if (const std::optional<std::string> pasted = editor::PasteFromPrimarySelection()) {
                buffer.InsertAtPoint(*pasted);
            }
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

        // main-editor-sticky-scroll follow-up: click-to-jump, checked first
        // (same "one specific region wins over the generic click fallthrough"
        // shape the fold-gutter click check just below already has) -- a
        // click inside the pinned band moves point to that ancestor's own
        // definition start instead of falling through to ByteOffsetForPoint,
        // which would otherwise resolve it against whatever real buffer line
        // the pinned rows are currently covering.
        if (stickyRowCount_ > 0 && mouse->at.y < stickyRowCount_) {
            const std::vector<editor::SymbolMarker> chain = StickyScrollChainForCurrentViewport();
            const auto                              index = static_cast<std::size_t>(mouse->at.y);
            if (index < chain.size()) {
                text::Buffer& buffer = activeBuffer_.Get();
                buffer.ClearMark();
                buffer.SetPoint(chain[index].startByte);
                ScrollToShowPoint();
            }
            return true;
        }

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
        const std::size_t foldColumnWidth  = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
        const std::size_t blameColumnWidth = BlameGutterActive() ? kBlameWidth : 0;
        // Mirrors GutterWidth()/Paint()'s own
        // [status][gap][digits][gap][symbol][fold][blame] layout -- foldStart
        // is where the fold region actually starts on screen: GutterWidth()
        // minus fold's and blame's own widths leaves everything to fold's
        // *left* (dap/diff/status/diagnostic/line-numbers/symbol, gutter-
        // symbol-kind follow-up), which is exactly foldStart -- no separate
        // symbol subtraction needed here, unlike the other three call sites
        // this check's own doc comment warns about, since this one derives
        // from the already-symbol-aware GutterWidth() instead of summing
        // column starts independently.
        const std::size_t foldStart = GutterWidth() - foldColumnWidth - blameColumnWidth;
        if (foldColumnWidth > 0 && mouse->at.x >= static_cast<int>(foldStart) &&
            static_cast<std::size_t>(mouse->at.x) < foldStart + foldColumnWidth) {
            text::Buffer&     buffer        = activeBuffer_.Get();
            const text::ITextStorage& content       = buffer.Content();
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

        // Universal-clickable-affordances follow-up: Ctrl+Click opens the
        // link under the click, the mouse counterpart to open-link-at-
        // point's own C-c C-l -- same VS Code/browser convention (plain
        // click still just places point, matching every other mode) and
        // available in every mode without any per-mode wiring, since
        // OpenLinkAtPoint() already tries Org's bracket links first and
        // falls back to the generic bare-URL/file-path scan for everything
        // else. Takes priority over the read-only visit-result click below
        // -- an explicit Ctrl+Click is a more specific request than a plain
        // click, so it shouldn't silently fall back to a visit when the
        // clicked position isn't on a link.
        if (mouse->control) {
            clickCount_      = 0;
            lastClickOffset_ = std::nullopt;
            OpenLinkAtPoint();
            return true;
        }

        // Double/triple-click word/line selection -- same repeated-click-at-
        // the-same-spot detection ProjectSidebar's double-click-to-open uses
        // (kDoubleClickWindow), extended with a click count so a third click
        // selects the whole line instead of re-selecting the word. Skipped
        // on a read-only ("tossable") results buffer, where a click's job is
        // visiting the result under it, not selecting text.
        const auto now = std::chrono::steady_clock::now();
        clickCount_     = (lastClickOffset_.has_value() && *lastClickOffset_ == offset && (now - lastClickTime_) < kDoubleClickWindow)
                              ? (clickCount_ >= 3 ? 1 : clickCount_ + 1)
                              : 1;
        lastClickOffset_ = offset;
        lastClickTime_   = now;

        if (!buffer.ReadOnly() && clickCount_ >= 2) {
            const text::ITextStorage& content = buffer.Content();
            std::size_t       start;
            std::size_t       end;
            if (clickCount_ == 2) {
                const auto [wordStart, wordEnd] = WordBoundsAtOffset(content, offset);
                start                           = wordStart;
                end                             = wordEnd;
            }
            else {
                const std::size_t line = content.ByteOffsetToLine(offset);
                start                  = content.LineToByteOffset(line);
                end = line + 1 < content.LineCount() ? content.LineToByteOffset(line + 1) : content.ByteLength();
            }
            buffer.SetMark(start);
            buffer.SetPoint(end);
            dragAnchor_ = start;
            return true;
        }

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
    // mouse.x/y are absolute (screen-space) coordinates here -- coordinates
    // aren't translated before delivery (see Widget.h's own header comment),
    // and this logs the raw event as received, before LocalMouseEvent's own
    // translation.
    log << event << " at=(" << mouse.at.x << ',' << mouse.at.y << ')' << " button=" << static_cast<int>(mouse.button)
        << " inputMode=" << static_cast<int>(inputMode_) << " point=" << buffer.Point()
        << " mark=" << (buffer.HasMark() ? static_cast<long long>(buffer.Mark()) : -1LL) << " topLine=" << topLine_
        << " size=" << size().width << 'x' << size().height << '\n';
}

void BufferView::HandleConfirmQuitKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        // See the identical null check in OnKeyEvent's own context.quit
        // branch for why this is required, not defensive -- and that
        // branch's comment for why the message is set before exiting.
        statusMessage_ = "Shutting down...";
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

void BufferView::HandleConfirmOverwriteSaveKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        EndInteractiveSession();
        // save-buffer-force is the same save body save-buffer runs, minus
        // the supersession gate that routed us here -- see Commands.cpp.
        editor::CommandContext context = MakeContext();
        try {
            dispatcher_.Registry().Invoke("save-buffer-force", context);
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Save cancelled; the file on disk was left as-is.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
}

void BufferView::HandleConfirmSaveWithConflictsKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        EndInteractiveSession();
        // save-buffer-force is the same save body save-buffer runs, minus
        // both gates (ExternallyModified() and HasConflictMarkers()) that
        // could have routed us here -- see Commands.cpp.
        editor::CommandContext context = MakeContext();
        try {
            dispatcher_.Registry().Invoke("save-buffer-force", context);
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Save cancelled; resolve the <<<<<<< markers first.";
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
            ReportError(e.what());
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

void BufferView::RequestTrustProjectInit(
    const std::filesystem::path&                                                   initPath,
    std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onDecision) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    pendingTrustInitPath_ = initPath;
    onTrustDecision_      = std::move(onDecision);
    inputMode_            = InputMode::ConfirmTrustProjectInit;
    statusMessage_        = "Load project init \"" + initPath.string() + "\"? (y=once, a=always, n=no)";
}

void BufferView::HandleConfirmTrustProjectInitKey(const editor::KeyChord& chord) {
    std::optional<editor::ProjectInitDecision> decision;
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        decision = editor::ProjectInitDecision::LoadOnce;
    }
    else if (chord.Codepoint == U'a' || chord.Codepoint == U'A') {
        decision = editor::ProjectInitDecision::LoadAlways;
    }
    else if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        decision = editor::ProjectInitDecision::Decline;
    }
    if (!decision) {
        return; // anything else is ignored -- stay in the prompt
    }

    // Moved out before EndInteractiveSession clears both members; the
    // callback runs after the session has fully ended so it's free to set
    // statusMessage_ (and could even start a new prompt) without this
    // session's teardown clobbering it.
    const std::filesystem::path path       = pendingTrustInitPath_;
    const auto                  onDecision = std::move(onTrustDecision_);
    EndInteractiveSession();
    if (onDecision) {
        onDecision(path, *decision);
    }
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
        (void)HandlePromptEditingKey(chord); // outcome doesn't matter here -- always just re-echoes the prompt text
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
            ReportError(e.what());
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
        const std::filesystem::path source      = renameSource_;
        EndInteractiveSession();
        PerformProjectRename(source, destination);
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Rename cancelled.";
        EndInteractiveSession();
        return;
    }
    (void)HandlePromptEditingKey(chord);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::PerformProjectRename(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        ReportError("ned: does not exist: " + source.string());
        return;
    }

    // Snapshot every open buffer's canonical path *before* the actual
    // rename happens on disk -- weakly_canonical needs real ancestors to
    // resolve symlinks through, and once source (or an ancestor of a
    // buffer nested inside it, for a directory rename) is gone, there's
    // nothing left on disk at the old location to resolve against.
    std::vector<std::pair<text::Buffer*, std::filesystem::path>> openBuffers;
    for (const auto& candidate : bufferList_.Buffers()) {
        if (candidate->Path().has_value()) {
            openBuffers.emplace_back(candidate.get(), std::filesystem::weakly_canonical(*candidate->Path()));
        }
    }
    const std::filesystem::path sourceCanonical = std::filesystem::weakly_canonical(source);

    // rename-file-notifications follow-up: one (oldPath, newPath) pair per
    // real file this rename touches -- a directory rename touches every
    // file nested inside it, each needing its own pair for
    // LspManager::RequestWillRenameFiles/NotifyFilesRenamed's per-file glob
    // matching (a server's filter is typically an extension glob like
    // "**/*.ts", which only makes sense matched per file, not against the
    // renamed directory's own path). Walked from source, which must still
    // exist on disk at this point -- see the existence check above.
    std::vector<editor::lsp::LspManager::FileRenameEntry> renamedFiles;
    if (std::filesystem::is_directory(source, ec)) {
        std::error_code walkEc;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 source, std::filesystem::directory_options::skip_permission_denied, walkEc)) {
            if (entry.is_regular_file(walkEc)) {
                const std::filesystem::path relative = entry.path().lexically_relative(source);
                renamedFiles.push_back({entry.path(), destination / relative});
            }
        }
    }
    else {
        renamedFiles.push_back({source, destination});
    }

    // rename-file-notifications follow-up: the actual rename+buffer-
    // bookkeeping, deferred behind LspManager::RequestWillRenameFiles below
    // -- captured by value since source/destination/openBuffers/
    // sourceCanonical/renamedFiles must all outlive this call, and a fresh
    // rename session started before this one's round trip completes must
    // not be able to invalidate any of them (they're locals here, not the
    // renameSource_/renameStage_ members EndInteractiveSession already
    // reset before this method was ever called).
    auto finishRename = [this, source, destination, sourceCanonical, openBuffers, renamedFiles](
                            std::optional<editor::lsp::LspManager::ResolvedRename> willRenameEdit) {
        // Applied BEFORE the actual rename below, per spec's intended use:
        // a server's willRenameFiles response typically fixes up *other*
        // files' import paths while source still exists at its pre-rename
        // location.
        if (willRenameEdit) {
            ApplyResolvedWorkspaceEdit(*willRenameEdit, "Fixed up references before rename");
        }
        try {
            editor::RenameProjectPath(source, destination);

            // Any open buffer whose file *was* source itself, or was
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
                    // per-buffer-mode-cache follow-up: a rename can change
                    // which Mode applies (e.g. .txt -> .cpp) without
                    // touching content at all -- CachedModeForBuffer would
                    // otherwise keep returning the Mode built for the old
                    // path forever, since its cache is keyed purely by
                    // buffer identity with no path check of its own.
                    editor::ClearModeCacheFor(*buffer);
                }
                else if (const std::filesystem::path relative = canonicalPath.lexically_relative(sourceCanonical);
                         !relative.empty() && *relative.begin() != "..") {
                    buffer->SetPath(destination / relative);
                    editor::ClearModeCacheFor(*buffer);
                }
            }

            statusMessage_ = "Renamed to " + destination.string();
            if (projectSidebar_) {
                projectSidebar_->InvalidateTree();
            }
            if (lspManager_) {
                lspManager_->NotifyFilesRenamed(renamedFiles);
            }
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    };

    if (lspManager_) {
        lspManager_->RequestWillRenameFiles(renamedFiles, std::move(finishRename));
    }
    else {
        finishRename(std::nullopt);
    }
}

void BufferView::HandleSetPropertyKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();

        if (propertyStage_ == PropertyPromptStage::EnteringName) {
            if (input.empty()) {
                statusMessage_ = "Property name cannot be empty.";
                EndInteractiveSession();
                return;
            }
            pendingPropertyName_ = input;
            propertyStage_       = PropertyPromptStage::EnteringValue;
            prompt_.emplace("Value for :" + pendingPropertyName_ + ": ");
            // Pre-fill with the property's current value, if it already has
            // one -- same "prompt opens pre-filled with what's already
            // there" precedent SetHeadlineTags's own StartInteractiveSession
            // case establishes.
            if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
                if (const auto current = editor::org::GetProperty(activeBuffer_.Get().Text(), *headline, pendingPropertyName_)) {
                    prompt_->SetText(*current);
                }
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }

        // EnteringValue -- re-resolves HeadlineAtPoint fresh rather than
        // trusting anything captured when the session opened, same
        // reasoning SetHeadlineTags's own doc comment states (point can't
        // have moved since org-set-property's own precondition check).
        if (editor::org::SetPropertyAtPoint(activeBuffer_.Get(), pendingPropertyName_, input)) {
            statusMessage_.clear();
        }
        else {
            statusMessage_ = "Not on a headline.";
        }
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Set property cancelled.";
        EndInteractiveSession();
        return;
    }
    (void)HandlePromptEditingKey(chord);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::HandleRecoverFileKey(const editor::KeyChord& chord) {
    if (recoverStage_ == RecoverFileStage::PickingVersion) {
        if (chord.Special == editor::SpecialKey::Enter) {
            const std::string input  = prompt_->Text();
            std::size_t       choice = 1; // Enter alone means the newest
            if (!input.empty()) {
                try {
                    choice = std::stoul(input);
                }
                catch (const std::exception&) {
                    choice = 0; // non-numeric -- caught by the range check below
                }
            }
            if (choice < 1 || choice > recoverVersions_.size()) {
                statusMessage_ = "No such version: " + input;
                EndInteractiveSession();
                return;
            }
            recoverChoice_ = choice - 1;
            recoverStage_  = RecoverFileStage::Confirming;
            prompt_.reset();
            statusMessage_ = "Recover \"" + recoverVersions_[recoverChoice_].label + "\" over buffer " + activeBuffer_.Get().Name() + "? (y/n)";
            return;
        }
        if (IsQuit(chord)) {
            statusMessage_ = "Recover cancelled.";
            EndInteractiveSession();
            return;
        }
        // Digit-only field -- HandlePromptEditingKey's own plain-character
        // insert would accept anything, so this stays hand-rolled rather
        // than delegating InsertChar the way the free-text prompts do;
        // Backspace/Delete/Left/Right/Home/End all still apply unchanged.
        if (chord.Special == editor::SpecialKey::Backspace) {
            prompt_->DeleteBackward();
        }
        else if (chord.Special == editor::SpecialKey::Delete) {
            prompt_->DeleteForward();
        }
        else if (chord.Special == editor::SpecialKey::Left) {
            prompt_->MoveCursorLeft();
        }
        else if (chord.Special == editor::SpecialKey::Right) {
            prompt_->MoveCursorRight();
        }
        else if (chord.Special == editor::SpecialKey::Home) {
            prompt_->MoveCursorToStart();
        }
        else if (chord.Special == editor::SpecialKey::End) {
            prompt_->MoveCursorToEnd();
        }
        else if (IsPlainCharacter(chord) && chord.Codepoint >= U'0' && chord.Codepoint <= U'9') {
            prompt_->InsertChar(chord.Codepoint);
        }
        statusMessage_ = prompt_->StatusText();
        return;
    }

    // Confirming
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        try {
            const std::string content = editor::ReadBackupVersion(recoverVersions_[recoverChoice_].path);
            activeBuffer_.Get().RestoreContent(content);
            statusMessage_ = "Recovered " + recoverVersions_[recoverChoice_].label + " -- buffer is modified; save to keep it";
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        EndInteractiveSession();
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = "Recover cancelled.";
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored -- stay in the prompt.
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

    const char32_t name   = chord.Codepoint;
    text::Buffer&  buffer = activeBuffer_.Get();
    switch (inputMode_) {
        case InputMode::PointToRegister: {
            // multi-cursor-round-2 follow-up: [0] is the primary's point,
            // the rest are every secondary's, in cursor order.
            std::vector<std::size_t> points{buffer.Point()};
            for (const auto& cursor : buffer.SecondaryCursors()) {
                points.push_back(cursor.point);
            }
            registers_.SetPoint(name, buffer.Name(), std::move(points));
            statusMessage_ = "Point stored in register.";
            break;
        }
        case InputMode::JumpToRegister:
            if (const editor::PointRegisterValue* value = registers_.Point(name)) {
                if (text::Buffer* target = bufferList_.Find(value->bufferName)) {
                    PushJumpMark();
                    activeBuffer_.Set(*target);
                    // multi-cursor-round-2 follow-up: restores the primary
                    // point plus a secondary cursor for every further saved
                    // offset -- byteOffsets is never empty (RegisterTable::
                    // SetPoint's own guarantee).
                    target->ClearSecondaryCursors();
                    target->SetPoint(value->byteOffsets.front()); // Buffer::SetPoint already clamps out-of-range offsets
                    for (std::size_t i = 1; i < value->byteOffsets.size(); ++i) {
                        target->AddCursorAt(value->byteOffsets[i]);
                    }
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
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No region to copy.";
                }
                else {
                    const auto [start, end] = buffer.Region();
                    registers_.SetText(name, buffer.Content().Substring(start, end - start));
                    statusMessage_ = "Copied to register.";
                }
            }
            else {
                // multi-cursor-round-2 follow-up: same "one piece per
                // cursor, empty for a cursor with no mark" shape
                // KillPerCursor uses for kill-region/kill-ring-save.
                std::vector<std::string> pieces;
                bool                     any = false;
                buffer.ForEachCursor([&] {
                    if (buffer.HasMark()) {
                        const auto [start, end] = buffer.Region();
                        pieces.push_back(buffer.Content().Substring(start, end - start));
                        any = true;
                    }
                    else {
                        pieces.emplace_back();
                    }
                });
                if (any) {
                    registers_.SetTextPieces(name, std::move(pieces));
                    statusMessage_ = "Copied to register.";
                }
                else {
                    statusMessage_ = "No region to copy.";
                }
            }
            break;
        case InputMode::InsertRegister: {
            const std::string* blob = registers_.Text(name);
            if (!blob) {
                statusMessage_ = "Register does not contain text.";
                break;
            }
            if (!buffer.HasSecondaryCursors()) {
                buffer.InsertAtPoint(*blob);
                statusMessage_.clear();
                break;
            }
            // multi-cursor-round-2 follow-up: distribute 1:1 in cursor
            // order when the entry's own piece count matches how many
            // cursors are live right now, else fall back to the whole
            // joined blob at every cursor -- same rule multi-cursor yank
            // uses on KillRing.
            const std::vector<std::string>& pieces      = *registers_.TextPieces(name); // guaranteed present alongside blob
            const std::size_t               cursorCount = 1 + buffer.SecondaryCursors().size();
            const bool                      perCursor   = pieces.size() == cursorCount;
            std::size_t                     i           = 0;
            buffer.ForEachCursor([&] {
                buffer.InsertAtPoint(perCursor ? pieces[i] : *blob);
                ++i;
            });
            statusMessage_.clear();
            break;
        }
        default:
            break; // unreachable -- OnKeyEvent only routes here for the four modes above
    }

    EndInteractiveSession();
}

void BufferView::HandleZapToCharKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Zap to char cancelled.";
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a target character
    }

    const char32_t target = chord.Codepoint;
    const bool     append = pendingZapToCharAppend_;
    text::Buffer&  buffer = activeBuffer_.Get();

    // The offset just past the next occurrence of `target` at/after `from`,
    // or nullopt if there isn't one -- forward scan, codepoint-granular
    // (matches Buffer's own word-motion scanning style).
    const auto findForward = [&](std::size_t from) -> std::optional<std::size_t> {
        const text::ITextStorage& content = buffer.Content();
        const std::size_t total   = content.ByteLength();
        std::size_t       offset  = from;
        while (offset < total) {
            const auto decoded = content.CodepointAt(offset);
            offset += decoded.byteLength;
            if (decoded.codepoint == target) {
                return offset;
            }
        }
        return std::nullopt;
    };

    if (!buffer.HasSecondaryCursors()) {
        const std::size_t point = buffer.Point();
        if (const auto end = findForward(point)) {
            buffer.ClearMark();
            std::string text = buffer.DeleteRange(point, *end - point);
            if (append) {
                killRing_.AppendToCurrent(std::move(text), /*prepend=*/false);
            }
            else {
                killRing_.Kill(std::move(text));
            }
            editor::CopyToSystemClipboard(killRing_.Current());
            statusMessage_.clear();
        }
        else {
            statusMessage_ = "No such character.";
        }
        EndInteractiveSession();
        return;
    }

    // multi-cursor: one piece per cursor, empty for a cursor with no match
    // -- KillPerCursor's own resolution (Commands.cpp), duplicated here for
    // the same reason CopyToRegister above duplicates it: KillPerCursor is
    // file-local to Commands.cpp. Multi-cursor kill-append is a deliberate
    // v1 cut (KillRing::AppendToCurrent's own doc comment) -- always a
    // fresh entry here regardless of `append`.
    std::vector<std::string> pieces;
    bool                     any = false;
    buffer.ForEachCursor([&] {
        const std::size_t point = buffer.Point();
        if (const auto end = findForward(point)) {
            buffer.ClearMark();
            pieces.push_back(buffer.DeleteRange(point, *end - point));
            any = true;
        }
        else {
            pieces.emplace_back();
        }
    });
    if (any) {
        killRing_.KillPieces(std::move(pieces));
        editor::CopyToSystemClipboard(killRing_.Current());
        statusMessage_.clear();
    }
    else {
        statusMessage_ = "No such character.";
    }
    EndInteractiveSession();
}

void BufferView::HandleOrgCaptureKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Capture cancelled.";
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a template key
    }

    // Template keys are always plain ASCII (Janet callers pass a one-char
    // std::string) -- anything outside that range simply can't match.
    if (chord.Codepoint > 0x7F) {
        statusMessage_ = "No such capture template.";
        EndInteractiveSession();
        return;
    }

    const auto tmpl = editor::org::CaptureTemplateForKey(static_cast<char>(chord.Codepoint));
    if (!tmpl) {
        statusMessage_ = "No such capture template.";
        EndInteractiveSession();
        return;
    }

    try {
        const std::filesystem::path targetPath(tmpl->targetFile);
        if (targetPath.has_parent_path()) {
            std::filesystem::create_directories(targetPath.parent_path());
        }
        text::Buffer&                    target = bufferList_.OpenOrCreateFile(targetPath);
        const editor::org::CaptureResult result = editor::org::InsertCapture(target, *tmpl);
        activeBuffer_.Set(target);
        target.ClearSecondaryCursors();
        target.SetPoint(result.insertedAt);
        if (!tmpl->headline.empty() && !result.headlineFound) {
            statusMessage_ = "Headline \"" + tmpl->headline + "\" not found -- filed at end of " + tmpl->targetFile;
        }
        else {
            statusMessage_ = "Captured: " + tmpl->name;
        }
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
    EndInteractiveSession();
}

void BufferView::RefreshExecuteCommandStatus() {
    const std::vector<std::string> ranked =
        editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());
    executeCommandSelection_ = ranked.empty() ? 0 : std::min(executeCommandSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, executeCommandSelection_)));
    }
}

void BufferView::HandleExecuteCommandKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("execute-command", prompt_->Text());

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

    if (TryNavigatePromptHistory(chord, "execute-command")) {
        executeCommandSelection_ = 0;
        RefreshExecuteCommandStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked =
            editor::FuzzyFilterAndRank(dispatcher_.Registry().Names(), prompt_->Text());
        if (!ranked.empty()) {
            // Wraps at either end -- the list's own bottom/top -- rather
            // than sticking there, matching every ListPopup-driven focus
            // list's own navigation.
            executeCommandSelection_ = chord.Special == editor::SpecialKey::Down
                                          ? (executeCommandSelection_ + 1) % ranked.size()
                                          : (executeCommandSelection_ + ranked.size() - 1) % ranked.size();
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
    // affordance would be redundant. Pure cursor movement (CursorMoved)
    // re-snaps nothing -- the candidate list doesn't change.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_      = kNoHistoryIndex; // editing exits history browsing -- see TryNavigatePromptHistory's own doc comment
        executeCommandSelection_ = 0;
        RefreshExecuteCommandStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::RefreshProjectFindFileStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(projectFindFileCandidates_, prompt_->Text());
    projectFindFileSelection_             = ranked.empty() ? 0 : std::min(projectFindFileSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty()
                ? std::nullopt
                : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, projectFindFileSelection_)));
    }
}

void BufferView::HandleProjectFindFileKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("project-find-file", prompt_->Text());

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
            ReportError(e.what());
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Project find file cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "project-find-file")) {
        projectFindFileSelection_ = 0;
        RefreshProjectFindFileStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(projectFindFileCandidates_, prompt_->Text());
        if (!ranked.empty()) {
            projectFindFileSelection_ = chord.Special == editor::SpecialKey::Down
                                           ? (projectFindFileSelection_ + 1) % ranked.size()
                                           : (projectFindFileSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshProjectFindFileStatus();
        return;
    }

    // Same "typing re-snaps to the top match" reasoning as
    // HandleExecuteCommandKey above -- see that method's own comment.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_       = kNoHistoryIndex; // editing exits history browsing -- see TryNavigatePromptHistory's own doc comment
        projectFindFileSelection_ = 0;
        RefreshProjectFindFileStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// editor-ergonomics follow-up: HandleProjectFindFileKey/
// RefreshProjectFindFileStatus's own shape, over recentFileCandidates_
// (already-absolute paths, unlike ProjectFindFile's project-relative ones,
// so Enter opens the selection directly with no ProjectRoot() join).
void BufferView::RefreshFindRecentFileStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(recentFileCandidates_, prompt_->Text());
    recentFileSelection_                  = ranked.empty() ? 0 : std::min(recentFileSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, recentFileSelection_)));
    }
}

void BufferView::HandleFindRecentFileKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("find-recent-file", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(recentFileCandidates_, prompt_->Text());

        if (ranked.empty()) {
            statusMessage_ = "No recent file matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::filesystem::path selected = ranked[std::min(recentFileSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(selected);
            activeBuffer_.Set(opened);
            statusMessage_ = "Opened " + opened.Name();
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Find recent file cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "find-recent-file")) {
        recentFileSelection_ = 0;
        RefreshFindRecentFileStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(recentFileCandidates_, prompt_->Text());
        if (!ranked.empty()) {
            recentFileSelection_ = chord.Special == editor::SpecialKey::Down
                                      ? (recentFileSelection_ + 1) % ranked.size()
                                      : (recentFileSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshFindRecentFileStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_  = kNoHistoryIndex;
        recentFileSelection_ = 0;
        RefreshFindRecentFileStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

namespace {
    // "name — root" -- unique per entry since ProjectRegistryStore keys on
    // the normalized root, so this doubles as the lookup key back from a
    // ranked/selected string to its underlying entry below.
    std::string FormatProjectEntry(const ned::editor::ProjectRegistryEntry& entry) {
        return entry.name + " — " + entry.root;
    }
} // namespace

// named-projects follow-up: HandleProjectFindFileKey/RefreshProjectFindFileStatus's
// own shape, over switchProjectEntries_ (Editor/ProjectRegistry.h's saved-project
// list) formatted via FormatProjectEntry.
void BufferView::RefreshSwitchProjectStatus() {
    std::vector<std::string> candidates;
    candidates.reserve(switchProjectEntries_.size());
    for (const auto& entry : switchProjectEntries_) {
        candidates.push_back(FormatProjectEntry(entry));
    }

    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
    switchProjectSelection_ = ranked.empty() ? 0 : std::min(switchProjectSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, switchProjectSelection_)));
    }
}

void BufferView::HandleSwitchProjectKey(const editor::KeyChord& chord) {
    std::vector<std::string> candidates;
    candidates.reserve(switchProjectEntries_.size());
    for (const auto& entry : switchProjectEntries_) {
        candidates.push_back(FormatProjectEntry(entry));
    }

    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("switch-project", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No project matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string selected = ranked[std::min(switchProjectSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        const auto it = std::find_if(switchProjectEntries_.begin(), switchProjectEntries_.end(),
                                      [&selected](const editor::ProjectRegistryEntry& entry) {
                                          return FormatProjectEntry(entry) == selected;
                                      });
        if (it == switchProjectEntries_.end()) {
            ReportError("Internal error resolving the selected project.");
            return;
        }
        ActivateProjectAndReport(it->root);
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Switch project cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "switch-project")) {
        switchProjectSelection_ = 0;
        RefreshSwitchProjectStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (!ranked.empty()) {
            switchProjectSelection_ = chord.Special == editor::SpecialKey::Down
                                         ? (switchProjectSelection_ + 1) % ranked.size()
                                         : (switchProjectSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshSwitchProjectStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_     = kNoHistoryIndex;
        switchProjectSelection_ = 0;
        RefreshSwitchProjectStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// dropdown-path-completion follow-up: HandleSwitchProjectKey's own shape,
// over every open buffer's name (text::CompleteBufferNames(bufferList_, "")
// -- the full list, fuzzy-ranked here rather than prefix-filtered). Unlike
// find-file/find-scratch, there's no "create a new buffer by typing a name
// that doesn't exist" case (the old CompletePrompt-driven prompt reported an
// error on a non-match rather than creating anything), so Enter safely
// resolves the highlighted ranked candidate instead of literal prompt text.
void BufferView::RefreshSwitchToBufferStatus() {
    const std::vector<std::string> candidates = text::CompleteBufferNames(bufferList_, "");
    const std::vector<std::string> ranked     = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
    switchToBufferSelection_ = ranked.empty() ? 0 : std::min(switchToBufferSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, switchToBufferSelection_)));
    }
}

void BufferView::HandleSwitchToBufferKey(const editor::KeyChord& chord) {
    const std::vector<std::string> candidates = text::CompleteBufferNames(bufferList_, "");

    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("switch-to-buffer", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No buffer matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string selected = ranked[std::min(switchToBufferSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        if (text::Buffer* found = bufferList_.Find(selected)) {
            activeBuffer_.Set(*found);
            statusMessage_.clear();
        }
        else {
            ReportError("Internal error resolving the selected buffer.");
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Switch to buffer cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "switch-to-buffer")) {
        switchToBufferSelection_ = 0;
        RefreshSwitchToBufferStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (!ranked.empty()) {
            switchToBufferSelection_ = chord.Special == editor::SpecialKey::Down
                                          ? (switchToBufferSelection_ + 1) % ranked.size()
                                          : (switchToBufferSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshSwitchToBufferStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_      = kNoHistoryIndex;
        switchToBufferSelection_ = 0;
        RefreshSwitchToBufferStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// dropdown-path-completion follow-up: RefreshSwitchToBufferStatus's own
// shape, over vcsBranchCandidates_ (populated once, before this mode is
// entered -- see BeginVcsSwitchBranchPrompt). VcsCreateBranch is a distinct
// InputMode (free-text new-branch naming, no candidate list) and is not
// handled here -- it stays on HandlePromptKey's own literal-text path.
void BufferView::RefreshVcsSwitchBranchStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(vcsBranchCandidates_, prompt_->Text());
    vcsSwitchBranchSelection_ = ranked.empty() ? 0 : std::min(vcsSwitchBranchSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, vcsSwitchBranchSelection_)));
    }
}

void BufferView::HandleVcsSwitchBranchKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("vcs-switch-branch", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(vcsBranchCandidates_, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No branch matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string selected = ranked[std::min(vcsSwitchBranchSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        if (!vcsRunner_) {
            statusMessage_ = "no vcs runner configured";
            return;
        }
        // A branch switch rewrites the working tree underneath any open
        // buffer. Unmodified buffers catch up on the next auto-revert tick
        // (external-modification-safety follow-up, Editor/AutoRevert.h);
        // a *modified* buffer is left alone, its save hitting the
        // supersession y/n rather than a confusing stale-content overwrite.
        statusMessage_ = "Switching to " + selected + "...";
        vcsRunner_->RequestBranchSwitch(
            selected,
            [this, selected] {
                statusMessage_ = "Switched to " + selected + " (modified buffers not reloaded)";
                RefreshVcsStatusBuffer();
                RequestDiffForCurrentBuffer();
            },
            [this](std::string error) { statusMessage_ = "vcs branch: " + error; });
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Switch branch cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "vcs-switch-branch")) {
        vcsSwitchBranchSelection_ = 0;
        RefreshVcsSwitchBranchStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(vcsBranchCandidates_, prompt_->Text());
        if (!ranked.empty()) {
            vcsSwitchBranchSelection_ = chord.Special == editor::SpecialKey::Down
                                           ? (vcsSwitchBranchSelection_ + 1) % ranked.size()
                                           : (vcsSwitchBranchSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshVcsSwitchBranchStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_       = kNoHistoryIndex;
        vcsSwitchBranchSelection_ = 0;
        RefreshVcsSwitchBranchStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// dropdown-path-completion follow-up: RefreshSwitchToBufferStatus's own
// shape, over editor::acp::AcpAgentNames() (a static configured list --
// same "no create-new case" reasoning as switch-to-buffer above).
void BufferView::RefreshAcpAgentNameStatus() {
    const std::vector<std::string> candidates = editor::acp::AcpAgentNames();
    const std::vector<std::string> ranked      = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
    acpAgentNameSelection_ = ranked.empty() ? 0 : std::min(acpAgentNameSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, acpAgentNameSelection_)));
    }
}

void BufferView::HandleAcpAgentNameKey(const editor::KeyChord& chord) {
    const std::vector<std::string> candidates = editor::acp::AcpAgentNames();

    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("acp-agent-name", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No agent matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string selected = ranked[std::min(acpAgentNameSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        if (!acpManager_) {
            statusMessage_ = "No ACP manager available.";
        }
        else if (text::Buffer* buffer = acpManager_->StartSession(selected)) {
            activeBuffer_.Set(*buffer);
            statusMessage_.clear();
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Start ACP session cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "acp-agent-name")) {
        acpAgentNameSelection_ = 0;
        RefreshAcpAgentNameStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(candidates, prompt_->Text());
        if (!ranked.empty()) {
            acpAgentNameSelection_ = chord.Special == editor::SpecialKey::Down
                                        ? (acpAgentNameSelection_ + 1) % ranked.size()
                                        : (acpAgentNameSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshAcpAgentNameStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_     = kNoHistoryIndex;
        acpAgentNameSelection_ = 0;
        RefreshAcpAgentNameStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// named-projects follow-up: the shared tail of switch-project/open-project
// once a target root is known -- see this method's own header-comment
// contract in BufferView.h.
void BufferView::ActivateProjectAndReport(const std::filesystem::path& root) {
    switch (editor::ActivateProjectRoot(root)) {
        case editor::ProjectActivationOutcome::OpenedInNewTab:
            statusMessage_ = "Opened " + root.string() + " in a new tab.";
            return;
        case editor::ProjectActivationOutcome::RanCustomCommand:
            statusMessage_ = "Opened " + root.string() + " via the configured open command.";
            return;
        case editor::ProjectActivationOutcome::ReplacingInPlace:
            // HandleConfirmQuitKey's own 'y' branch: a visible message
            // before quitting, then eventLoop_->Exit() directly -- ned's
            // own main.cpp performs the actual execv() once every local
            // there (this BufferView included) has been destroyed.
            statusMessage_ = "Switching to " + root.string() + "...";
            if (eventLoop_) {
                eventLoop_->Exit();
            }
            return;
        case editor::ProjectActivationOutcome::Failed:
            statusMessage_ = "Could not switch to " + root.string() +
                             " -- no terminal detected, no configured open command, and this "
                             "process's own executable path couldn't be resolved.";
            return;
        case editor::ProjectActivationOutcome::RootMissing:
            statusMessage_ = root.string() + " no longer exists.";
            return;
    }
}

// editor-ergonomics follow-up: same picker shape again, over
// bookmarkCandidates_ (Editor/Bookmark.h's sorted name list). Enter jumps
// (bookmarkPromptAction_ == Jump) or deletes (== Delete) the selected name.
void BufferView::RefreshBookmarkJumpStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(bookmarkCandidates_, prompt_->Text());
    bookmarkSelection_                    = ranked.empty() ? 0 : std::min(bookmarkSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, bookmarkSelection_)));
    }
}

void BufferView::HandleBookmarkJumpKey(const editor::KeyChord& chord) {
    const bool isDelete = (bookmarkPromptAction_ == BookmarkPromptAction::Delete);

    if (chord.Special == editor::SpecialKey::Enter) {
        promptHistory_.Record("bookmark-jump", prompt_->Text());

        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(bookmarkCandidates_, prompt_->Text());

        if (ranked.empty()) {
            statusMessage_ = "No bookmark matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        const std::string selected = ranked[std::min(bookmarkSelection_, ranked.size() - 1)];
        EndInteractiveSession();

        if (isDelete) {
            editor::DeleteBookmark(selected);
            editor::SaveBookmarks();
            statusMessage_ = "Deleted bookmark: " + selected;
            return;
        }

        const std::optional<editor::Bookmark> mark = editor::FindBookmark(selected);
        if (!mark) {
            statusMessage_ = "Bookmark \"" + selected + "\" no longer exists";
            return;
        }
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(mark->path);
            PushJumpMark();
            activeBuffer_.Set(opened);
            opened.SetPoint(opened.ByteOffsetForLineAndColumn(mark->line, mark->column, static_cast<std::size_t>(editor::TabWidth())));
            statusMessage_ = "Bookmark: " + selected;
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = std::string(isDelete ? "Delete bookmark" : "Jump to bookmark") + " cancelled.";
        EndInteractiveSession();
        return;
    }

    if (TryNavigatePromptHistory(chord, "bookmark-jump")) {
        bookmarkSelection_ = 0;
        RefreshBookmarkJumpStatus();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(bookmarkCandidates_, prompt_->Text());
        if (!ranked.empty()) {
            bookmarkSelection_ = chord.Special == editor::SpecialKey::Down
                                    ? (bookmarkSelection_ + 1) % ranked.size()
                                    : (bookmarkSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshBookmarkJumpStatus();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_ = kNoHistoryIndex;
        bookmarkSelection_  = 0;
        RefreshBookmarkJumpStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

// rich-theme-set follow-up (Phase 1) -- see the declarations' own doc
// comments in BufferView.h for the session's overall shape.
void BufferView::RefreshSelectThemeStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(selectThemeCandidates_, prompt_->Text());
    selectThemeSelection_                 = ranked.empty() ? 0 : std::min(selectThemeSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, selectThemeSelection_)));
    }
}

void BufferView::ApplySelectedThemePreview() {
    if (!themeApplier_) {
        return;
    }
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(selectThemeCandidates_, prompt_->Text());
    if (ranked.empty()) {
        // Nothing highlighted to preview -- show what the session started
        // on rather than leaving whichever candidate was last previewed.
        if (themeBeforePreview_) {
            themeApplier_(*themeBeforePreview_);
        }
        return;
    }
    const std::string& selected = ranked[std::min(selectThemeSelection_, ranked.size() - 1)];
    if (selected == kCurrentThemeLabel) {
        // select-theme-current-row follow-up: resolved against the
        // snapshot, not ThemeByName() -- see kCurrentThemeLabel's own doc
        // comment for why a named lookup here would be destructive.
        if (themeBeforePreview_) {
            themeApplier_(*themeBeforePreview_);
        }
        return;
    }
    if (const auto named = ThemeByName(selected)) {
        themeApplier_(*named);
    }
}

void BufferView::HandleSelectThemeKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(selectThemeCandidates_, prompt_->Text());

        if (ranked.empty()) {
            // No candidate was showing either (ApplySelectedThemePreview's
            // own empty-ranked branch already restored the snapshot when
            // the list emptied), so this is a plain report-and-leave.
            statusMessage_ = "No theme matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }

        // Usually the highlighted candidate is already applied (every
        // selection/rank change previews), but not always: an immediate
        // Enter on a fresh session never previewed anything. Applying
        // explicitly covers that and is a no-op re-apply otherwise.
        const std::string selected = ranked[std::min(selectThemeSelection_, ranked.size() - 1)];
        if (selected == kCurrentThemeLabel) {
            // select-theme-current-row follow-up: committing "Current
            // theme" leaves everything exactly as it already is -- no
            // ThemeByName() lookup (there's no registry entry by this
            // name), and no variables.json write, since the persisted
            // base theme name (if any) already correctly describes what's
            // active; overwriting it with this synthetic label would
            // break next launch's own theme resolution.
            if (themeBeforePreview_) {
                themeApplier_(*themeBeforePreview_);
            }
            statusMessage_ = "Theme unchanged.";
            EndInteractiveSession();
            return;
        }
        if (const auto named = ThemeByName(selected)) {
            themeApplier_(*named);
        }
        // variables-store follow-up: a committed pick is remembered across
        // runs ($XDG_STATE_HOME/ned/variables.json) as the *base* theme --
        // preview and cancel deliberately never persist anything, and
        // init.janet's (ned/theme-set ...) overrides still apply over this
        // at startup (see main.cpp's precedence comment).
        editor::SetVariable("theme", selected);
        statusMessage_ = "Theme: " + selected;
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        if (themeApplier_ && themeBeforePreview_) {
            themeApplier_(*themeBeforePreview_);
        }
        statusMessage_ = "Theme selection cancelled.";
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(selectThemeCandidates_, prompt_->Text());
        if (!ranked.empty()) {
            selectThemeSelection_ = chord.Special == editor::SpecialKey::Down
                                       ? (selectThemeSelection_ + 1) % ranked.size()
                                       : (selectThemeSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshSelectThemeStatus();
        ApplySelectedThemePreview();
        return;
    }

    // Same "typing re-snaps to the top match" reasoning as
    // HandleExecuteCommandKey -- see that method's own comment.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        selectThemeSelection_ = 0;
        RefreshSelectThemeStatus();
        ApplySelectedThemePreview();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::CloseBufferNow(text::Buffer& buffer) {
    const bool        wasActive = (&activeBuffer_.Get() == &buffer);
    const std::string name      = buffer.Name();

    text::Buffer* replacement = nullptr;
    if (wasActive) {
        // MRU-close follow-up: land on the buffer the user most recently
        // left, not the first tab -- the ActiveBuffer on-change hook (see
        // Pane's constructor) keeps this order current. Falls back to
        // list order for buffers never activated (e.g. session-restored
        // and never visited).
        replacement = bufferList_.MostRecentlyUsedBuffer(&buffer);
        if (replacement == nullptr) {
            for (const auto& candidate : bufferList_.Buffers()) {
                if (candidate.get() != &buffer) {
                    replacement = candidate.get();
                    break;
                }
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
    // session-persistence slice 1: a stored viewport for this buffer wins
    // over whatever topLine_ the previous buffer left behind -- this seam
    // fires exactly once per buffer switch (and on a pane's very first
    // Paint, covering the startup buffer), which is what makes restored
    // scroll positions land here and nowhere else. Clamped by MaxTopLine()
    // against content that shrank since the place was recorded (an outside
    // edit between runs); ScrollToShowPoint() below then still guarantees
    // point is visible, so a topLine/point pair that somehow disagrees
    // resolves in point's favor, never a blank or point-less view.
    if (const auto place = editor::StoredFilePlaceFor(buffer); place && place->topLine) {
        topLine_ = std::min(*place->topLine, MaxTopLine());
    }
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
    ScrollToShowOffset(activeBuffer_.Get().Point());
    ScrollToShowPointHorizontally();
}

void BufferView::ScrollToShowOffset(std::size_t offset) {
    const text::ITextStorage& content   = activeBuffer_.Get().Content();
    const std::size_t pointLine = content.ByteOffsetToLine(offset);

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
    // multi-cursor-round-2 follow-up: no ScrollToShowPointHorizontally()
    // call here -- it always reads activeBuffer_.Get().Point() directly, so
    // calling it for an arbitrary secondary-cursor offset would scroll
    // horizontally to the *primary's* column instead, wrong for exactly the
    // case this parameterized form exists for. ScrollToShowPoint() (below)
    // still gets both, since it always wants "make sure point itself is
    // fully visible."
}

void BufferView::ScrollToShowPointHorizontally() {
    if (EffectiveWrapLines()) {
        return; // a wrapped line never extends past the viewport width -- nothing to scroll
    }

    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::ITextStorage&   content     = buffer.Content();
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
    return editor::EffectiveWrapLines(activeBuffer_.Get().Path(), activeBuffer_.Get().Name(), mode_);
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

void BufferView::SetVcsPanel(VcsPanel* panel) {
    vcsPanel_ = panel;
}

void BufferView::RequestVcsAction(VcsPanelAction action) {
    switch (action) {
        case VcsPanelAction::Commit:
            BeginVcsCommitMessage();
            return;
        case VcsPanelAction::SwitchBranch:
            BeginVcsSwitchBranchPrompt();
            return;
        case VcsPanelAction::CreateBranch:
            BeginVcsCreateBranchPrompt();
            return;
    }
}

void BufferView::SetThemeApplier(std::function<void(const Theme&)> applier) {
    themeApplier_ = std::move(applier);
}

void BufferView::SetMinimap(Minimap* minimap, Widget* scrollColumn) {
    minimap_             = minimap;
    minimapScrollColumn_ = scrollColumn;
}

void BufferView::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
}

void BufferView::SetTaskRunner(editor::tasks::TaskRunner* taskRunner) {
    taskRunner_ = taskRunner;
}

void BufferView::SetProjectUndo(editor::ProjectUndoManager* projectUndo) {
    projectUndo_ = projectUndo;
}

void BufferView::SetTestRunner(editor::testrun::TestRunner* testRunner) {
    testRunner_ = testRunner;
}

void BufferView::SetDapManager(editor::dap::DapManager* dapManager) {
    dapManager_ = dapManager;
}

void BufferView::SetAcpManager(editor::acp::AcpManager* acpManager) {
    acpManager_ = acpManager;
}

void BufferView::SetSurfaceUnseenLogEntries(bool enabled) {
    surfaceUnseenLogEntries_ = enabled;
}

void BufferView::SetJanetEnvironment(const janet::Environment* janetEnv) {
    janetEnv_ = janetEnv;
}

void BufferView::ShowAcpPermissionPrompt(const editor::acp::AcpManager::PermissionPrompt& prompt) {
    pendingAcpPermissionOptions_ = prompt.options;
    acpPermissionSelection_      = 0;
    inputMode_                   = InputMode::AcpPermissionPrompt;
    acpPermissionDescription_    = prompt.description;
    RefreshAcpPermissionPromptStatus();
}

void BufferView::RefreshAcpPermissionPromptStatus() {
    std::string status = acpPermissionDescription_ + ": ";
    for (std::size_t i = 0; i < pendingAcpPermissionOptions_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == acpPermissionSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingAcpPermissionOptions_[i].name + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleAcpPermissionPromptKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        if (acpManager_) {
            acpManager_->CancelPermissionPrompt();
        }
        statusMessage_ = "Permission request cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        acpPermissionSelection_ = (acpPermissionSelection_ + 1) % pendingAcpPermissionOptions_.size();
        RefreshAcpPermissionPromptStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        acpPermissionSelection_ = (acpPermissionSelection_ + pendingAcpPermissionOptions_.size() - 1) % pendingAcpPermissionOptions_.size();
        RefreshAcpPermissionPromptStatus();
        return;
    }
    std::size_t chosen = acpPermissionSelection_;
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= pendingAcpPermissionOptions_.size()) {
            return; // out of range -- stay in the selection list
        }
        chosen = index;
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the selection list
    }

    const editor::acp::AcpManager::PermissionOption& option = pendingAcpPermissionOptions_[chosen];
    if (acpManager_) {
        acpManager_->ResolvePermissionPrompt(option.optionId);
    }
    statusMessage_ = "Selected \"" + option.name + "\".";
    EndInteractiveSession();
}

void BufferView::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
}

bool BufferView::InSelection(std::size_t byteOffset) const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.HasMark()) {
        const auto [start, end] = buffer.Region();
        if (byteOffset >= start && byteOffset < end) {
            return true;
        }
    }
    // Multi-cursor phase: each secondary cursor's own selection highlights
    // through the exact same overlay -- per-codepoint x per-secondary is
    // fine at real cursor counts (a handful), the same reasoning
    // currentLineDiagnosticSpans' own per-cell loop already relies on.
    for (const auto& cursor : buffer.SecondaryCursors()) {
        if (!cursor.mark) {
            continue;
        }
        const std::size_t start = std::min(cursor.point, *cursor.mark);
        const std::size_t end   = std::max(cursor.point, *cursor.mark);
        if (byteOffset >= start && byteOffset < end) {
            return true;
        }
    }
    return false;
}

bool BufferView::IsSecondaryCursorAt(std::size_t byteOffset) const {
    for (const auto& cursor : activeBuffer_.Get().SecondaryCursors()) {
        if (cursor.point == byteOffset) {
            return true;
        }
    }
    return false;
}

Brush BufferView::ResolvedBrush(editor::SyntaxClass cls, editor::CaptureId captureId) const {
    // Flush on any style change (ned/set-syntax-* and ned/set-capture-*
    // share one generation, SyntaxTheme.h) -- one locked counter read per
    // call, versus the several locked map lookups plus a name lookup the
    // capture-aware BrushFor does on a miss.
    const std::size_t generation = editor::SyntaxThemeGeneration();
    if (generation != brushCacheGeneration_ || theme_.name != brushCacheThemeName_) {
        // The name check covers select-theme: the applier (main.cpp)
        // assigns a whole new Theme into the one object theme_ refers to,
        // which moves no SyntaxTheme generation -- every real theme (and
        // both preview directions) carries a distinct name, and nothing
        // mutates a live Theme's fields under an unchanged name today.
        brushCache_.clear();
        brushCacheGeneration_ = generation;
        brushCacheThemeName_  = theme_.name;
    }

    const std::uint32_t key = (static_cast<std::uint32_t>(cls) << 16) | captureId;
    if (const auto it = brushCache_.find(key); it != brushCache_.end()) {
        return it->second;
    }
    const Brush brush = theme_.BrushFor(cls, captureId);
    brushCache_.emplace(key, brush);
    return brush;
}

bool BufferView::InActiveSnippetField(std::size_t byteOffset) const {
    if (inputMode_ != InputMode::Snippet || !snippetSession_) {
        return false;
    }
    // Reads the *active* buffer's ranges -- if it isn't the session buffer
    // (a transient mid-frame mismatch; the session ends on a real switch)
    // its range set is empty and this simply reports false.
    const auto range = snippetSession_->ActiveFieldRange(activeBuffer_.Get());
    return range && byteOffset >= range->first && byteOffset < range->second;
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
