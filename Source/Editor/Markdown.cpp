#include "Markdown.h"

#include <algorithm>
#include <utility>

namespace ned::editor::markdown {

namespace {

    // Same "scan forward via find('\n', ...)" idiom Org.cpp's own scanners use,
    // bounded to [startLine, endLine) -- returns each line's own text plus its
    // absolute start byte offset, in file order.
    std::vector<std::pair<std::string_view, std::size_t>> ScanBlockLines(const std::string& bufferText,
                                                                         std::size_t startLine, std::size_t endLine) {
        std::vector<std::pair<std::string_view, std::size_t>> result;
        std::size_t                                           lineStart  = 0;
        std::size_t                                           lineNumber = 0;
        while (lineStart <= bufferText.size() && lineNumber < endLine) {
            const std::size_t newlinePos = bufferText.find('\n', lineStart);
            const std::size_t lineEnd    = (newlinePos == std::string::npos) ? bufferText.size() : newlinePos;
            if (lineNumber >= startLine) {
                result.emplace_back(std::string_view(bufferText).substr(lineStart, lineEnd - lineStart), lineStart);
            }
            if (newlinePos == std::string::npos)
                break;
            lineStart = newlinePos + 1;
            ++lineNumber;
        }
        return result;
    }

    // A delimiter cell is optional ':' + one-or-more '-' + optional ':' --
    // e.g. "---", ":---", "---:", ":---:". Cells are already trimmed (via
    // table::SplitRow), so no whitespace handling needed here.
    bool IsMarkdownDelimiterCell(std::string_view cell) {
        if (cell.empty())
            return false;
        std::size_t start = 0;
        std::size_t end   = cell.size();
        if (cell.front() == ':')
            ++start;
        if (end > start && cell.back() == ':')
            --end;
        if (start >= end)
            return false; // nothing left but the colon(s) -- needs at least one '-'
        for (std::size_t i = start; i < end; ++i) {
            if (cell[i] != '-')
                return false;
        }
        return true;
    }

    table::Alignment AlignmentForDelimiterCell(std::string_view cell) {
        const bool left  = !cell.empty() && cell.front() == ':';
        const bool right = !cell.empty() && cell.back() == ':';
        if (left && right)
            return table::Alignment::Center;
        if (right)
            return table::Alignment::Right;
        if (left)
            return table::Alignment::Left;
        return table::Alignment::Default;
    }

} // namespace

std::optional<Table> FindTableAtPoint(const text::Buffer& buffer) {
    const std::string bufferText = buffer.Text();
    const std::size_t pointLine  = buffer.Content().ByteOffsetToLine(buffer.Point());

    const auto block = table::FindTableBlockLines(bufferText, pointLine);
    if (!block)
        return std::nullopt;
    const auto [startLine, endLine] = *block;
    if (endLine - startLine < 2)
        return std::nullopt; // needs at least a header and a delimiter row

    const auto lines = ScanBlockLines(bufferText, startLine, endLine);

    const auto delimiterCells = table::SplitRow(lines[1].first);
    for (const std::string& cell : delimiterCells) {
        if (!IsMarkdownDelimiterCell(cell))
            return std::nullopt; // not a real GFM table
    }

    Table result;
    result.startLine = startLine;
    result.endLine   = endLine;
    for (const std::string& cell : delimiterCells) {
        result.columnAlignments.push_back(AlignmentForDelimiterCell(cell));
    }
    result.rows.push_back(table::SplitRow(lines[0].first)); // header
    for (std::size_t i = 2; i < lines.size(); ++i) {
        result.rows.push_back(table::SplitRow(lines[i].first));
    }
    return result;
}

bool AlignTableAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;

    const std::string bufferText = buffer.Text();
    const auto        lines      = ScanBlockLines(bufferText, tableInfo->startLine, tableInfo->endLine);

    // Byte spans per row -- lines[0] is the header (result.rows[0]),
    // lines[1] is the delimiter row (skipped, never a cell target),
    // lines[2..] are the data rows (result.rows[1..]).
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> cellSpans(tableInfo->rows.size());
    cellSpans[0] = table::CellByteSpans(lines[0].first, lines[0].second);
    for (std::size_t i = 2; i < lines.size(); ++i) {
        cellSpans[i - 1] = table::CellByteSpans(lines[i].first, lines[i].second);
    }

