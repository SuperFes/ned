#include "MergeAlignment.h"

#include <algorithm>
#include <utility>

#include "LineDiff.h"

namespace ned::text {

namespace {

    // SplitLines pieces starting before `byte`. `byte` is a line start or
    // the end of the text.
    std::size_t LinesBefore(std::string_view text, std::size_t byte) {
        const auto newlines = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(byte), '\n'));
        return newlines + (byte > 0 && text[byte - 1] != '\n' ? 1 : 0);
    }

    std::optional<ConflictHunk::Range> SectionOf(const ConflictHunk& hunk, MergeSideKind kind) {
        switch (kind) {
            case MergeSideKind::Ours:
                return hunk.oursRange;
            case MergeSideKind::Theirs:
                return hunk.theirsRange;
            case MergeSideKind::Base:
                return hunk.baseRange;
        }
        return std::nullopt;
    }

    // A merged line every side has a definite counterpart for, and each
    // side's line there.
    struct SyncPoint {
        std::size_t              merged;
        std::vector<std::size_t> sides;
    };

    // The side line merged line `c` maps to, or nullopt when `c` falls
    // strictly inside a changed chunk. `index` only moves forward, so the
    // caller must ask for ascending `c`.
    std::optional<std::size_t> SideLineAt(const MergeSide& side, std::size_t& index, std::size_t c) {
        const auto& chunks = side.chunks;
        while (index < chunks.size()) {
            const AlignedChunk& chunk = chunks[index];
            if (chunk.mergedLine + chunk.mergedCount > c || (chunk.mergedCount == 0 && chunk.mergedLine == c)) {
                break;
            }
            ++index;
        }
        if (index == chunks.size()) {
            return side.lineCount;
        }
        const AlignedChunk& chunk = chunks[index];
        if (chunk.mergedLine == c) {
            return chunk.sideLine;
        }
        if (!chunk.changed) {
            return chunk.sideLine + (c - chunk.mergedLine);
        }
        return std::nullopt;
    }

} // namespace

std::optional<MergeSide> MergeSideFromMarkers(std::string_view merged, std::span<const ConflictHunk> hunks, MergeSideKind kind) {
    MergeSide   side;
    std::size_t copiedTo   = 0;
    std::size_t mergedLine = 0;

    auto appendUnchanged = [&](std::size_t end) {
        const std::size_t count = LinesBefore(merged, end) - mergedLine;
        if (count > 0) {
            side.chunks.push_back({mergedLine, count, side.lineCount, count, false});
        }
        side.text.append(merged.substr(copiedTo, end - copiedTo));
        side.lineCount += count;
        mergedLine += count;
        copiedTo = end;
    };

    for (const ConflictHunk& hunk : hunks) {
        const std::optional<ConflictHunk::Range> section = SectionOf(hunk, kind);
        if (!section) {
            return std::nullopt;
        }
        appendUnchanged(hunk.startByte);

        const std::string_view content     = merged.substr(section->start, section->end - section->start);
        const std::size_t      sideCount   = SplitLines(content).size();
        const std::size_t      mergedCount = LinesBefore(merged, hunk.endByte) - mergedLine;
        side.chunks.push_back({mergedLine, mergedCount, side.lineCount, sideCount, true});
        side.text.append(content);
        side.lineCount += sideCount;
        mergedLine += mergedCount;
        copiedTo = hunk.endByte;
    }
    appendUnchanged(merged.size());
    return side;
}

PaneAlignment::PaneAlignment(std::vector<std::size_t> rowOfLine) : rowOfLine_(std::move(rowOfLine)) {
}

std::size_t PaneAlignment::LeadingRows() const {
    return rowOfLine_.front();
}

std::size_t PaneAlignment::RowsAfter(std::size_t line) const {
    if (line + 1 >= rowOfLine_.size()) {
        return 0;
    }
    return rowOfLine_[line + 1] - rowOfLine_[line] - 1;
}

std::size_t PaneAlignment::AlignedRow(std::size_t line) const {
    return rowOfLine_[std::min(line, rowOfLine_.size() - 1)];
}

std::size_t PaneAlignment::LineAtAlignedRow(std::size_t row) const {
    const std::size_t lineCount = rowOfLine_.size() - 1;
    if (lineCount == 0) {
        return 0;
    }
    // The last line whose row is <= `row`.
    const auto it = std::upper_bound(rowOfLine_.begin(), rowOfLine_.begin() + static_cast<std::ptrdiff_t>(lineCount), row);
    if (it == rowOfLine_.begin()) {
        return 0;
    }
    return static_cast<std::size_t>(it - rowOfLine_.begin()) - 1;
}

