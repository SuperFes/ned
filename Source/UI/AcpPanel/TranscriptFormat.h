//
// AcpPanel's transcript rendering, UI-state-free: Manager's structured
// transcript in, styled logical lines out, then word-wrapped into the
// physical rows Paint() draws. Nothing here touches a Canvas or a Theme --
// DisplayStyle is resolved to a Brush by the panel itself -- so the whole
// pipeline is testable on plain strings.
//

#ifndef NED_UI_ACPPANEL_TRANSCRIPTFORMAT_H
#define NED_UI_ACPPANEL_TRANSCRIPTFORMAT_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Acp/Manager.h"

namespace ned::ui::acppanel {

enum class DisplayStyle { Plain,
                          Dim,
                          Warning,
                          Accent,
                          Hint,
                          DiffAdded,
                          DiffRemoved };

// Styling beyond DisplayLine::style over a range of display columns of the
// line's own (markup-stripped) text. `code` tints the background and keeps
// the foreground.
struct InlineSpan {
    int  startColumn;
    int  columnCount;
    bool bold;
    bool code;
};

inline constexpr std::size_t kNoEntry = std::numeric_limits<std::size_t>::max();

// What a click on a line does.
enum class LineAction { None,
                        ToggleExpand,
                        OpenLocation };

struct LineLocation {
    std::string                path;
    std::optional<std::size_t> line; // 1-based
};

struct DisplayLine {
    std::string                 text;
    DisplayStyle                style = DisplayStyle::Plain;
    std::vector<InlineSpan>     spans;
    std::size_t                 entryIndex = kNoEntry; // the transcript entry this line renders, if any
    LineAction                  action     = LineAction::None;
    std::optional<LineLocation> location; // LineAction::OpenLocation only
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

struct TranscriptFormatOptions {
    int width = 0;
    // Whether transcript entry i shows its details: a tool call's input,
    // locations, diff and output, or a thought's full text. Unset means
    // every such entry is collapsed.
    std::function<bool(std::size_t)> expanded;
    bool                             hideThinking = false;
    // Tool locations under this directory are shown relative to it.
    std::filesystem::path projectRoot;
};

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