    const std::size_t point      = buffer.Point();
    std::size_t       currentRow = 0, currentCol = 0;
    bool              found = false;
    for (std::size_t row = 0; row < tableInfo->rows.size() && !found; ++row) {
        for (std::size_t col = 0; col < cellSpans[row].size(); ++col) {
            if (point >= cellSpans[row][col].first && point <= cellSpans[row][col].second) {
                currentRow = row;
                currentCol = col;
                found      = true;
                break;
            }
        }
    }
    // No fallback-to-first-cell needed here (unlike Org's separator rows,
    // which can appear anywhere): every row in tableInfo->rows has a real
    // cellSpans entry, and FindTableAtPoint already confirmed point is
    // somewhere in [startLine, endLine) -- the only line with no cell
    // spans at all is the delimiter row itself (index 1), and point being
    // on it lands `found` false with currentRow/currentCol still {0, 0},
    // a reasonable landing spot (the header's own first cell).

    const std::vector<std::size_t> widths = table::ComputeColumnWidths(tableInfo->rows);

    std::string                           newText;
    std::vector<std::vector<std::size_t>> newCellOffsets(tableInfo->rows.size());
    auto                                  renderDataRow = [&](std::size_t rowIndex) {
        newText += '|';
        for (std::size_t col = 0; col < widths.size(); ++col) {
            newText += ' ';
            newCellOffsets[rowIndex].push_back(newText.size());
            const table::Alignment alignment =
                col < tableInfo->columnAlignments.size() ? tableInfo->columnAlignments[col] : table::Alignment::Default;
            const std::string cellText =
                col < tableInfo->rows[rowIndex].size() ? tableInfo->rows[rowIndex][col] : std::string();
            newText += table::PadCell(cellText, widths[col], alignment);
            newText += " |";
        }
        newText += '\n';
    };

    renderDataRow(0); // header
    newText += '|';
    for (std::size_t col = 0; col < widths.size(); ++col) {
        const table::Alignment alignment =
            col < tableInfo->columnAlignments.size() ? tableInfo->columnAlignments[col] : table::Alignment::Default;
        const bool leftColon  = alignment == table::Alignment::Left || alignment == table::Alignment::Center;
        const bool rightColon = alignment == table::Alignment::Right || alignment == table::Alignment::Center;
        // Matches a data cell's own total pipe-to-pipe span (" " + content
        // + " " = width + 2) so the delimiter row visually lines up under
        // the data rows -- at least one dash regardless (GFM's own
        // requirement), which only actually bites for a fully-empty,
        // Center-aligned column (width == 0).
        const std::size_t totalLength = widths[col] + 2;
        const std::size_t colonCount  = (leftColon ? 1 : 0) + (rightColon ? 1 : 0);
        const std::size_t dashCount   = totalLength > colonCount ? std::max<std::size_t>(1, totalLength - colonCount) : 1;
        if (leftColon)
            newText += ':';
        newText += std::string(dashCount, '-');
        if (rightColon)
            newText += ':';
        newText += '|';
    }
    newText += '\n';
    for (std::size_t row = 1; row < tableInfo->rows.size(); ++row)
        renderDataRow(row);

    const bool blockHadTrailingNewline = tableInfo->endLine < buffer.Content().LineCount();
    if (!blockHadTrailingNewline && !newText.empty())
        newText.pop_back();

    const std::size_t blockStartByte = buffer.Content().LineToByteOffset(tableInfo->startLine);
    const std::size_t blockEndByte   = (tableInfo->endLine < buffer.Content().LineCount())
                                           ? buffer.Content().LineToByteOffset(tableInfo->endLine)
                                           : buffer.Content().ByteLength();

    // Row-major advance across header + data rows (never the delimiter),
    // wrapping from the last cell back to the header's own first.
    std::vector<std::pair<std::size_t, std::size_t>> realCells;
    for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
        for (std::size_t col = 0; col < widths.size(); ++col)
            realCells.emplace_back(row, col);
    }
    std::size_t currentIndex = 0;
    for (std::size_t i = 0; i < realCells.size(); ++i) {
        if (realCells[i].first == currentRow && realCells[i].second == currentCol) {
            currentIndex = i;
            break;
        }
    }
    const auto [targetRow, targetCol]       = realCells[(currentIndex + 1) % realCells.size()];
    const std::size_t targetOffsetInNewText = newCellOffsets[targetRow][targetCol];

    buffer.DeleteRange(blockStartByte, blockEndByte - blockStartByte);
    buffer.InsertAt(blockStartByte, newText);
    buffer.SetPoint(blockStartByte + targetOffsetInNewText);
    return true;
}

} // namespace ned::editor::markdown
