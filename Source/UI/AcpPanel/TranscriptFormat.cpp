#include "TranscriptFormat.h"

#include <algorithm>
#include <iterator>

#include "Editor/Acp/ContentBlocks.h"
#include "Text/DisplayWidth.h"
#include "Text/LineDiff.h"
#include "Text/Utf8.h"

namespace ned::ui::acppanel {

namespace {

    // A large edit shouldn't push the rest of the transcript off a short panel.
    constexpr std::size_t kMaxDiffPreviewLines = 12;
    constexpr std::size_t kMaxToolOutputLines  = 12;
    constexpr std::size_t kLiveOutputLines     = 3;
    constexpr long        kShowDurationSeconds = 10;

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
            InlineSpan clipped  = span;
            clipped.startColumn = start - rowStartColumn;
            clipped.columnCount = end - start;
            result.push_back(clipped);
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

    constexpr int kCodeTabWidth = 4;

    std::vector<std::string_view> SplitLines(std::string_view text) {
        std::vector<std::string_view> lines;
        while (true) {
            const std::size_t newline = text.find('\n');
            lines.push_back(text.substr(0, newline));
            if (newline == std::string_view::npos) {
                break;
            }
            text = text.substr(newline + 1);
        }
        return lines;
    }

    std::string_view Trim(std::string_view text) {
        const std::size_t first = text.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            return {};
        }
        const std::size_t last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }

    bool IsFence(std::string_view line) {
        return Trim(line).starts_with("```");
    }

    // The language a fence names: its first word, without Pandoc's "{." wrapping.
    std::string FenceLanguage(std::string_view fence) {
        std::string_view info = Trim(Trim(fence).substr(3));
        while (!info.empty() && (info.front() == '{' || info.front() == '.')) {
            info.remove_prefix(1);
        }
        const std::size_t end = info.find_first_of(" \t,}");
        return std::string(info.substr(0, end));
    }

    // The index just past the fenced block opening at lines[open], and the
    // block's code.
    std::pair<std::size_t, std::string> CollectFence(const std::vector<std::string_view>& lines, std::size_t open) {
        std::string code;
        std::size_t i = open + 1;
        for (; i < lines.size() && !IsFence(lines[i]); ++i) {
            if (i > open + 1) {
                code += '\n';
            }
            code.append(lines[i]);
        }
        return {i < lines.size() ? i + 1 : i, std::move(code)};
    }

    // One code line's text, tabs expanded, as runs of equal highlight --
    // each glyph taking the class of the last span covering its first byte.
    std::vector<DisplayLine> FormatCodeBlock(std::string_view language, std::string_view code, DisplayStyle style,
                                             std::string_view indent, const TranscriptFormatOptions& options) {
        std::vector<editor::HighlightSpan> highlights;
        if (options.highlightCode && !language.empty() && !code.empty()) {
            highlights = options.highlightCode(language, code);
        }
        constexpr int    kNone = -1;
        std::vector<int> classAt(code.size(), kNone);
        for (std::size_t i = 0; i < highlights.size(); ++i) {
            const std::size_t end = std::min(highlights[i].endByte, code.size());
            for (std::size_t byte = std::min(highlights[i].startByte, end); byte < end; ++byte) {
                classAt[byte] = static_cast<int>(i);
            }
        }

        std::vector<DisplayLine> lines;
        const int                shift     = ColumnCount(indent);
        std::size_t              lineStart = 0;
        while (lineStart <= code.size()) {
            const std::size_t newline = code.find('\n', lineStart);
            const std::size_t lineEnd = newline == std::string_view::npos ? code.size() : newline;

            std::string             text(indent);
            std::vector<InlineSpan> spans;
            int                     column = 0;
            auto                    emit   = [&](std::string_view glyphText, int columns, int highlight) {
                text.append(glyphText);
                InlineSpan span{.startColumn = shift + column, .columnCount = columns, .bold = false, .code = true};
                if (highlight != kNone) {
                    span.syntaxClass = highlights[static_cast<std::size_t>(highlight)].syntaxClass;
                    span.captureId   = highlights[static_cast<std::size_t>(highlight)].captureId;
                }
                if (!spans.empty() && spans.back().syntaxClass == span.syntaxClass && spans.back().captureId == span.captureId) {
                    spans.back().columnCount += columns;
                }
                else {
                    spans.push_back(span);
                }
                column += columns;
            };
            for (std::size_t pos = lineStart; pos < lineEnd;) {
                if (code[pos] == '\t') {
                    const int columns = kCodeTabWidth - column % kCodeTabWidth;
                    emit(std::string(static_cast<std::size_t>(columns), ' '), columns, classAt[pos]);
                    ++pos;
                    continue;
                }
                const text::Glyph glyph = text::GlyphAt(code, pos);
                emit(code.substr(pos, glyph.byteLength), glyph.columns, classAt[pos]);
                pos += glyph.byteLength;
            }
            if (column == 0) {
                emit(" ", 1, kNone); // an empty line still shows the block's tint
            }
            lines.push_back({.text = std::move(text), .style = style, .spans = std::move(spans)});
            if (newline == std::string_view::npos) {
                break;
            }
            lineStart = newline + 1;
        }
        return lines;
    }

