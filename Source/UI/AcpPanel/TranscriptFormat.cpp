#include "TranscriptFormat.h"

#include <algorithm>
#include <iterator>

#include "Text/DisplayWidth.h"
#include "Text/LineDiff.h"
#include "Text/Utf8.h"

namespace ned::ui::acppanel {

namespace {

    // A large edit shouldn't push the rest of the transcript off a short panel.
    constexpr std::size_t kMaxDiffPreviewLines = 12;
    constexpr std::size_t kMaxToolOutputLines  = 12;

    int ColumnCount(std::string_view text) {
        return text::StringColumns(text);
    }

} // namespace

std::vector<WrappedRow> WordWrap(std::string_view text, int width) {
    std::vector<WrappedRow> rows;
    if (width <= 0) {
        rows.push_back({std::string(text), 0, 0});
        return rows;
    }
    std::size_t rowStartByte  = 0;
    int         rowStartCol   = 0;
    int         col           = 0;
    std::size_t lastSpaceByte = std::string::npos; // byte just after the last space seen in this row
    int         lastSpaceCol  = 0;                 // col value at that point

    std::size_t pos = 0;
    while (pos < text.size()) {
        const text::Glyph glyph = text::GlyphAt(text, pos);
        if (col > 0 && col + glyph.columns > width) {
            if (lastSpaceByte != std::string::npos && lastSpaceByte > rowStartByte) {
                rows.push_back({std::string(text.substr(rowStartByte, lastSpaceByte - rowStartByte)), rowStartCol, lastSpaceCol});
                rowStartCol += lastSpaceCol;
                rowStartByte = lastSpaceByte;
                col -= lastSpaceCol;
            }
            else {
                rows.push_back({std::string(text.substr(rowStartByte, pos - rowStartByte)), rowStartCol, col});
                rowStartCol += col;
                rowStartByte = pos;
                col          = 0;
            }
            lastSpaceByte = std::string::npos;
        }
        const std::size_t next = pos + glyph.byteLength;
        if (text[pos] == ' ') { // safe at byte level: UTF-8 continuation/lead bytes are always >= 0x80
            lastSpaceByte = next;
            lastSpaceCol  = col + 1;
        }
        col += glyph.columns;
        pos = next;
    }
    rows.push_back({std::string(text.substr(rowStartByte)), rowStartCol, col});
    return rows;
}

std::string RightAlignMarker(const std::string& left, const std::string& marker, int width) {
    if (marker.empty()) {
        return left;
    }
    const int padding = width - ColumnCount(left) - ColumnCount(marker);
    if (padding < 1) {
        return left + " " + marker;
    }
    return left + std::string(static_cast<std::size_t>(padding), ' ') + marker;
}

InlineMarkdownResult ApplyInlineMarkdown(std::string_view raw) {
    InlineMarkdownResult result;
    std::string&         out = result.text;
    out.reserve(raw.size());

    // Indentation is preserved so nested lists stay nested. Never matches a
    // plan step's "[x] " checkbox prefix.
    std::size_t bodyStart = 0;
    {
        std::size_t indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') {
            ++indent;
        }
        if (indent + 1 < raw.size() && (raw[indent] == '-' || raw[indent] == '*' || raw[indent] == '+') && raw[indent + 1] == ' ') {
            out.append(raw.substr(0, indent));
            out += text::EncodeCodepointUtf8(U'•');
            out += ' ';
            bodyStart = indent + 2;
        }
    }

    int         col = ColumnCount(out);
    std::size_t pos = bodyStart;
    while (pos < raw.size()) {
        if (pos + 1 < raw.size() && raw[pos] == '*' && raw[pos + 1] == '*') {
            const std::size_t close = raw.find("**", pos + 2);
            if (close != std::string_view::npos && close > pos + 2) {
                const std::string_view inner    = raw.substr(pos + 2, close - (pos + 2));
                const int              startCol = col;
                out.append(inner);
                col += ColumnCount(inner);
                result.spans.push_back({.startColumn = startCol, .columnCount = col - startCol, .bold = true, .code = false});
                pos = close + 2;
                continue;
            }
        }
        if (raw[pos] == '`') {
            const std::size_t close = raw.find('`', pos + 1);
            if (close != std::string_view::npos && close > pos + 1) {
                const std::string_view inner    = raw.substr(pos + 1, close - (pos + 1));
                const int              startCol = col;
                out.append(inner);
                col += ColumnCount(inner);
                result.spans.push_back({.startColumn = startCol, .columnCount = col - startCol, .bold = false, .code = true});
                pos = close + 1;
                continue;
            }
        }
        const text::Glyph glyph = text::GlyphAt(raw, pos);
        out.append(raw.substr(pos, glyph.byteLength));
        col += glyph.columns;
        pos += glyph.byteLength;
    }
    return result;
}

