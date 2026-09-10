//
// File-local helpers shared by the several translation units that make up
// BufferView -- see Docs/BufferViewDecomposition.md.
//
// These were fourteen separate anonymous namespaces inside a single 16k-line
// BufferView.cpp. Splitting that file left most of them with consumers in more
// than one part, so they live here in `detail` and are `inline` rather than
// internal-linkage: identical codegen, still not part of any public surface.
// Source order is preserved, which is what keeps the one forward declaration
// here (FormatDebugVariableLine) ahead of its definition.
//
// The include block below is the one the original BufferView.cpp carried, kept
// whole so the split itself moved no includes around. It is Phase 0 scaffolding:
// as each cluster becomes a real class the includes it needs go with it, and this
// list shrinks to what the remaining helpers actually use.
//

#ifndef NED_UI_BUFFERVIEWINTERNAL_H
#define NED_UI_BUFFERVIEWINTERNAL_H

#include "UI/BufferView.h"
#include "UI/BufferView/RenderTypes.h"
#include "UI/Compositing.h"
#include "UI/ThemePaints.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include "Editor/Acp/Config.h"
#include "Editor/Bookmark.h"
#include "Editor/BufferSave.h"
#include "Editor/Clipboard.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/Coverage/Config.h"
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
#include "Editor/Lsp/EditApply.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/MassifOutputParser.h"
#include "Editor/MassifReportBuffer.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/MultibufferLimits.h"
#include "Editor/MultibufferSearchSettings.h"
#include "Editor/NextError.h"
#include "Editor/NodeModules.h"
#include "Editor/Org.h"
#include "Editor/OrgCapture.h"
#include "Editor/Php.h"
#include "Editor/PointerGraphNode.h"
#include "Editor/Project/Agenda.h"
#include "Editor/Project/FileOps.h"
#include "Editor/Project/Registry.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Search.h"
#include "Editor/Project/Settings.h"
#include "Editor/Project/Switch.h"
#include "Editor/Project/Tree.h"
#include "Editor/Project/Undo.h"
#include "Editor/RecentFiles.h"
#include "Editor/Rectangle.h"
#include "Editor/RegexPattern.h"
#include "Editor/RelativeLineNumberSettings.h"
#include "Editor/Repl/Config.h"
#include "Editor/ScratchPad.h"
#include "Editor/Session.h"
#include "Editor/Sparkline.h"
#include "Editor/StickyScroll.h"
#include "Editor/StickyScrollSettings.h"
#include "Editor/SyntaxTheme.h"
#include "Editor/TabWidth.h"
#include "Editor/TestRun/Config.h"
#include "Editor/TestRun/TestResultsBuffer.h"
#include "Editor/ToolchainIncludePaths.h"
#include "Editor/Variables.h"
#include "Editor/Vcs/DiffPatch.h"
#include "Editor/Vim/Settings.h"
#include "Editor/WhichKeySettings.h"
#include "Editor/WhitespaceSettings.h"
#include "Editor/WrapOverrides.h"
#include "Janet/Environment.h"
#include "Text/BinaryDetect.h"
#include "Text/Grapheme.h"
#include "Text/Utf8.h"
#include "UI/Border.h"
#include "UI/EchoArea.h"
#include "UI/KeyTranslation.h"
#include "UI/ThemeFile.h"
#include "UI/ThemeRegistry.h"