    DisplayLine CodeBlockHeader(const std::string& language, std::string code, std::string_view indent, int width) {
        const std::string label = std::string(indent) + (language.empty() ? "code" : language);
        return {.text = RightAlignMarker(label, "⧉ copy", width), .style = DisplayStyle::Dim, .action = LineAction::Copy, .copyText = std::move(code)};
    }

    bool IsTableDelimiterRow(std::string_view line) {
        line = Trim(line);
        return line.find('-') != std::string_view::npos && line.find_first_not_of("|:- \t") == std::string_view::npos &&
               (line.find('|') != std::string_view::npos || line.starts_with(":-") || line.starts_with("--"));
    }

    // A table row's cells: split at each '|' outside a code span, with the
    // outer pipes optional and "\|" standing for a literal one.
    std::vector<std::string> SplitTableRow(std::string_view line) {
        line = Trim(line);
        if (line.starts_with('|')) {
            line.remove_prefix(1);
        }
        if (line.ends_with('|') && !line.ends_with("\\|")) {
            line.remove_suffix(1);
        }
        std::vector<std::string> cells(1);
        bool                     inCode = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '\\' && i + 1 < line.size() && line[i + 1] == '|') {
                cells.back() += '|';
                ++i;
            }
            else if (c == '`') {
                inCode = !inCode;
                cells.back() += c;
            }
            else if (c == '|' && !inCode) {
                cells.emplace_back();
            }
            else {
                cells.back() += c;
            }
        }
        for (std::string& cell : cells) {
            cell = std::string(Trim(cell));
        }
        return cells;
    }

    // Each line of `text` through ApplyInlineMarkdown, indented by `indent`.
    // Fenced code gets a header (its language and a copy button) and
    // highlighting; the fence lines themselves are dropped. Tables are
    // aligned when they fit.
    std::vector<DisplayLine> FormatMarkdownLines(const std::string& text, DisplayStyle style, std::string_view indent,
                                                 const TranscriptFormatOptions& options) {
        std::vector<DisplayLine>            lines;
        const int                           shift = ColumnCount(indent);
        const std::vector<std::string_view> raw   = SplitLines(text);
        for (std::size_t i = 0; i < raw.size();) {
            if (IsFence(raw[i])) {
                const std::string language     = FenceLanguage(raw[i]);
                auto [next, code]              = CollectFence(raw, i);
                std::vector<DisplayLine> block = FormatCodeBlock(language, code, style, indent, options);
                lines.push_back(CodeBlockHeader(language, std::move(code), indent, options.width));
                lines.insert(lines.end(), std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
                i = next;
                continue;
            }
            if (raw[i].find('|') != std::string_view::npos && i + 1 < raw.size() && IsTableDelimiterRow(raw[i + 1])) {
                std::size_t end = i + 2;
                while (end < raw.size() && raw[end].find('|') != std::string_view::npos && !Trim(raw[end]).empty()) {
                    ++end;
                }
                const std::vector<std::string_view> rows(raw.begin() + static_cast<std::ptrdiff_t>(i), raw.begin() + static_cast<std::ptrdiff_t>(end));
                if (std::optional<std::vector<DisplayLine>> table = FormatTable(rows, options.width, style, indent)) {
                    lines.insert(lines.end(), std::make_move_iterator(table->begin()), std::make_move_iterator(table->end()));
                    i = end;
                    continue;
                }
            }
            InlineMarkdownResult formatted = ApplyInlineMarkdown(raw[i]);
            for (InlineSpan& span : formatted.spans) {
                span.startColumn += shift;
            }
            formatted.text.insert(0, indent);
            lines.push_back({.text = std::move(formatted.text), .style = style, .spans = std::move(formatted.spans)});
            ++i;
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

    // A tool's output, up to `maxLines` of it: a command's from its end,
    // where a build's errors and a test run's summary land, anything
    // else's from its start. `noteHidden` says what was left out.
    void AppendOutputLines(std::vector<DisplayLine>& lines, const editor::acp::Manager::TranscriptEntry& entry, std::size_t maxLines,
                           bool noteHidden) {
        std::string_view output = entry.toolOutput;
        while (!output.empty() && output.back() == '\n') {
            output.remove_suffix(1);
        }
        if (output.empty()) {
            return;
        }
        std::vector<std::string_view> all;
        while (true) {
            const std::size_t newline = output.find('\n');
            all.push_back(output.substr(0, newline));
            if (newline == std::string_view::npos) {
                break;
            }
            output = output.substr(newline + 1);
        }
        const std::size_t shown = std::min(all.size(), maxLines);
        const std::size_t first = entry.terminal ? all.size() - shown : 0;
        if (noteHidden && entry.terminal && (first > 0 || entry.toolOutputTrimmed)) {
            lines.push_back({.text  = entry.toolOutputTrimmed ? "    (earlier output not kept)" : "    (" + std::to_string(first) + " earlier line(s))",
                             .style = DisplayStyle::Dim});
        }
        for (std::size_t i = first; i < first + shown; ++i) {
            lines.push_back({.text = "    " + std::string(all[i]), .style = DisplayStyle::Dim});
        }
        if (noteHidden && !entry.terminal && all.size() > shown) {
            lines.push_back({.text = "    (" + std::to_string(all.size() - shown) + " more line(s))", .style = DisplayStyle::Dim});
        }
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
        const bool           running   = !failed && entry.status != "completed" && entry.status != "cancelled" && !entry.status.empty();
        std::string          marker    = entry.status == "completed"   ? "✓"
                                         : failed                      ? (entry.exitCode && *entry.exitCode != 0 ? "✗ exit " + std::to_string(*entry.exitCode) : "✗ failed")
                                         : entry.status == "cancelled" ? "cancelled"
                                         : entry.status.empty()        ? std::string()
                                                                       : "…";
        if (entry.startedAt) {
            // A running call counts up; a finished one keeps its time only
            // when it took long enough to be worth knowing.
            const auto end     = entry.finishedAt.value_or(options.now);
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(end - *entry.startedAt).count();
            if ((running && !entry.finishedAt && seconds >= 1) || (entry.status == "completed" && entry.finishedAt && seconds >= kShowDurationSeconds)) {
                marker += " " + std::to_string(seconds) + "s";
            }
        }
        InlineMarkdownResult formatted = ApplyInlineMarkdown(header);
        lines.push_back({.text   = RightAlignMarker(formatted.text, marker, options.width),
                         .style  = failed ? DisplayStyle::Warning : DisplayStyle::Dim,
                         .spans  = std::move(formatted.spans),
                         .action = LineAction::ToggleExpand});
        if (!open) {
            // A running command shows what it's printing.
            if (running && !entry.finishedAt && entry.terminal) {
                AppendOutputLines(lines, entry, kLiveOutputLines, false);
            }
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
        AppendOutputLines(lines, entry, kMaxToolOutputLines, true);
        return lines;
    }

} // namespace

std::vector<CodeBlock> ExtractCodeBlocks(std::string_view markdown) {
    std::vector<CodeBlock>              blocks;
    const std::vector<std::string_view> lines = SplitLines(markdown);
    for (std::size_t i = 0; i < lines.size();) {
        if (!IsFence(lines[i])) {
            ++i;
            continue;
        }
        auto [next, code] = CollectFence(lines, i);
        blocks.push_back({.language = FenceLanguage(lines[i]), .code = std::move(code)});
        i = next;
    }
    return blocks;
}

std::optional<std::vector<DisplayLine>> FormatTable(const std::vector<std::string_view>& rows, int width, DisplayStyle style,
                                                    std::string_view indent) {
    if (rows.size() < 2 || !IsTableDelimiterRow(rows[1])) {
        return std::nullopt;
    }
    enum class Align { Left,
                       Center,
                       Right };
    const std::vector<std::string> header  = SplitTableRow(rows[0]);
    const std::size_t              columns = header.size();
    std::vector<Align>             aligns(columns, Align::Left);
    {
        const std::vector<std::string> delimiters = SplitTableRow(rows[1]);
        for (std::size_t c = 0; c < columns && c < delimiters.size(); ++c) {
            const bool left  = delimiters[c].starts_with(':');
            const bool right = delimiters[c].ends_with(':');
            aligns[c]        = left && right ? Align::Center : right ? Align::Right
                                                                     : Align::Left;
        }
    }

    std::vector<std::vector<InlineMarkdownResult>> cells;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r == 1) {
            continue;
        }
        std::vector<std::string> raw = r == 0 ? header : SplitTableRow(rows[r]);
        raw.resize(columns);
        std::vector<InlineMarkdownResult> formatted;
        for (const std::string& cell : raw) {
            formatted.push_back(ApplyInlineMarkdown(cell));
        }
        cells.push_back(std::move(formatted));
    }
    std::vector<int> widths(columns, 1);
    for (const auto& row : cells) {
        for (std::size_t c = 0; c < columns; ++c) {
            widths[c] = std::max(widths[c], ColumnCount(row[c].text));
        }
    }
    const std::string separator = " │ ";
    const int         shift     = ColumnCount(indent);
    int               total     = shift + 3 * static_cast<int>(columns - 1);
    for (const int w : widths) {
        total += w;
    }
    if (width > 0 && total > width) {
        return std::nullopt;
    }

    std::vector<DisplayLine> lines;
    for (std::size_t r = 0; r < cells.size(); ++r) {
        const bool              isHeader = r == 0;
        std::string             text(indent);
        std::vector<InlineSpan> spans;
        int                     column = shift;
        for (std::size_t c = 0; c < columns; ++c) {
            if (c > 0) {
                text += separator;
                column += 3;
            }
            const InlineMarkdownResult& cell    = cells[r][c];
            const int                   slack   = widths[c] - ColumnCount(cell.text);
            const int                   before  = aligns[c] == Align::Right ? slack : aligns[c] == Align::Center ? slack / 2
                                                                                                                 : 0;
            const int                   textCol = column + before;
            text += std::string(static_cast<std::size_t>(before), ' ') + cell.text + std::string(static_cast<std::size_t>(slack - before), ' ');
            // A header cell is bold throughout, its own code spans included.
            int covered = textCol;
            for (InlineSpan span : cell.spans) {
                span.startColumn += textCol;
                if (isHeader && covered < span.startColumn) {
                    spans.push_back({.startColumn = covered, .columnCount = span.startColumn - covered, .bold = true, .code = false});
                }
                span.bold = span.bold || isHeader;
                covered   = span.startColumn + span.columnCount;
                spans.push_back(span);
            }
            const int cellEnd = textCol + ColumnCount(cell.text);
            if (isHeader && covered < cellEnd) {
                spans.push_back({.startColumn = covered, .columnCount = cellEnd - covered, .bold = true, .code = false});
            }
            column += widths[c];
        }
        lines.push_back({.text = std::move(text), .style = style, .spans = std::move(spans)});
        if (isHeader) {
            std::string rule(indent);
            for (std::size_t c = 0; c < columns; ++c) {
                if (c > 0) {
                    rule += "─┼─";
                }
                for (int k = 0; k < widths[c]; ++k) {
                    rule += "─";
                }
            }
            lines.push_back({.text = std::move(rule), .style = DisplayStyle::Dim});
        }
    }
    return lines;
}

std::vector<CopyCandidate> CopyCandidates(const std::vector<editor::acp::Manager::TranscriptEntry>& transcript, std::size_t maxReplies) {
    auto firstLine = [](std::string_view text) {
        text = text.substr(std::min(text.find_first_not_of(" \n"), text.size()));
        return std::string(text.substr(0, text.find('\n')));
    };
    auto lineCount = [](std::string_view text) {
        const std::size_t count = LineCount(text);
        return std::to_string(count) + (count == 1 ? " line" : " lines");
    };
    std::vector<CopyCandidate> candidates;
    std::size_t                replies = 0;
    for (auto it = transcript.rbegin(); it != transcript.rend() && replies < maxReplies; ++it) {
        if (it->kind != editor::acp::Manager::TranscriptEntry::Kind::AgentText || it->text.find_first_not_of(" \n") == std::string::npos) {
            continue;
        }
        ++replies;
        std::string_view reply = it->text;
        while (!reply.empty() && reply.back() == '\n') {
            reply.remove_suffix(1);
        }
        candidates.push_back({.label = firstLine(reply), .detail = "reply · " + lineCount(reply), .text = std::string(reply)});
        std::vector<CodeBlock> blocks = ExtractCodeBlocks(it->text);
        for (auto block = blocks.rbegin(); block != blocks.rend(); ++block) {
            candidates.push_back({.label  = firstLine(block->code),
                                  .detail = (block->language.empty() ? std::string("code") : block->language) + " · " + lineCount(block->code),
                                  .text   = std::move(block->code)});
        }
    }
    return candidates;
}

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

namespace {

    using Entry = editor::acp::Manager::TranscriptEntry;

    std::string LineCountLabel(std::string_view text) {
        const std::size_t count = LineCount(text);
        return std::to_string(count) + (count == 1 ? " line" : " lines");
    }

    // Rows for each of `images` the panel can show, `indent` cells in.
    void AppendImages(std::vector<DisplayLine>& lines, const std::vector<editor::acp::Manager::TranscriptImage>& images,
                      const TranscriptFormatOptions& options, int indent) {
        if (!options.imageFit) {
            return;
        }
        for (const editor::acp::Manager::TranscriptImage& image : images) {
            const std::optional<editor::image::CellFit> fit = options.imageFit(image, std::max(1, options.width - indent));
            if (!fit || fit->columns <= 0 || fit->rows <= 0) {
                continue;
            }
            for (int row = 0; row < fit->rows; ++row) {
                lines.push_back({.image = ImageRow{.id = image.id, .row = row, .rows = fit->rows, .columns = fit->columns, .column = indent}});
            }
        }
    }

    std::vector<DisplayLine> FormatNotice(const Entry& entry) {
        DisplayStyle style = DisplayStyle::Hint;
        std::string  glyph = "ℹ ";
        if (entry.status == "warning") {
            style = DisplayStyle::Warning;
            glyph = "⚠ ";
        }
        else if (entry.status == "error") {
            style = DisplayStyle::Error;
            glyph = "✗ ";
        }
        std::vector<DisplayLine> lines;
        std::string_view         rest   = entry.detail;
        std::string              prefix = glyph + entry.text + (rest.empty() ? "" : ": ");
        do {
            const std::size_t newline = rest.find('\n');
            lines.push_back({.text = prefix + std::string(rest.substr(0, newline)), .style = style});
            rest   = newline == std::string_view::npos ? std::string_view() : rest.substr(newline + 1);
            prefix = "  ";
        }
        while (!rest.empty());
        return lines;
    }

    std::vector<DisplayLine> FormatCompaction(const Entry& entry, bool open, const TranscriptFormatOptions& options) {
        if (entry.status == "in_progress") {
            return {{.text = "⟳ Compacting context…", .style = DisplayStyle::Dim}};
        }
        if (entry.status == "failed") {
            return {{.text = "✗ Context compaction failed" + (entry.detail.empty() ? std::string() : ": " + entry.detail), .style = DisplayStyle::Warning}};
        }
        if (entry.status == "cancelled") {
            return {{.text = "Context compaction cancelled", .style = DisplayStyle::Dim}};
        }
        if (entry.status != "completed") {
            return {{.text = "Context compaction: " + entry.status, .style = DisplayStyle::Dim}};
        }
        if (entry.text.find_first_not_of(" \n") == std::string::npos) {
            return {{.text = "✓ Context compacted", .style = DisplayStyle::Dim}};
        }
        std::vector<DisplayLine> lines{
            {.text   = open ? "▾ Context compacted" : "▸ Context compacted (" + LineCountLabel(entry.text) + " of summary)",
             .style  = DisplayStyle::Dim,
             .action = LineAction::ToggleExpand}};
        if (open) {
            std::vector<DisplayLine> summary = FormatMarkdownLines(entry.text, DisplayStyle::Dim, "  ", options);
            lines.insert(lines.end(), std::make_move_iterator(summary.begin()), std::make_move_iterator(summary.end()));
        }
        return lines;
    }

    // Where a click on an agent's resource goes: a file opens in the editor,
    // anything else with a scheme is handed to the system.
    void LinkTarget(DisplayLine& line, const std::string& uri) {
        if (const std::optional<std::string> path = editor::acp::FileUriPath(uri)) {
            line.action   = LineAction::OpenLocation;
            line.location = LineLocation{.path = *path};
        }
        else if (uri.find("://") != std::string::npos) {
            line.action = LineAction::OpenUrl;
            line.url    = uri;
        }
    }

    std::vector<DisplayLine> FormatAgentContent(const Entry& entry, bool open, const TranscriptFormatOptions& options) {
        std::string details;
        auto        add = [&details](const std::string& part) {
            if (!part.empty()) {
                details += " · " + part;
            }
        };
        add(entry.mimeType);
        if (entry.byteSize > 0) {
            add(editor::acp::FormatByteSize(entry.byteSize));
        }
        const std::string name = entry.contentName.empty() ? entry.status : entry.contentName;
        if (entry.status == "resource_link") {
            DisplayLine line{.text = "↗ " + name + details, .style = DisplayStyle::Hint};
            LinkTarget(line, entry.detail);
            return {line};
        }
        if (entry.status == "resource" && !entry.text.empty()) {
            std::vector<DisplayLine> lines{
                {.text   = open ? "▾ " + name : "▸ " + name + " (" + LineCountLabel(entry.text) + ")",
                 .style  = DisplayStyle::Hint,
                 .action = LineAction::ToggleExpand}};
            if (open) {
                const std::size_t dot      = name.rfind('.');
                const std::string language = dot == std::string::npos ? std::string() : name.substr(dot + 1);
                std::string       fence    = "```";
                while (entry.text.find(fence) != std::string::npos) {
                    fence += '`';
                }
                const std::string        body = entry.text.ends_with('\n') ? entry.text : entry.text + "\n";
                std::vector<DisplayLine> code = FormatMarkdownLines(fence + language + "\n" + body + fence, DisplayStyle::Plain, "  ", options);
                lines.insert(lines.end(), std::make_move_iterator(code.begin()), std::make_move_iterator(code.end()));
            }
            return lines;
        }
        DisplayLine line{.text = "▣ " + name + details, .style = DisplayStyle::Dim};
        LinkTarget(line, entry.detail);
        std::vector<DisplayLine> lines{line};
        AppendImages(lines, entry.images, options, 2);
        return lines;
    }

} // namespace

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
                std::vector<DisplayLine> pictures;
                AppendImages(pictures, entry.images, options, 2);
                append(std::move(pictures), i);
                break;
            }
            case Kind::AgentText: {
                append(FormatMarkdownLines(entry.text, DisplayStyle::Accent, "", options), i);
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
                    append(FormatMarkdownLines(entry.text, DisplayStyle::Dim, "  ", options), i);
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
                if (entry.status == "question") {
                    lines.push_back({.text = "? " + entry.text, .style = DisplayStyle::Warning, .entryIndex = i});
                    break;
                }
                if (entry.status == "review") {
                    lines.push_back({.text       = "✎ " + entry.text + " · review (C-c C-r)",
                                     .style      = DisplayStyle::Hint,
                                     .entryIndex = i,
                                     .action     = LineAction::Review});
                    break;
                }
                lines.push_back({.text = "-- " + entry.text + " --", .style = DisplayStyle::Dim, .entryIndex = i});
                break;
            }
            case Kind::Notice: {
                append(FormatNotice(entry), i);
                break;
            }
            case Kind::Compaction: {
                append(FormatCompaction(entry, isExpanded(i), options), i);
                break;
            }
            case Kind::AgentContent: {
                append(FormatAgentContent(entry, isExpanded(i), options), i);
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