std::vector<InlineSpan> SpansForRow(const std::vector<InlineSpan>& spans, int rowStartColumn, int rowColumnCount) {
    std::vector<InlineSpan> result;
    const int               rowEnd = rowStartColumn + rowColumnCount;
    for (const InlineSpan& span : spans) {
        const int start = std::max(span.startColumn, rowStartColumn);
        const int end   = std::min(span.startColumn + span.columnCount, rowEnd);
        if (start < end) {
            result.push_back({.startColumn = start - rowStartColumn, .columnCount = end - start, .bold = span.bold, .code = span.code});
        }
    }
    return result;
}

std::vector<DisplayLine> FormatDiffPreview(const std::string& oldText, const std::string& newText) {
    std::vector<DisplayLine>          lines;
    const std::vector<text::DiffLine> diff  = text::UnifiedDiff(oldText, newText);
    const std::size_t                 shown = std::min(diff.size(), kMaxDiffPreviewLines);
    for (std::size_t i = 0; i < shown; ++i) {
        const text::DiffLine& diffLine = diff[i];
        switch (diffLine.kind) {
            case text::DiffLineKind::Added:
                lines.push_back({.text = "  + " + diffLine.text, .style = DisplayStyle::DiffAdded});
                break;
            case text::DiffLineKind::Removed:
                lines.push_back({.text = "  - " + diffLine.text, .style = DisplayStyle::DiffRemoved});
                break;
            case text::DiffLineKind::Context:
            case text::DiffLineKind::Omitted:
                lines.push_back({.text = "    " + diffLine.text, .style = DisplayStyle::Dim});
                break;
        }
    }
    if (diff.size() > shown) {
        lines.push_back({.text = "  (" + std::to_string(diff.size() - shown) + " more diff line(s)...)", .style = DisplayStyle::Dim});
    }
    return lines;
}

namespace {

    // Each line of `text` through ApplyInlineMarkdown, indented by `indent`.
    // Lines inside a ``` fence are taken literally and tinted as code; the
    // fence lines themselves are dropped.
    std::vector<DisplayLine> FormatMarkdownLines(const std::string& text, DisplayStyle style, std::string_view indent) {
        std::vector<DisplayLine> lines;
        const int                shift   = ColumnCount(indent);
        bool                     inFence = false;
        std::size_t              start   = 0;
        while (start <= text.size()) {
            const std::size_t newlinePos = text.find('\n', start);
            const std::string rawLine    = newlinePos == std::string::npos ? text.substr(start) : text.substr(start, newlinePos - start);
            const std::size_t firstChar  = rawLine.find_first_not_of(' ');
            if (firstChar != std::string::npos && rawLine.compare(firstChar, 3, "```") == 0) {
                inFence = !inFence;
            }
            else if (inFence) {
                const std::string code = rawLine.empty() ? std::string(" ") : rawLine;
                lines.push_back({.text  = std::string(indent) + code,
                                 .style = style,
                                 .spans = {{.startColumn = shift, .columnCount = ColumnCount(code), .bold = false, .code = true}}});
            }
            else {
                InlineMarkdownResult formatted = ApplyInlineMarkdown(rawLine);
                for (InlineSpan& span : formatted.spans) {
                    span.startColumn += shift;
                }
                formatted.text.insert(0, indent);
                lines.push_back({.text = std::move(formatted.text), .style = style, .spans = std::move(formatted.spans)});
            }
            if (newlinePos == std::string::npos) {
                break;
            }
            start = newlinePos + 1;
        }
        return lines;
    }

    std::size_t LineCount(std::string_view text) {
        while (!text.empty() && text.back() == '\n') {
            text.remove_suffix(1);
        }
        return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
    }