std::size_t PaneAlignment::TotalRows() const {
    return rowOfLine_.back();
}

std::size_t PaneAlignment::LineCount() const {
    return rowOfLine_.size() - 1;
}

std::size_t PaneAlignment::RowAtTop(Top top) const {
    std::size_t above = top.padding;
    if (top.line == 0) {
        above += LeadingRows();
    }
    const std::size_t row = AlignedRow(top.line);
    return row > above ? row - above : 0;
}

PaneAlignment::Top PaneAlignment::TopForRow(std::size_t row) const {
    if (LineCount() == 0) {
        return {};
    }
    const std::size_t line = LineAtAlignedRow(row);
    if (AlignedRow(line) >= row) {
        return {.line = line, .padding = 0};
    }
    // `row` is one of the rows after `line`: start at the next line, with
    // the rest of them above it.
    if (line + 1 < LineCount()) {
        return {.line = line + 1, .padding = AlignedRow(line + 1) - row};
    }
    return {.line = line, .padding = 0};
}

std::vector<PaneAlignment> AlignPanes(std::size_t mergedLineCount, std::span<const MergeSide* const> sides) {
    std::vector<SyncPoint> syncPoints;
    syncPoints.push_back({0, std::vector<std::size_t>(sides.size(), 0)});

    std::vector<std::size_t> chunkIndex(sides.size(), 0);
    for (std::size_t c = 1; c < mergedLineCount; ++c) {
        SyncPoint point{c, {}};
        point.sides.reserve(sides.size());
        for (std::size_t k = 0; k < sides.size(); ++k) {
            const std::optional<std::size_t> line = SideLineAt(*sides[k], chunkIndex[k], c);
            if (!line) {
                break;
            }
            point.sides.push_back(*line);
        }
        if (point.sides.size() == sides.size()) {
            syncPoints.push_back(std::move(point));
        }
    }

    SyncPoint end{mergedLineCount, {}};
    for (const MergeSide* side : sides) {
        end.sides.push_back(side->lineCount);
    }
    syncPoints.push_back(std::move(end));

    // Pane 0 is merged; pane 1 + k is sides[k].
    const std::size_t                     paneCount = sides.size() + 1;
    std::vector<std::size_t>              leading(paneCount, 0);
    std::vector<std::vector<std::size_t>> rowsAfter(paneCount);
    rowsAfter[0].assign(mergedLineCount, 0);
    for (std::size_t k = 0; k < sides.size(); ++k) {
        rowsAfter[k + 1].assign(sides[k]->lineCount, 0);
    }

    auto lineOf = [](const SyncPoint& point, std::size_t pane) {
        return pane == 0 ? point.merged : point.sides[pane - 1];
    };

    for (std::size_t s = 0; s + 1 < syncPoints.size(); ++s) {
        const SyncPoint& from   = syncPoints[s];
        const SyncPoint& to     = syncPoints[s + 1];
        std::size_t      height = 0;
        for (std::size_t pane = 0; pane < paneCount; ++pane) {
            height = std::max(height, lineOf(to, pane) - lineOf(from, pane));
        }
        for (std::size_t pane = 0; pane < paneCount; ++pane) {
            const std::size_t pad = height - (lineOf(to, pane) - lineOf(from, pane));
            if (pad == 0) {
                continue;
            }
            const std::size_t segmentEnd = lineOf(to, pane);
            if (segmentEnd == 0) {
                leading[pane] += pad;
            }
            else {
                rowsAfter[pane][segmentEnd - 1] += pad;
            }
        }
    }

    std::vector<PaneAlignment> alignments;
    alignments.reserve(paneCount);
    for (std::size_t pane = 0; pane < paneCount; ++pane) {
        std::vector<std::size_t> rowOfLine;
        rowOfLine.reserve(rowsAfter[pane].size() + 1);
        std::size_t row = leading[pane];
        for (const std::size_t pad : rowsAfter[pane]) {
            rowOfLine.push_back(row);
            row += 1 + pad;
        }
        rowOfLine.push_back(row);
        alignments.emplace_back(std::move(rowOfLine));
    }
    return alignments;
}

} // namespace ned::text
