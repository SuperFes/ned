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

    // The shared machinery behind every table-editing op (mirrors Org.cpp's
    // own OrgTableCell/LocateOrgTableCell/RewriteOrgTable trio) -- factored
    // out once every row/column op turned out to need the exact same
    // locate-cell / mutate-grid / rebuild-and-realign steps with only the
    // grid mutation differing. Simpler than Org's version in one respect:
    // every entry in `rows` here (header included) is real cell content --
    // there's no separator-row bookkeeping mixed into the grid, since GFM's
    // delimiter row is tracked separately as columnAlignments and never
    // stored in rows at all.

    struct TableCell {
        std::size_t row = 0;
        std::size_t col = 0;
    };

    // Which cell point currently sits in -- falls back to the table's first
    // cell (the header's own first cell) if point isn't cleanly inside any
    // span (e.g. sitting on the delimiter row's own line, which carries no
    // cell spans at all).
    TableCell LocateTableCell(const text::Buffer& buffer, const Table& tableInfo) {
        const std::string bufferText = buffer.Text();
        const auto        lines      = ScanBlockLines(bufferText, tableInfo.startLine, tableInfo.endLine);

        std::vector<std::vector<std::pair<std::size_t, std::size_t>>> cellSpans(tableInfo.rows.size());
        cellSpans[0] = table::CellByteSpans(lines[0].first, lines[0].second);
        for (std::size_t i = 2; i < lines.size(); ++i) {
            cellSpans[i - 1] = table::CellByteSpans(lines[i].first, lines[i].second);
        }

        const std::size_t point = buffer.Point();
        for (std::size_t row = 0; row < cellSpans.size(); ++row) {
            for (std::size_t col = 0; col < cellSpans[row].size(); ++col) {
                if (point >= cellSpans[row][col].first && point <= cellSpans[row][col].second) {
                    return {row, col};
                }
            }
        }
        return {0, 0};
    }

    // Line index (0-based, relative to tableInfo.startLine) that point's own
    // line falls on, clamped into the block -- 0 is always the header, 1
    // always the delimiter, 2.. the data rows in order.
    std::size_t RelativeLineAtPoint(const text::Buffer& buffer, const Table& tableInfo) {
        const std::size_t pointLine = buffer.Content().ByteOffsetToLine(buffer.Point());
        const std::size_t blockLen  = tableInfo.endLine - tableInfo.startLine;
        return std::min(pointLine < tableInfo.startLine ? std::size_t{0} : pointLine - tableInfo.startLine,
                        blockLen - 1);
    }

    // Real column count -- the widest row across the whole grid (header
    // included), matching what table::ComputeColumnWidths itself derives.
    std::size_t TableColumnCount(const std::vector<std::vector<std::string>>& rows) {
        std::size_t count = 0;
        for (const auto& row : rows)
            count = std::max(count, row.size());
        return count;
    }

    // Pads every row (header included) to columnCount with empty cells, and
    // columnAlignments to the same length with Default -- so a column index
    // means the same thing in every row and in the alignment vector before
    // a column op touches them (ragged input is tolerated the same way
    // Org's own PadDataRows tolerates it).
    void PadTableGrid(std::vector<std::vector<std::string>>& rows, std::vector<table::Alignment>& columnAlignments,
                      std::size_t columnCount) {
        for (auto& row : rows) {
            if (row.size() < columnCount)
                row.resize(columnCount);
        }
        if (columnAlignments.size() < columnCount)
            columnAlignments.resize(columnCount, table::Alignment::Default);
    }

    // Rebuilds the whole block from the (possibly mutated) rows/
    // columnAlignments grid, realigned to content width, replaces the
    // original block's bytes with it, and lands point on
    // (targetRow, targetCol), each clamped into the rebuilt grid's bounds.
    void RewriteTable(text::Buffer& buffer, const Table& original, const std::vector<std::vector<std::string>>& rows,
                      const std::vector<table::Alignment>& columnAlignments, std::size_t targetRow,
                      std::size_t targetCol) {
        const std::vector<std::size_t> widths = table::ComputeColumnWidths(rows);

        std::string                           newText;
        std::vector<std::vector<std::size_t>> newCellOffsets(rows.size());
        auto                                  renderRow = [&](std::size_t rowIndex) {
            newText += '|';
            for (std::size_t col = 0; col < widths.size(); ++col) {
                newText += ' ';
                newCellOffsets[rowIndex].push_back(newText.size());
                const table::Alignment alignment =
                    col < columnAlignments.size() ? columnAlignments[col] : table::Alignment::Default;
                const std::string cellText = col < rows[rowIndex].size() ? rows[rowIndex][col] : std::string();
                newText += table::PadCell(cellText, widths[col], alignment);
                newText += " |";
            }
            newText += '\n';
        };

        renderRow(0); // header
        newText += '|';
        for (std::size_t col = 0; col < widths.size(); ++col) {
            const table::Alignment alignment =
                col < columnAlignments.size() ? columnAlignments[col] : table::Alignment::Default;
            const bool        leftColon   = alignment == table::Alignment::Left || alignment == table::Alignment::Center;
            const bool        rightColon  = alignment == table::Alignment::Right || alignment == table::Alignment::Center;
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
        for (std::size_t row = 1; row < rows.size(); ++row)
            renderRow(row);

        const bool blockHadTrailingNewline = original.endLine < buffer.Content().LineCount();
        if (!blockHadTrailingNewline && !newText.empty())
            newText.pop_back();

        const std::size_t blockStartByte = buffer.Content().LineToByteOffset(original.startLine);
        const std::size_t blockEndByte   = (original.endLine < buffer.Content().LineCount())
                                               ? buffer.Content().LineToByteOffset(original.endLine)
                                               : buffer.Content().ByteLength();

        buffer.DeleteRange(blockStartByte, blockEndByte - blockStartByte);
        buffer.InsertAt(blockStartByte, newText);

        if (widths.empty() || rows.empty()) {
            buffer.SetPoint(blockStartByte);
            return;
        }
        const std::size_t row = std::min(targetRow, rows.size() - 1);
        const std::size_t col = std::min(targetCol, widths.size() - 1);
        buffer.SetPoint(blockStartByte + newCellOffsets[row][col]);
    }

    // Realign + step point one cell forward/backward in row-major order --
    // AlignTableAtPoint and MoveToPreviousTableCellAtPoint are this with
    // only the direction (and forward's row auto-append) differing.
    bool StepTableCell(text::Buffer& buffer, bool forward) {
        const auto tableInfo = FindTableAtPoint(buffer);
        if (!tableInfo)
            return false;

        const TableCell   current     = LocateTableCell(buffer, *tableInfo);
        const std::size_t columnCount = std::max<std::size_t>(TableColumnCount(tableInfo->rows), 1);

        std::vector<TableCell> realCells;
        for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
            for (std::size_t col = 0; col < columnCount; ++col)
                realCells.push_back({row, col});
        }
        std::size_t currentIndex = 0;
        for (std::size_t i = 0; i < realCells.size(); ++i) {
            if (realCells[i].row == current.row && realCells[i].col == current.col) {
                currentIndex = i;
                break;
            }
        }

        if (forward) {
            if (currentIndex + 1 >= realCells.size()) {
                // Tabbing past the table's last cell: append a fresh empty
                // data row at the very end and land on its first cell --
                // real Org's/real GFM editors' own TAB behavior.
                auto rows = tableInfo->rows;
                rows.emplace_back();
                RewriteTable(buffer, *tableInfo, rows, tableInfo->columnAlignments, rows.size() - 1, 0);
                return true;
            }
            const TableCell target = realCells[currentIndex + 1];
            RewriteTable(buffer, *tableInfo, tableInfo->rows, tableInfo->columnAlignments, target.row, target.col);
            return true;
        }

        const TableCell target = realCells[(currentIndex + realCells.size() - 1) % realCells.size()];
        RewriteTable(buffer, *tableInfo, tableInfo->rows, tableInfo->columnAlignments, target.row, target.col);
        return true;
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
    return StepTableCell(buffer, /*forward=*/true);
}

