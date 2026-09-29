//
// AcpPanel's transcript rendering, UI-state-free: Manager's structured
// transcript in, styled logical lines out, then word-wrapped into the
// physical rows Paint() draws. Nothing here touches a Canvas or a Theme --
// DisplayStyle is resolved to a Brush by the panel itself -- so the whole
// pipeline is testable on plain strings.
//

#ifndef NED_UI_ACPPANEL_TRANSCRIPTFORMAT_H
#define NED_UI_ACPPANEL_TRANSCRIPTFORMAT_H

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Acp/Manager.h"
#include "Editor/Image/Decode.h"
#include "Editor/Mode.h"

namespace ned::ui::acppanel {

enum class DisplayStyle { Plain,
                          Dim,
                          Warning,
                          Error,
                          Accent,
                          Hint,
                          DiffAdded,
                          DiffRemoved };

// Styling beyond DisplayLine::style over a range of display columns of the
// line's own (markup-stripped) text. `code` tints the background and keeps
// the foreground; `syntaxClass` then recolours it from the syntax theme.
// Spans are sorted and never overlap.
struct InlineSpan {
    int                                startColumn;
    int                                columnCount;
    bool                               bold;
    bool                               code;
    std::optional<editor::SyntaxClass> syntaxClass = std::nullopt;
    editor::CaptureId                  captureId   = editor::kNoCapture;
};

inline constexpr std::size_t kNoEntry = std::numeric_limits<std::size_t>::max();

// What a click on a line does.
enum class LineAction { None,
                        ToggleExpand,
                        OpenLocation,
                        Copy,
                        Review, // the turn before this line
                        OpenUrl };

struct LineLocation {
    std::string                path;
    std::optional<std::size_t> line; // 1-based
};

// One row of a picture drawn inline: which image, which of its rows, and
// the box of cells it fills, starting `column` cells in.
struct ImageRow {
    std::uint64_t id      = 0; // TranscriptImage::id
    int           row     = 0;
    int           rows    = 0;
    int           columns = 0;
    int           column  = 0;
};

struct DisplayLine {
    std::string                 text;
    DisplayStyle                style = DisplayStyle::Plain;
    std::vector<InlineSpan>     spans;
    std::size_t                 entryIndex = kNoEntry; // the transcript entry this line renders, if any
    LineAction                  action     = LineAction::None;
    std::optional<LineLocation> location; // LineAction::OpenLocation only
    std::string                 copyText; // LineAction::Copy only
    std::string                 url;      // LineAction::OpenUrl only
    std::optional<ImageRow>     image;    // a picture's row, drawn over the (empty) text
};

// One physical row of a wrapped string. startColumn/columnCount are display
// columns into the original string; every glyph lands in exactly one row.
struct WrappedRow {
    std::string text;
    int         startColumn;
    int         columnCount;
};

// Greedy word-wrap, breaking after the last space in the row when there is
// one and mid-word otherwise. Always returns at least one row.
[[nodiscard]] std::vector<WrappedRow> WordWrap(std::string_view text, int width);

// `left` padded so `marker` ends at column `width`; a plain " marker"
// suffix when there isn't room.
[[nodiscard]] std::string RightAlignMarker(const std::string& left, const std::string& marker, int width);

struct InlineMarkdownResult {
    std::string             text;
    std::vector<InlineSpan> spans;
};

// Strips **bold** / `code` markup and turns a leading "- "/"* "/"+ " into a
// bullet glyph. No nesting, no escapes; unmatched delimiters stay literal.
[[nodiscard]] InlineMarkdownResult ApplyInlineMarkdown(std::string_view raw);

// Re-bases `spans` onto one wrapped row's [0, rowColumnCount) column space,
// dropping anything that doesn't overlap it.
[[nodiscard]] std::vector<InlineSpan> SpansForRow(const std::vector<InlineSpan>& spans, int rowStartColumn, int rowColumnCount);

// A compact +/- unified diff, capped with a "(N more...)" tail.
[[nodiscard]] std::vector<DisplayLine> FormatDiffPreview(const std::string& oldText, const std::string& newText);

// Highlights `code` as the language a fence names; empty when the tag names
// no language ned knows.
using CodeHighlighter = std::function<std::vector<editor::HighlightSpan>(std::string_view language, std::string_view code)>;

struct CodeBlock {
    std::string language; // the fence's first word, possibly empty
    std::string code;     // without the fences or a trailing newline
};

// Every fenced block in a Markdown text, in order. A block still open at
// the end (a reply mid-stream) runs to the end.
[[nodiscard]] std::vector<CodeBlock> ExtractCodeBlocks(std::string_view markdown);

// A GFM table's rows as aligned columns, or nullopt when `rows` isn't a
// table (no delimiter row second) or it can't fit `width`.
[[nodiscard]] std::optional<std::vector<DisplayLine>> FormatTable(const std::vector<std::string_view>& rows, int width, DisplayStyle style,
                                                                  std::string_view indent);

// Something the copy picker offers: a whole reply, or one code block.
struct CopyCandidate {
    std::string label;  // the text's first line
    std::string detail; // "reply · 12 lines", "cpp · 4 lines"
    std::string text;
};

// The agent's last `maxReplies` replies, newest first, each followed by
// its code blocks, last first.
[[nodiscard]] std::vector<CopyCandidate> CopyCandidates(const std::vector<editor::acp::Manager::TranscriptEntry>& transcript,
                                                        std::size_t                                               maxReplies);

// A picture the save picker offers.
struct ImageCandidate {
    std::string                           label;  // the prompt it came with, or the agent's name for it
    std::string                           detail; // "yours · image/png · 12 KB"
    editor::acp::Manager::TranscriptImage image;
};

// Every picture in the transcript, newest first.
[[nodiscard]] std::vector<ImageCandidate> ImageCandidates(const std::vector<editor::acp::Manager::TranscriptEntry>& transcript);

struct TranscriptFormatOptions {
    int width = 0;
    // Whether transcript entry i shows its details: a tool call's input,
    // locations, diff and output, or a thought's full text. Unset means
    // every such entry is collapsed.
    std::function<bool(std::size_t)> expanded;
    bool                             hideThinking = false;
    // Tool locations under this directory are shown relative to it.
    std::filesystem::path projectRoot;
    CodeHighlighter       highlightCode;
    // What a running tool call's elapsed time is measured against.
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // The box of cells a picture fills within `maxColumns`; nullopt (or
    // unset) when it can't be shown, leaving only its caption.
    std::function<std::optional<editor::image::CellFit>(const editor::acp::Manager::TranscriptImage& image, int maxColumns)> imageFit;
};

// The most rows a picture takes in the transcript.
inline constexpr int kImageMaxRows = 20;

// A tool kind's one-column glyph ("edit" -> "✎", ...).
[[nodiscard]] std::string_view ToolKindGlyph(std::string_view toolKind);

[[nodiscard]] std::vector<DisplayLine> FormatTranscript(const std::vector<editor::acp::Manager::TranscriptEntry>&    transcript,
                                                        const std::optional<editor::acp::Manager::PermissionPrompt>& pending,
                                                        const TranscriptFormatOptions&                               options);

struct PhysicalLine {
    std::string             text;
    DisplayStyle            style = DisplayStyle::Plain;
    std::vector<InlineSpan> spans;
    std::size_t             lineIndex = 0; // index of the DisplayLine this row was wrapped from
};

[[nodiscard]] std::vector<PhysicalLine> WrapDisplayLines(const std::vector<DisplayLine>& lines, int width);

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_TRANSCRIPTFORMAT_H