    std::string DisplayPath(const std::string& path, const std::filesystem::path& projectRoot) {
        if (projectRoot.empty()) {
            return path;
        }
        const std::filesystem::path relative = std::filesystem::path(path).lexically_relative(projectRoot);
        if (relative.empty() || *relative.begin() == "..") {
            return path;
        }
        return relative.generic_string();
    }

    std::string LocationLabel(const editor::acp::Manager::ToolLocation& location, const std::filesystem::path& projectRoot) {
        std::string label = DisplayPath(location.path, projectRoot);
        if (location.line) {
            label += ":" + std::to_string(*location.line + 1);
        }
        return label;
    }

    std::vector<DisplayLine> FormatToolCall(const editor::acp::Manager::TranscriptEntry& entry, bool open,
                                            const TranscriptFormatOptions& options) {
        std::vector<DisplayLine> lines;

        // A title is one line; a multi-line one (a shell command) shows its
        // first.
        const std::size_t titleBreak = entry.text.find('\n');
        const std::string title      = titleBreak == std::string::npos ? entry.text : entry.text.substr(0, titleBreak) + " …";
        std::string       header     = std::string(open ? "▾ " : "▸ ") + std::string(ToolKindGlyph(entry.toolKind)) + " " + title;
        if (!entry.locations.empty()) {
            const std::string filename = std::filesystem::path(entry.locations.front().path).filename().string();
            if (filename.empty() || title.find(filename) == std::string::npos) {
                header += " · " + LocationLabel(entry.locations.front(), options.projectRoot);
            }
        }
        const bool           failed    = entry.status == "failed";
        const std::string    marker    = entry.status == "completed"   ? "✓"
                                         : failed                      ? "✗ failed"
                                         : entry.status == "cancelled" ? "cancelled"
                                         : entry.status.empty()        ? std::string()
                                                                       : "…";
        InlineMarkdownResult formatted = ApplyInlineMarkdown(header);
        lines.push_back({.text   = RightAlignMarker(formatted.text, marker, options.width),
                         .style  = failed ? DisplayStyle::Warning : DisplayStyle::Dim,
                         .spans  = std::move(formatted.spans),
                         .action = LineAction::ToggleExpand});
        if (!open) {
            return lines;
        }

        if (!entry.toolInput.empty()) {
            lines.push_back({.text = "    " + entry.toolInput, .style = DisplayStyle::Dim, .action = LineAction::ToggleExpand});
        }
        for (const editor::acp::Manager::ToolLocation& location : entry.locations) {
            lines.push_back({.text     = "    ↳ " + LocationLabel(location, options.projectRoot),
                             .style    = DisplayStyle::Hint,
                             .action   = LineAction::OpenLocation,
                             .location = LineLocation{.path = location.path, .line = location.line ? std::optional(*location.line + 1) : std::nullopt}});
        }
        if (entry.diffOldText && entry.diffNewText) {
            std::vector<DisplayLine> diff = FormatDiffPreview(*entry.diffOldText, *entry.diffNewText);
            lines.insert(lines.end(), std::make_move_iterator(diff.begin()), std::make_move_iterator(diff.end()));
        }
        if (!entry.toolOutput.empty()) {
            std::string_view  output = entry.toolOutput;
            const std::size_t total  = LineCount(output);
            std::size_t       shown  = 0;
            while (!output.empty() && shown < kMaxToolOutputLines) {
                const std::size_t newline = output.find('\n');
                lines.push_back({.text = "    " + std::string(output.substr(0, newline)), .style = DisplayStyle::Dim});
                ++shown;
                output = newline == std::string_view::npos ? std::string_view() : output.substr(newline + 1);
            }
            if (total > shown) {
                lines.push_back({.text = "    (" + std::to_string(total - shown) + " more line(s))", .style = DisplayStyle::Dim});
            }
        }
        return lines;
    }

} // namespace

std::string_view ToolKindGlyph(std::string_view toolKind) {
    if (toolKind == "read") {
        return "»";
    }
    if (toolKind == "edit") {
        return "✎";
    }
    if (toolKind == "delete") {
        return "✗";
    }
    if (toolKind == "move") {
        return "⇄";
    }
    if (toolKind == "search") {
        return "⌕";
    }
    if (toolKind == "execute") {
        return "$";
    }
    if (toolKind == "think") {
        return "…";
    }
    if (toolKind == "fetch") {
        return "↓";
    }
    if (toolKind == "switch_mode") {
        return "⇆";
    }
    return "•";
}