bool MoveToPreviousTableCellAtPoint(text::Buffer& buffer) {
    return StepTableCell(buffer, /*forward=*/false);
}

bool InsertTableRowAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t relativeLine = RelativeLineAtPoint(buffer, *tableInfo);
    // relativeLine 0 (header) or 1 (delimiter): the new row becomes the
    // first data row, i.e. inserted at rows index 1. relativeLine >= 2
    // (an existing data row at rows index relativeLine - 1): inserted
    // above it, at that same index.
    const std::size_t insertAt = relativeLine <= 1 ? 1 : relativeLine - 1;
    const TableCell   cell     = LocateTableCell(buffer, *tableInfo);

    auto rows = tableInfo->rows;
    rows.emplace(rows.begin() + static_cast<std::ptrdiff_t>(insertAt));
    RewriteTable(buffer, *tableInfo, rows, tableInfo->columnAlignments, insertAt, cell.col);
    return true;
}

bool KillTableRowAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t relativeLine = RelativeLineAtPoint(buffer, *tableInfo);
    if (relativeLine <= 1)
        return false; // on the header or delimiter line -- nothing to kill
    const std::size_t dataRow = relativeLine - 1;
    const TableCell   cell    = LocateTableCell(buffer, *tableInfo);

    auto rows = tableInfo->rows;
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(dataRow));
    RewriteTable(buffer, *tableInfo, rows, tableInfo->columnAlignments, dataRow, cell.col);
    return true;
}