namespace ned::ui::detail {

// Moved to BufferView/RenderTypes.h now that painting, the viewport and the
// per-line render state all name them; pulled back in here so the helpers below
// keep spelling them unqualified.
using bufferview::RenderedInlayHint;
using bufferview::RenderedLink;
using bufferview::WrapSegment;

// Plain, non-modifier printable input: the only kind of chord that should
// feed into a query string during isearch/query-replace/prompt text entry.
inline bool IsPlainCharacter(const editor::KeyChord& chord) {
    return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
}

inline bool IsQuit(const editor::KeyChord& chord) {
    return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
}

// Double/triple-click word/line selection: same window ProjectSidebar's
// own double-click-to-open uses.
constexpr std::chrono::milliseconds kDoubleClickWindow{400};

// ASCII alphanumeric + underscore, deliberately not Unicode-aware --
// mirrors Buffer.cpp's own (private) IsWordCodepoint used by
// MoveForwardWord/MoveBackwardWord.
inline bool IsWordCodepointForClick(char32_t codepoint) {
    return (codepoint >= U'a' && codepoint <= U'z') || (codepoint >= U'A' && codepoint <= U'Z') ||
           (codepoint >= U'0' && codepoint <= U'9') || codepoint == U'_';
}

// Expands a click offset to the bounds of the contiguous word/non-word
// run it falls in -- e.g. double-clicking mid-identifier selects the
// whole identifier, double-clicking mid-whitespace selects the whole
// run of whitespace.
inline std::pair<std::size_t, std::size_t> WordBoundsAtOffset(const text::ITextStorage& content, std::size_t offset) {
    const std::size_t total = content.ByteLength();
    if (total == 0) {
        return {0, 0};
    }
    const std::size_t probe  = offset < total ? offset : content.PreviousCodepointBoundary(offset);
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
inline bool IsWindowManagementRequest(editor::InteractiveRequest request) {
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

inline GutterSelection ClassifyGutterSelection(const text::Buffer& buffer, std::size_t lineStart,
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
inline std::string LongestCommonPrefix(const std::vector<std::string>& strings) {
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

inline std::string JoinCandidates(const std::vector<std::string>& candidates) {
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
struct CandidatePopupWindow {
    std::size_t start;
    std::size_t end;
};

// The sliding-window math BuildFuzzyCandidatePopupModel renders from,
// factored out so ResolveFuzzyCandidateRowIndex below can reproduce the
// exact same window a click is landing on without duplicating it.
// `total` must be nonzero; `selected` is clamped internally.
inline CandidatePopupWindow ComputeCandidatePopupWindow(std::size_t selected, std::size_t total) {
    selected = std::min(selected, total - 1);

    std::size_t       windowStart = selected;
    std::size_t       windowEnd   = selected + 1;
    const std::size_t maxRows     = std::min(kMaxPopupRows, total);
    while (windowEnd - windowStart < maxRows) {
        if (windowEnd < total) {
            ++windowEnd;
        }
        else if (windowStart > 0) {
            --windowStart;
        }
        else {
            break;
        }
    }
    return CandidatePopupWindow{.start = windowStart, .end = windowEnd};
}

inline ListPopupModel BuildFuzzyCandidatePopupModel(const std::string& title, const std::vector<std::string>& ranked,
                                                    std::size_t                                           selected,
                                                    const std::function<std::string(const std::string&)>& display = nullptr) {
    ListPopupModel model;
    model.title = title;
    if (ranked.empty()) {
        return model;
    }
    selected = std::min(selected, ranked.size() - 1);

    const auto [windowStart, windowEnd] = ComputeCandidatePopupWindow(selected, ranked.size());

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

// click-to-activate follow-up: maps a raw row index from ListPopup's own
// click handler (which knows nothing about the "N more above/below"
// synthetic rows BuildFuzzyCandidatePopupModel splices in) back to a real
// index into the `total`-sized ranked/candidate list it was built from --
// `selected` must be the same value the popup was last rendered with, so
// this reproduces that exact window. A click landing on a synthetic
// divider row (or past the end -- a stale click racing a just-changed
// list) resolves to nullopt, not a clamped guess.
inline std::optional<std::size_t> ResolveFuzzyCandidateRowIndex(std::size_t rowIndex, std::size_t selected,
                                                                std::size_t total) {
    if (total == 0) {
        return std::nullopt;
    }
    const auto [windowStart, windowEnd] = ComputeCandidatePopupWindow(selected, total);
    const std::size_t offset            = windowStart > 0 ? 1 : 0;
    if (rowIndex < offset) {
        return std::nullopt; // the "more above" divider row
    }
    const std::size_t withinWindow = rowIndex - offset;
    if (withinWindow >= windowEnd - windowStart) {
        return std::nullopt; // the "more below" divider row, or past it
    }
    return windowStart + withinWindow;
}

// dropdown-path-completion follow-up: turns an accumulated
// text::CompleteFilePath candidate ("src/editor/" or
// "src/editor/BufferView.cpp") into just its last segment ("editor/" or
// "BufferView.cpp"), independent of how deep the accumulated prefix is --
// the `display` transform RefreshPathCompletionPopup passes to
// BuildFuzzyCandidatePopupModel for FindFile/OpenProjectPath rows.
inline std::string MaskPathCandidateToLastSegment(const std::string& candidate) {
    const bool        isDirectory = candidate.ends_with('/');
    const std::string trimmed     = isDirectory ? candidate.substr(0, candidate.size() - 1) : candidate;
    std::string       segment     = std::filesystem::path(trimmed).filename().string();
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
inline std::string BuildSymbolLabel(const editor::lsp::Manager::SymbolResult& symbol, bool includePath) {
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
inline std::string BuildHierarchyRowLabel(const editor::lsp::Manager::ResolvedHierarchyItem& resolved) {
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
inline bool IsUnprintableControl(char32_t cp) {
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

// wrap-continuation-indicator follow-up: painted in the one column
// ComputeWrapSegments' own caller deliberately reserves at the right
// edge of every row when wrap is on -- U+21B5, the same glyph printed
// on a physical Return/Enter keycap, so it reads unambiguously as "this
// line keeps going" without being mistaken for real content.
constexpr char32_t kWrapContinuationIndicator = U'↵';

// trailing-blank-line-gutter follow-up: painted after the buffer's true
// last line when it has content but no trailing newline follows it --
// U+00AC NOT SIGN, distinct from kFoldEllipsis/kTruncationIndicator and
// not a glyph real code/prose is likely to end a line with.
constexpr char32_t kNoTrailingNewlineIndicator = U'¬';

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
inline Color IndentGuideColor(const Theme& theme, int displayColumn, int tabWidth) {
    if (!editor::IndentGuideDepthColorsEnabled() || theme.indentGuideDepthPalette.empty() || tabWidth <= 0) {
        return theme.indentGuideForeground;
    }
    const std::size_t level = static_cast<std::size_t>(displayColumn / tabWidth - 1);
    return theme.indentGuideDepthPalette[level % theme.indentGuideDepthPalette.size()];
}

inline char32_t HexDigit(char32_t nibble) {
    return (nibble < 10) ? (U'0' + nibble) : (U'A' + (nibble - 10));
}

// Columns a single codepoint occupies when rendered: editor::TabWidth()
// for a tab, 4 (open bracket + 2 hex digits + close bracket) for a
// binary placeholder, 1 for every ordinary glyph. Shared by Paint()'s
// render loop and VisualColumn below so the two can never disagree
// about column math.
inline int CodepointColumns(char32_t cp) {
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
inline int DisplayColumns(const std::string& text) {
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


// Filters org::ParseLinks's whole-buffer result down to just the links
// fully inside [lineStart, lineEnd) that should render collapsed --
// called once per line, the same "filter once, consult per-codepoint"
// shape SpansForLine already establishes for syntax highlighting.
inline std::vector<RenderedLink> LinksForLine(const std::vector<editor::org::Link>& links, std::size_t lineStart,
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
inline const RenderedLink* LinkStartingAt(const std::vector<RenderedLink>& links, std::size_t offset) {
    for (const RenderedLink& link : links) {
        if (link.startByte == offset) {
            return &link;
        }
    }
    return nullptr;
}


inline std::vector<RenderedInlayHint> InlayHintsForLine(const std::vector<editor::lsp::Manager::ResolvedInlayHint>& hints,
                                                        std::size_t lineStart, std::size_t lineEnd) {
    std::vector<RenderedInlayHint> rendered;
    for (const editor::lsp::Manager::ResolvedInlayHint& hint : hints) {
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
inline const RenderedInlayHint* InlayHintStartingAt(const std::vector<RenderedInlayHint>& hints, std::size_t offset) {
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
inline std::optional<int> VisualColumn(const text::ITextStorage& content, std::size_t lineStart, std::size_t byteOffset,
                                       int maxColumns, const std::vector<RenderedLink>& lineLinks = {},
                                       const std::vector<RenderedInlayHint>& lineHints = {}) {
    int         col    = 0;
    std::size_t offset = lineStart;
    while (offset < byteOffset) {
        if (col >= maxColumns) {
            return std::nullopt;
        }
        // An inlay hint renders as extra cells *before* the real character
        // still at this offset, so every hint strictly before byteOffset
        // pushes point that much further right. A hint anchored exactly at
        // byteOffset does not: it renders after the cursor, which is why the
        // loop condition stops before it (matching where the painter puts
        // the cursor, and where VS Code puts it too).
        //
        // Leaving this out was a real bug: the cursor drew N columns left of
        // the character it was on, N being the width of every hint earlier
        // in the line, so an Enter split appeared in the wrong place and the
        // horizontal-scroll decision under-estimated how far right point
        // really was.
        if (const RenderedInlayHint* hint = InlayHintStartingAt(lineHints, offset)) {
            col += DisplayColumns(hint->label);
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

inline std::size_t ByteOffsetForColumnInLine(const text::ITextStorage& content, std::size_t lineStart, std::size_t lineEnd,
                                             std::size_t targetColumn, int tabWidth,
                                             const std::vector<RenderedLink>&      lineLinks,
                                             const std::vector<RenderedInlayHint>& lineHints = {}) {
    std::size_t offset       = lineStart;
    std::size_t visualColumn = 0;
    std::size_t steps        = 0;
    while (offset < lineEnd && visualColumn < targetColumn) {
        // VisualColumn's inverse has to skip the same virtual cells, or a
        // click lands on a different character than the one under the mouse
        // by the total width of the hints to its left.
        if (const RenderedInlayHint* hint = InlayHintStartingAt(lineHints, offset)) {
            const std::size_t hintColumns = static_cast<std::size_t>(DisplayColumns(hint->label));
            if (targetColumn < visualColumn + hintColumns) {
                return offset; // the click landed on the hint itself -- the real character it annotates
            }
            visualColumn += hintColumns;
        }
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
inline bool IsWrapBreakWhitespace(char32_t cp) {
    return cp == U' ' || cp == U'\t';
}


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
inline std::vector<WrapSegment> ComputeWrapSegments(const text::ITextStorage& content, std::size_t lineStart, std::size_t lineEnd,
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

// wrap-continuation-indicator follow-up: the one true source every
// consumer of wrap-segment row/column math shares -- Paint()'s own
// render loop, RowsForLine's row-count cache, CursorPosition, and
// ByteOffsetForPoint's click resolution. All four used to call
// ComputeWrapSegments directly with their own "full width" value; only
// Paint() knew to knock one column off once a line is confirmed to
// wrap (reserving the row's own right edge for
// kWrapContinuationIndicator), so the other three would report
// different segment boundaries than what was actually painted --
// observed live as an undercounted row total (MaxTopLine/
// ScrollToShowPoint couldn't scroll far enough to reveal a wrapped
// line's true last row), a cursor drawn past its real character (the
// column math still assumed the wider, unreserved layout), and motion
// that appeared to stop dead once the two disagreed enough. `fullWidth`
// here must be the same "size().width - gutterWidth" value at every
// call site -- see ComputeWrapSegments's own doc comment for why
// reducing width can only ever add segments, never remove one, so this
// can't oscillate.
inline std::vector<WrapSegment> ComputeWrappedLineSegments(const text::ITextStorage& content, std::size_t lineStart,
                                                           std::size_t lineEnd, int fullWidth,
                                                           const std::vector<RenderedLink>& lineLinks) {
    std::vector<WrapSegment> segments = ComputeWrapSegments(content, lineStart, lineEnd, fullWidth, lineLinks);
    if (segments.size() > 1) {
        const int reservedWidth = std::max(1, fullWidth - 1);
        segments                = ComputeWrapSegments(content, lineStart, lineEnd, reservedWidth, lineLinks);
    }
    return segments;
}

// Filters mode_.highlight's whole-buffer HighlightSpan list down to just
// the spans overlapping [lineStart, lineEnd) -- called once per visible
// row from Paint(), *not* once per rendered codepoint, so SpanAtOffset
// below only ever scans a small, per-line list rather than the whole
// file's spans on every single codepoint.
inline std::vector<editor::HighlightSpan> SpansForLine(const std::vector<editor::HighlightSpan>& spans,
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
inline editor::HighlightSpan SpanAtOffset(const std::vector<editor::HighlightSpan>& spans, std::size_t byteOffset) {
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
inline bool IsWordCodepoint(char32_t codepoint) {
    return (codepoint < 0x80) && (std::isalnum(static_cast<unsigned char>(codepoint)) != 0 || codepoint == U'_');
}

inline std::size_t WordPrefixStart(const text::ITextStorage& content, std::size_t point) {
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
inline std::string_view MouseEventTag(const MouseEvent& mouse) {
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

// Higher rank = more severe = wins when two diagnostics start on the
// same line. Matches LSP's own severity ordering (1=Error is the most
// severe, 4=Hint the least), just inverted to a "bigger number wins"
// comparison for MostSevere below.
inline int DiagnosticSeverityRank(text::Buffer::Diagnostic::Severity severity) {
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
inline DiagnosticGlyph DiagnosticGlyphFor(text::Buffer::Diagnostic::Severity severity) {
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
inline const char* SymbolGlyphFor(editor::SymbolKind kind) {
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
inline std::optional<editor::SymbolKind> CompletionKindBucket(int lspKind) {
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
// test-runner-gaps follow-up: nullopt = discovered but not run yet, the
// clickable "run this test" affordance (only ever produced when a
// filter command is configured -- see EnsureTestGutterCache).
inline const char* TestGlyphFor(const std::optional<editor::testrun::TestResult::Status>& status) {
    if (!status) {
        return "▸"; // BLACK RIGHT-POINTING SMALL TRIANGLE -- the run affordance
    }
    switch (*status) {
        case editor::testrun::TestResult::Status::Passed:
            return "✓"; // CHECK MARK
        case editor::testrun::TestResult::Status::Failed:
            return "✗"; // BALLOT X, the diagnostic column's own error glyph
        case editor::testrun::TestResult::Status::Skipped:
            return "−"; // MINUS SIGN
    }
    return " "; // unreachable, same convention as DiagnosticGlyphFor above
}

inline Color TestStatusColor(const Theme& theme, const std::optional<editor::testrun::TestResult::Status>& status) {
    if (!status) {
        // An affordance, not a result -- deliberately quiet, so it takes the
        // gutter's own recessive colour rather than a status hue.
        return theme.lineNumberForeground;
    }
    switch (*status) {
        case editor::testrun::TestResult::Status::Passed:
            return theme.successForeground;
        case editor::testrun::TestResult::Status::Failed:
            return theme.diagnosticError;
        case editor::testrun::TestResult::Status::Skipped:
            return theme.diagnosticWarning;
    }
    return theme.successForeground; // unreachable
}

inline Color DiagnosticSeverityColor(const Theme& theme, text::Buffer::Diagnostic::Severity severity) {
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
inline Color BlameHashColor(const Theme& theme, const std::string& date) {
    constexpr int kBlameMaxAgeDays = 365;

    std::istringstream    stream(date);
    std::chrono::sys_days parsed;
    stream >> std::chrono::parse("%Y-%m-%d", parsed);
    if (stream.fail()) {
        return theme.blameOldForeground;
    }

    const auto  now     = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const auto  ageDays = std::chrono::duration_cast<std::chrono::days>(now - parsed).count();
    const float t       = std::clamp(static_cast<float>(ageDays) / static_cast<float>(kBlameMaxAgeDays), 0.0f, 1.0f);
    return Color::Interpolate(t, theme.blameRecentForeground, theme.blameOldForeground);
}

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

// peek-definition follow-up: how many lines of context to pull around the
// target line -- asymmetric (more after than before) since a definition's own
// doc comment/attributes usually sit directly above it and the body the user
// actually wants to glance at follows. Kept small enough that the popup never
// needs to scroll -- HandlePeekDefinitionKey has no scroll handling, by design
// (a peek is meant to be a glance; Enter opens the real buffer for anything
// needing more).
constexpr std::size_t kPeekContextLinesBefore = 4;
constexpr std::size_t kPeekContextLinesAfter  = 10;

// right-click-context-menu follow-up: short menu labels for a fixed,
// closed set of command names -- Command::Docstring() is a full
// sentence meant for M-x/help text ("Kill (cut) the region between
// point and mark into the kill ring."), not a menu row. Resolved once,
// at push time, into ContextMenuEntry::label -- a code-action entry
// carries its own dynamic title instead, so this map only ever needs
// to cover the fixed set of named commands the menu can offer.
inline std::string StaticContextMenuLabel(const std::string& commandName) {
    static const std::unordered_map<std::string, std::string> kLabels = {
        {"kill-region", "Cut"},
        {"kill-ring-save", "Copy"},
        {"yank", "Paste"},
        {"lsp-goto-definition", "Go to Definition"},
        {"project-find-references", "Find References"},
        {"lsp-rename", "Rename Symbol"},
        {"format-buffer", "Format Buffer"},
        {"code-fold-toggle", "Toggle Fold"},
        {"dap-toggle-breakpoint", "Toggle Breakpoint"},
        {"vcs-blame-detail-at-point", "Show Blame"},
        {"vcs-stage-hunk", "Stage Hunk"},
        {"vcs-unstage-hunk", "Unstage Hunk"},
        {"vcs-revert-hunk", "Revert Hunk..."},
    };
    const auto it = kLabels.find(commandName);
    return it != kLabels.end() ? it->second : commandName;
}

// context-aware-menu-round-2 follow-up: the rule ListPopup's own
// preview-footer divider paints (Border.h's RoundedBorderGlyphs().horizontal,
// U+2500), long enough to span any reasonable popup width -- PaintRowText
// truncates the rest, the same "silently truncate" convention every other
// row already follows, so a narrower popup is harmless.
inline std::string ContextMenuDividerRule() {
    std::string rule;
    for (int i = 0; i < 40; ++i) {
        rule += "─";
    }
    return rule;
}

// snippet-variables follow-up: resolves the practical $TM_*/CLIPBOARD/
// CURRENT_*/RANDOM* subset SnippetVariables documents as supported --
// TM_CURRENT_WORD, BLOCK_COMMENT_*, LINE_COMMENT, and WORKSPACE_* are
// deliberate v1 cuts (no word-boundary helper at the Buffer level to
// reuse for the former; Mode isn't threaded into BeginSnippetExpansion
// for the latter two/three).
inline editor::SnippetVariables BuildSnippetVariables(text::Buffer& buffer) {
    editor::SnippetVariables  vars;
    const text::ITextStorage& content = buffer.Content();
    const std::size_t         point   = buffer.Point();

    if (buffer.HasMark()) {
        const auto [start, end] = buffer.Region();
        vars.selectedText       = content.Substring(start, end - start);
    }

    const std::size_t line      = content.ByteOffsetToLine(point);
    vars.lineNumber             = std::to_string(line + 1);
    vars.lineIndex              = std::to_string(line);
    const std::size_t lineStart = content.LineToByteOffset(line);
    // Bounded the same way Buffer.cpp's kMaxTabAwareColumnScan is --
    // this is a preview value for one snippet field, not worth an
    // unbounded scan on a pathologically long line.
    constexpr std::size_t kMaxLineScan = 8192;
    const std::size_t     scanEnd      = std::min(content.ByteLength(), lineStart + kMaxLineScan);
    const std::string     chunk        = content.Substring(lineStart, scanEnd - lineStart);
    const std::size_t     newline      = chunk.find('\n');
    vars.currentLine                   = newline == std::string::npos ? chunk : chunk.substr(0, newline);

    if (const auto& path = buffer.Path()) {
        vars.filename     = path->filename().string();
        vars.filenameBase = path->stem().string();
        vars.directory    = path->parent_path().string();
        vars.filepath     = path->string();
        std::error_code ec;
        const auto      relative = std::filesystem::relative(*path, editor::ProjectRoot(), ec);
        vars.relativeFilepath    = ec ? vars.filepath : relative.string();
    }

    if (const auto clip = editor::PasteFromSystemClipboard()) {
        vars.clipboard = *clip;
    }

    const auto        now     = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm           local{};
    localtime_r(&nowTime, &local);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d", local.tm_year + 1900);
    vars.year = buf;
    std::snprintf(buf, sizeof(buf), "%02d", local.tm_mon + 1);
    vars.month = buf;
    std::snprintf(buf, sizeof(buf), "%02d", local.tm_mday);
    vars.date = buf;
    std::snprintf(buf, sizeof(buf), "%02d", local.tm_hour);
    vars.hour = buf;
    std::snprintf(buf, sizeof(buf), "%02d", local.tm_min);
    vars.minute = buf;
    std::snprintf(buf, sizeof(buf), "%02d", local.tm_sec);
    vars.second = buf;

    return vars;
}

// Forward-declared here (defined below, near ShowDebugInfo) so
// HandlePromptKey's DapSetVariableValue branch -- which runs earlier in
// this file -- can reuse it rather than duplicating the "[ref:N]"/
// "[owner:M]"-marker line format.
// Debugging wishlist: hex appends a trailing "[hex]" marker -- both the
// display hint the value itself was fetched with, and (ToggleHexFormatAtPoint's
// own read of it back) the toggle's persisted state.
inline std::string FormatDebugVariableLine(const ned::editor::dap::Manager::Variable& variable, std::size_t indent, int ownerRef = 0,
                                           bool hex = false);

// Right-aligns number into a fixed kDiffLineNumberWidth-wide field, or a
// blank field of the same width if number is unset (the line doesn't
// exist on that side of the diff) -- widens rather than truncates for a
// number too large to fit, never lossy.
constexpr int kDiffLineNumberWidth = 4;

inline std::string FormatDiffLineNumber(std::optional<std::size_t> number) {
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
inline std::string FormatDiffHunkBody(const editor::vcs::DiffHunkText& hunk, std::vector<editor::multibuffer::LineTint>& tints) {
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

// org-clock-display follow-up: "H:MM", same unpadded-hour/zero-padded-
// minute shape Org.cpp's own (file-local) FormatClockEntry uses for a
// closed clock line's own "=>  H:MM" -- kept as a small local copy here
// rather than exposed from Org.h, since nothing outside that one clock-
// line-formatting call and this multibuffer excerpt formatting needs it.
inline std::string FormatClockMinutes(std::chrono::minutes minutes) {
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
inline void CollectClockedHeadlines(std::string_view bufferText, const editor::org::HeadlineNode& node,
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

// find-all-references follow-up: [start, end) of the ASCII word/
// identifier point sits inside or immediately after, or nullopt when
// point touches no word at all. Same classification Commands.cpp's own
// (anonymous-namespace-private) WordRegionAt uses for select-next-
// occurrence/select-all-occurrences -- duplicated here rather than
// shared, the same "not worth a new seam for something this small" call
// that function's own doc comment already makes, and the one
// WordPrefixStart above makes too (that one only scans backward, for a
// completion prefix -- this needs the full symmetric span).
inline std::optional<std::pair<std::size_t, std::size_t>> WordRegionAtPoint(const text::ITextStorage& content, std::size_t point) {
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
inline std::string ReadFileLine(const std::filesystem::path& path, std::size_t lineNumber) {
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

// project-replace-review follow-up: whether a result's source is a huge file
// (Text/BufferList.h's HugeFileThreshold), so an excerpt header can say so.
// Huge-file support is deliberately second-class throughout this codebase --
// it stays correct and bounded, but it isn't what shapes any fast path, and
// several operations behave differently on one (a disk-target commit routes
// through the buffer instead; project search scans it single-threaded rather
// than snapshotting it). Saying "huge" on the row is what keeps that
// difference visible instead of surprising.
inline bool LooksHugeSource(text::BufferList& bufferList, const std::filesystem::path& path) {
    if (const text::Buffer* open = bufferList.FindByPath(path)) {
        return open->Content().IsHuge();
    }
    std::error_code      ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    return !ec && size > text::HugeFileThreshold();
}

// Multibuffer-gaps follow-up: ReadFileLine's live-first, caching sibling,
// for a caller resolving *many* lines at once (find-references: one per
// resolved LSP location).
//
// Live first, always. An open buffer's own content is what the user is
// looking at, so it's what an excerpt must show -- and it's also what
// BuildMultibuffer resolves that excerpt's byte range against
// (ReadExcerptText's own rule), so reading the body from disk instead would
// hand back an excerpt whose displayed text and source range disagree the
// moment a buffer has unsaved edits. Everything downstream of that -- the
// column-preserving jump, a wgrep-style commit -- is only as good as those
// two agreeing. Resolved through the source's own bounded line index
// (LineToByteOffset is O(log n)), so this stays cheap even for a huge open
// buffer.
//
// The disk path is the fallback for the common case that a reference lives
// in a file nobody has opened. ReadFileLine reopens and re-scans the file
// for every single line -- 500 references inside one file was 500 full
// re-reads -- so this caches exactly one file's lines, the most recently
// asked-for: enough to collapse a run of same-file locations into one read
// (LSP locations arrive grouped by file in practice), while keeping the
// memory this can hold bounded by a single file rather than by the whole
// result set. A file past kMaxCachedFileBytes is never cached at all and
// falls back to ReadFileLine verbatim, so a huge unopened source file can't
// be pulled into memory line-by-line just to build a display buffer.
class FileLineReader {
  public:
    explicit FileLineReader(text::BufferList& bufferList) : bufferList_(&bufferList) {
    }

    std::string Line(const std::filesystem::path& path, std::size_t lineNumber) {
        if (lineNumber == 0) {
            return {};
        }
        if (const text::Buffer* open = bufferList_->FindByPath(path)) {
            const text::ITextStorage& content   = open->Content();
            const std::size_t         lineCount = content.LineCount();
            if (lineNumber > lineCount) {
                return {}; // past the live end -- same empty-body degrade a short file gets
            }
            // [start of this line, start of the next) -- LineToByteOffset
            // clamps at the last line, so this is the whole tail there.
            // Only the trailing '\n' comes off: a CRLF file's '\r' is a real
            // source byte, and dropping it would put the excerpt's own text
            // one byte out of step with the range resolved against it.
            std::string line = content.Substring(content.LineToByteOffset(lineNumber - 1),
                                                 content.LineToByteOffset(lineNumber) - content.LineToByteOffset(lineNumber - 1));
            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }
            return line;
        }
        if (path != cachedPath_) {
            Load(path);
        }
        if (!cached_) {
            return ReadFileLine(path, lineNumber);
        }
        return lineNumber <= cachedLines_.size() ? cachedLines_[lineNumber - 1] : std::string();
    }

  private:
    static constexpr std::uintmax_t kMaxCachedFileBytes = 8u * 1024u * 1024u;

    void Load(const std::filesystem::path& path) {
        cachedPath_ = path;
        cachedLines_.clear();
        cached_ = false;

        std::error_code      ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaxCachedFileBytes) {
            return; // unreadable, or big enough that one line at a time is the cheaper trade
        }
        std::ifstream file(path);
        if (!file) {
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            cachedLines_.push_back(line);
        }
        cached_ = true;
    }

    text::BufferList*        bufferList_;
    std::filesystem::path    cachedPath_;
    std::vector<std::string> cachedLines_;
    bool                     cached_ = false;
};

// ACP context auto-attach follow-up: ReadFileLine's ranged sibling, for
// SendResultLineToAgent's "surrounding source" excerpt -- reads
// [startLine, endLine] (1-indexed, inclusive, clamped to what the file
// actually has), each returned line prefixed with its own 1-indexed
// line number for readability. Empty string on total failure (path
// unreadable, startLine == 0), same contract as ReadFileLine; a partial
// read (file shorter than endLine) simply stops early rather than
// failing outright.
inline std::string ReadFileLines(const std::filesystem::path& path, std::size_t startLine, std::size_t endLine) {
    std::ifstream file(path);
    if (!file || startLine == 0 || startLine > endLine) {
        return {};
    }
    std::string line;
    for (std::size_t i = 1; i < startLine; ++i) {
        if (!std::getline(file, line)) {
            return {};
        }
    }
    std::string excerpt;
    for (std::size_t lineNumber = startLine; lineNumber <= endLine; ++lineNumber) {
        if (!std::getline(file, line)) {
            break;
        }
        excerpt += "  " + std::to_string(lineNumber) + ": " + line + "\n";
    }
    return excerpt;
}

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
inline text::Buffer& RefillSingletonBuffer(text::BufferList& bufferList, const char* name, const std::string& text) {
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
// two (see Manager::Variable::memoryReference's own doc comment).
inline std::string FormatDebugVariableLine(const ned::editor::dap::Manager::Variable& variable, std::size_t indent, int ownerRef,
                                           bool hex) {
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
    if (hex) {
        line += "  [hex]";
    }
    return line;
}

// Pointer-graph follow-up: the inverse of FormatDebugVariableLine above,
// extracting whatever a caller needs from a
// "name[: type] = value  [ref:N]?  [owner:M]?  [mem:<ref>]?  [hex]?"
// *debug* buffer line -- replaces what used to be three near-identical
// ad-hoc rfind blocks in ExpandVariableAtPoint/SetVariableAtPoint/
// ToggleHexFormatAtPoint, and is what RequestPointerGraphAtPoint uses
// too. A field a real line doesn't carry is simply left at its default
// (0/empty) rather than treated as a parse failure -- e.g. a watch line
// ("expr = value  [watch:N]") has no [ref:]/[owner:]/[mem:] at all and
// still parses fine. Returns std::nullopt only when the line isn't even
// "name ... = value"-shaped at all (SetVariableAtPoint/
// ToggleHexFormatAtPoint's own prior "Not an editable variable line."/
// "No formattable value on this line." guard).
struct ParsedDebugVariableLine {
    std::size_t indent = 0;
    std::string name;
    std::string type; // empty if the line carried none
    std::string value;
    int         variablesReference = 0; // 0 if no [ref:N] marker
    int         ownerRef           = 0; // 0 if no [owner:M] marker
    std::string memoryReference;        // empty if no [mem:<ref>] marker
};

inline std::optional<ParsedDebugVariableLine> ParseDebugVariableLine(const std::string& lineText) {
    ParsedDebugVariableLine parsed;
    while (parsed.indent < lineText.size() && lineText[parsed.indent] == ' ') {
        ++parsed.indent;
    }

    auto extractInt = [&lineText](const char* marker, std::size_t markerLen) -> int {
        const std::size_t pos = lineText.rfind(marker);
        if (pos == std::string::npos) {
            return 0;
        }
        try {
            return std::stoi(lineText.substr(pos + markerLen)); // stoi stops at the closing ']'
        }
        catch (const std::exception&) {
            return 0;
        }
    };
    parsed.variablesReference = extractInt("[ref:", 5);
    parsed.ownerRef           = extractInt("[owner:", 7);

    const std::size_t memPos   = lineText.rfind("[mem:");
    const std::size_t memClose = (memPos == std::string::npos) ? std::string::npos : lineText.find(']', memPos + 5);
    if (memPos != std::string::npos && memClose != std::string::npos) {
        parsed.memoryReference = lineText.substr(memPos + 5, memClose - (memPos + 5));
    }

    // Same ": "/" = " separator convention FormatDebugVariableLine
    // always writes -- look for those literal two-character separators,
    // not the first bare ':'/'=' (a variable named e.g. "operator=" must
    // not be split mid-name).
    const std::size_t colonPos = lineText.find(": ", parsed.indent);
    const std::size_t eqPos    = lineText.find(" = ", parsed.indent);
    const std::size_t nameEnd  = (colonPos != std::string::npos && (eqPos == std::string::npos || colonPos < eqPos))
                                     ? colonPos
                                     : eqPos;
    if (nameEnd == std::string::npos || nameEnd <= parsed.indent) {
        return std::nullopt;
    }
    parsed.name = lineText.substr(parsed.indent, nameEnd - parsed.indent);

    std::size_t valueStart;
    if (nameEnd == colonPos) {
        const std::size_t typeEqPos = lineText.find(" = ", colonPos + 2);
        if (typeEqPos == std::string::npos) {
            return std::nullopt;
        }
        parsed.type = lineText.substr(colonPos + 2, typeEqPos - (colonPos + 2));
        valueStart  = typeEqPos + 3;
    }
    else {
        valueStart = eqPos + 3;
    }

    std::size_t valueEnd = lineText.size();
    for (const std::size_t markerPos :
         {lineText.rfind("[ref:"), lineText.rfind("[owner:"), memPos, lineText.rfind("[hex]")}) {
        if (markerPos != std::string::npos && markerPos < valueEnd) {
            valueEnd = markerPos;
        }
    }
    while (valueEnd > 0 && lineText[valueEnd - 1] == ' ') {
        --valueEnd;
    }
    if (valueEnd <= valueStart) {
        return std::nullopt;
    }
    parsed.value = lineText.substr(valueStart, valueEnd - valueStart);

    return parsed;
}

// "name — root" -- unique per entry since ProjectRegistryStore keys on
// the normalized root, so this doubles as the lookup key back from a
// ranked/selected string to its underlying entry below.
inline std::string FormatProjectEntry(const ned::editor::ProjectRegistryEntry& entry) {
    return entry.name + " — " + entry.root;
}

inline bool InRange(std::size_t byteOffset, const text::ConflictHunk::Range& range) {
    return byteOffset >= range.start && byteOffset < range.end;
}

} // namespace ned::ui::detail

#endif // NED_UI_BUFFERVIEWINTERNAL_H