std::vector<DisplayLine> FormatTranscript(const std::vector<editor::acp::Manager::TranscriptEntry>&    transcript,
                                          const std::optional<editor::acp::Manager::PermissionPrompt>& pending,
                                          const TranscriptFormatOptions&                               options) {
    using Kind = editor::acp::Manager::TranscriptEntry::Kind;
    std::vector<DisplayLine> lines;

    auto append = [&lines](std::vector<DisplayLine> more, std::size_t entryIndex) {
        for (DisplayLine& line : more) {
            line.entryIndex = entryIndex;
            lines.push_back(std::move(line));
        }
    };

    auto isExpanded = [&options](std::size_t i) { return options.expanded && options.expanded(i); };

    for (std::size_t i = 0; i < transcript.size(); ++i) {
        const auto& entry = transcript[i];
        switch (entry.kind) {
            case Kind::UserMessage: {
                // "↳" marks a message steered into a running turn; a
                // multi-line prompt's later lines align under its first.
                std::string_view rest   = entry.text;
                std::string      prefix = entry.status == "steered" ? "↳ " : "> ";
                while (true) {
                    const std::size_t newline = rest.find('\n');
                    lines.push_back({.text = prefix + std::string(rest.substr(0, newline)), .style = DisplayStyle::Plain, .entryIndex = i});
                    if (newline == std::string_view::npos) {
                        break;
                    }
                    rest   = rest.substr(newline + 1);
                    prefix = "  ";
                }
                break;
            }
            case Kind::AgentText: {
                append(FormatMarkdownLines(entry.text, DisplayStyle::Accent, ""), i);
                break;
            }
            case Kind::AgentThought: {
                if (options.hideThinking || entry.text.find_first_not_of(" \n") == std::string::npos) {
                    break;
                }
                const bool open = isExpanded(i);
                lines.push_back({.text       = open ? "▾ Thinking" : "▸ Thinking (" + std::to_string(LineCount(entry.text)) + " lines)",
                                 .style      = DisplayStyle::Dim,
                                 .entryIndex = i,
                                 .action     = LineAction::ToggleExpand});
                if (open) {
                    append(FormatMarkdownLines(entry.text, DisplayStyle::Dim, "  "), i);
                }
                break;
            }
            case Kind::ToolCall: {
                append(FormatToolCall(entry, isExpanded(i), options), i);
                break;
            }
            case Kind::Plan: {
                for (const std::string& step : entry.planSteps) {
                    InlineMarkdownResult formatted = ApplyInlineMarkdown(step);
                    lines.push_back({.text = std::move(formatted.text), .style = DisplayStyle::Hint, .spans = std::move(formatted.spans), .entryIndex = i});
                }
                break;
            }
            case Kind::Permission: {
                lines.push_back({.text = "! " + entry.text, .style = DisplayStyle::Warning, .entryIndex = i});
                if (pending && pending->description == entry.text) {
                    std::string choices;
                    for (std::size_t optIndex = 0; optIndex < pending->options.size(); ++optIndex) {
                        if (optIndex > 0) {
                            choices += "  ";
                        }
                        choices += "[" + std::to_string(optIndex + 1) + "] " + pending->options[optIndex].name;
                    }
                    if (!choices.empty()) {
                        lines.push_back({.text = "  " + choices, .style = DisplayStyle::Warning, .entryIndex = i});
                    }
                    if (pending->diffOldText && pending->diffNewText) {
                        append(FormatDiffPreview(*pending->diffOldText, *pending->diffNewText), i);
                    }
                }
                break;
            }
            case Kind::SessionEvent: {
                lines.push_back({.text = "-- " + entry.text + " --", .style = DisplayStyle::Dim, .entryIndex = i});
                break;
            }
        }
    }
    return lines;
}

std::vector<PhysicalLine> WrapDisplayLines(const std::vector<DisplayLine>& lines, int width) {
    std::vector<PhysicalLine> rows;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const DisplayLine& logical = lines[i];
        for (WrappedRow& row : WordWrap(logical.text, width)) {
            rows.push_back({.text      = std::move(row.text),
                            .style     = logical.style,
                            .spans     = SpansForRow(logical.spans, row.startColumn, row.columnCount),
                            .lineIndex = i});
        }
    }
    return rows;
}

} // namespace ned::ui::acppanel