bool MoveTableRowUpAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t relativeLine = RelativeLineAtPoint(buffer, *tableInfo);
    if (relativeLine <= 1)
        return false; // header/delimiter line: nothing to move
    const std::size_t dataRow = relativeLine - 1;
    if (dataRow <= 1)
        return false; // already the first data row -- swapping up hits the header
    const TableCell cell = LocateTableCell(buffer, *tableInfo);

    auto rows = tableInfo->rows;
    std::swap(rows[dataRow], rows[dataRow - 1]);
    RewriteTable(buffer, *tableInfo, rows, tableInfo->columnAlignments, dataRow - 1, cell.col);
    return true;
}

bool MoveTableRowDownAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t relativeLine = RelativeLineAtPoint(buffer, *tableInfo);
    if (relativeLine <= 1)
        return false; // header/delimiter line: nothing to move
    const std::size_t dataRow = relativeLine - 1;
    if (dataRow + 1 >= tableInfo->rows.size())
        return false; // already the last data row
    const TableCell cell = LocateTableCell(buffer, *tableInfo);

    auto rows = tableInfo->rows;
    std::swap(rows[dataRow], rows[dataRow + 1]);
    RewriteTable(buffer, *tableInfo, rows, tableInfo->columnAlignments, dataRow + 1, cell.col);
    return true;
}

bool InsertTableColumnAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const TableCell cell = LocateTableCell(buffer, *tableInfo);

    auto              rows             = tableInfo->rows;
    auto              columnAlignments = tableInfo->columnAlignments;
    const std::size_t columnCount      = std::max<std::size_t>(TableColumnCount(rows), 1);
    PadTableGrid(rows, columnAlignments, columnCount);
    const std::size_t insertAt = std::min(cell.col + 1, columnCount);
    for (auto& row : rows)
        row.emplace(row.begin() + static_cast<std::ptrdiff_t>(insertAt));
    columnAlignments.emplace(columnAlignments.begin() + static_cast<std::ptrdiff_t>(insertAt), table::Alignment::Default);
    RewriteTable(buffer, *tableInfo, rows, columnAlignments, cell.row, insertAt);
    return true;
}

bool DeleteTableColumnAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t columnCount = TableColumnCount(tableInfo->rows);
    if (columnCount <= 1)
        return false; // a zero-column table has no representation in the `|`-line syntax
    const TableCell   cell = LocateTableCell(buffer, *tableInfo);
    const std::size_t col  = std::min(cell.col, columnCount - 1);

    auto rows             = tableInfo->rows;
    auto columnAlignments = tableInfo->columnAlignments;
    PadTableGrid(rows, columnAlignments, columnCount);
    for (auto& row : rows)
        row.erase(row.begin() + static_cast<std::ptrdiff_t>(col));
    columnAlignments.erase(columnAlignments.begin() + static_cast<std::ptrdiff_t>(col));
    RewriteTable(buffer, *tableInfo, rows, columnAlignments, cell.row, col);
    return true;
}

bool MoveTableColumnLeftAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t columnCount = TableColumnCount(tableInfo->rows);
    const TableCell   cell        = LocateTableCell(buffer, *tableInfo);
    const std::size_t col         = columnCount == 0 ? 0 : std::min(cell.col, columnCount - 1);
    if (col == 0)
        return false;

    auto rows             = tableInfo->rows;
    auto columnAlignments = tableInfo->columnAlignments;
    PadTableGrid(rows, columnAlignments, columnCount);
    for (auto& row : rows)
        std::swap(row[col], row[col - 1]);
    std::swap(columnAlignments[col], columnAlignments[col - 1]);
    RewriteTable(buffer, *tableInfo, rows, columnAlignments, cell.row, col - 1);
    return true;
}

bool MoveTableColumnRightAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindTableAtPoint(buffer);
    if (!tableInfo)
        return false;
    const std::size_t columnCount = TableColumnCount(tableInfo->rows);
    const TableCell   cell        = LocateTableCell(buffer, *tableInfo);
    if (cell.col + 1 >= columnCount)
        return false;

    auto rows             = tableInfo->rows;
    auto columnAlignments = tableInfo->columnAlignments;
    PadTableGrid(rows, columnAlignments, columnCount);
    for (auto& row : rows)
        std::swap(row[cell.col], row[cell.col + 1]);
    std::swap(columnAlignments[cell.col], columnAlignments[cell.col + 1]);
    RewriteTable(buffer, *tableInfo, rows, columnAlignments, cell.row, cell.col + 1);
    return true;
}

} // namespace ned::editor::markdown
