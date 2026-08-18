#include "Editor/Table.h"

#include <algorithm>
#include <cctype>

namespace ned::editor::table {

namespace {

    std::string TrimWhitespace(std::string_view s) {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return std::string(s.substr(start, end - start));
    }

    // Codepoint count, not a grapheme-cluster/East-Asian-Wide-aware display
    // width -- see ComputeColumnWidths' own doc comment in Table.h for why.
    std::size_t CodepointWidth(std::string_view text) {
        std::size_t count = 0;
        for (const unsigned char byte : text) {
            if ((byte & 0xC0) != 0x80)
                ++count; // not a UTF-8 continuation byte
        }
        return count;
    }

    bool IsTableLine(std::string_view line) {
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        return i < line.size() && line[i] == '|';
    }

    // Same "scan forward via find('\n', ...)" idiom Org.cpp's ParseOutline/
    // ParseCheckboxes already establish, materialized into a random-access
    // vector here since FindTableBlockLines needs to walk both backward and
    // forward from an arbitrary line index, unlike those single forward passes.
    std::vector<std::string_view> SplitLines(std::string_view bufferText) {
        std::vector<std::string_view> lines;
        std::size_t                   lineStart = 0;
        while (lineStart <= bufferText.size()) {
            const std::size_t newlinePos = bufferText.find('\n', lineStart);
            const std::size_t lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
            lines.push_back(bufferText.substr(lineStart, lineEnd - lineStart));
            if (newlinePos == std::string_view::npos)
                break;
            lineStart = newlinePos + 1;
        }
        return lines;
    }

} // namespace

std::vector<std::string> SplitRow(std::string_view line) {
    std::string_view trimmed = line;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.remove_prefix(1);
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.remove_suffix(1);
    // Edge pipes are optional in both real Org and real GFM -- strip them if
    // present so "| a | b |" and "a | b" split identically.
    if (!trimmed.empty() && trimmed.front() == '|')
        trimmed.remove_prefix(1);
    if (!trimmed.empty() && trimmed.back() == '|')
        trimmed.remove_suffix(1);

    // Plain split on every '|' -- escaped pipes ("\|") inside a cell aren't
    // recognized, a stated v1 simplification (real Org/GFM both support
    // them; rare enough in practice not to hold up the common case).
    std::vector<std::string> cells;
    std::size_t              cellStart = 0;
    while (true) {
        const std::size_t      pipePos = trimmed.find('|', cellStart);
        const std::string_view cell =
            trimmed.substr(cellStart, pipePos == std::string_view::npos ? std::string_view::npos : pipePos - cellStart);
        cells.push_back(TrimWhitespace(cell));
        if (pipePos == std::string_view::npos)
            break;
        cellStart = pipePos + 1;
    }
    return cells;
}

std::vector<std::size_t> ComputeColumnWidths(const std::vector<std::vector<std::string>>& dataRows) {
    std::size_t columnCount = 0;
    for (const auto& row : dataRows)
        columnCount = std::max(columnCount, row.size());

    std::vector<std::size_t> widths(columnCount, 0);
    for (const auto& row : dataRows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], CodepointWidth(row[i]));
        }
    }
    return widths;
}

std::string PadCell(const std::string& text, std::size_t width, Alignment alignment) {
    const std::size_t textWidth = CodepointWidth(text);
    if (textWidth >= width)
        return text;

    const std::size_t totalPad = width - textWidth;
    switch (alignment) {
        case Alignment::Right:
            return std::string(totalPad, ' ') + text;
        case Alignment::Center: {
            const std::size_t leftPad  = totalPad / 2;
            const std::size_t rightPad = totalPad - leftPad;
            return std::string(leftPad, ' ') + text + std::string(rightPad, ' ');
        }
        case Alignment::Left:
        case Alignment::Default:
        default:
            return text + std::string(totalPad, ' ');
    }
}

std::vector<std::pair<std::size_t, std::size_t>> CellByteSpans(std::string_view line, std::size_t lineStartByte) {
    std::string_view trimmed     = line;
    std::size_t      leadingTrim = 0;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
        trimmed.remove_prefix(1);
        ++leadingTrim;
    }
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.remove_suffix(1);
    std::size_t pipeSkip = 0;
    if (!trimmed.empty() && trimmed.front() == '|') {
        trimmed.remove_prefix(1);
        pipeSkip = 1;
    }
    if (!trimmed.empty() && trimmed.back() == '|')
        trimmed.remove_suffix(1);

    const std::size_t contentStart = lineStartByte + leadingTrim + pipeSkip;

    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::size_t                                      cellStart = 0;
    while (true) {
        const std::size_t pipePos = trimmed.find('|', cellStart);
        const std::size_t cellEnd = (pipePos == std::string_view::npos) ? trimmed.size() : pipePos;
        spans.emplace_back(contentStart + cellStart, contentStart + cellEnd);
        if (pipePos == std::string_view::npos)
            break;
        cellStart = pipePos + 1;
    }
    return spans;
}

std::optional<std::pair<std::size_t, std::size_t>> FindTableBlockLines(std::string_view bufferText,
                                                                       std::size_t      pointLine) {
    const std::vector<std::string_view> lines = SplitLines(bufferText);
    if (pointLine >= lines.size() || !IsTableLine(lines[pointLine])) {
        return std::nullopt;
    }

    std::size_t start = pointLine;
    while (start > 0 && IsTableLine(lines[start - 1]))
        --start;

    std::size_t end = pointLine + 1;
    while (end < lines.size() && IsTableLine(lines[end]))
        ++end;

    return std::pair{start, end};
}

} // namespace ned::editor::table
