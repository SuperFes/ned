#include "Minimap.h"

#include <algorithm>
#include <cstdint>

#include "Editor/MinimapSettings.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Bit position within a braille cell for (subColumnInCell, subRowInCell)
    // -- subColumnInCell in [0,1], subRowInCell in [0,3]. Standard Unicode
    // braille dot numbering: column 0 is dots 1/2/3/7, column 1 is dots
    // 4/5/6/8, top-to-bottom -- expressed here as the bit each dot sets
    // relative to the U+2800 base codepoint.
    constexpr std::uint8_t kBrailleBit[2][4] = {
        {0x01, 0x02, 0x04, 0x40},
        {0x08, 0x10, 0x20, 0x80},
    };

    // A copy of one HighlightSpan plus its position in the original
    // mode_.highlight() result -- kept so a sorted-by-startByte copy can
    // still resolve an overlap the same "later in the original vector
    // wins" way BufferView's own ClassAtOffset does, per Mode.h's own
    // documented convention.
    struct IndexedSpan {
        std::size_t         startByte;
        std::size_t         endByte;
        editor::SyntaxClass syntaxClass;
        std::size_t         originalIndex;
    };

    // Best-effort syntax class at offset: binary-searches sortedSpans (by
    // startByte) for the latest-starting span that could contain offset,
    // then walks backward through a small, bounded window of
    // earlier-starting-but-still-open candidates, taking whichever has the
    // highest originalIndex (the "later wins" rule) among those that
    // actually contain offset. Deliberately not a full interval-tree
    // resolution -- real overlap depth at any single point is small in
    // practice (a handful of nested captures at most), and this is a
    // cosmetic minimap, not the real highlighter (see this file's own
    // header comment on why a second, independent, approximate pass here
    // is fine rather than reusing BufferView's own private machinery).
    editor::SyntaxClass ClassAt(const std::vector<IndexedSpan>& sortedSpans, std::size_t offset) {
        constexpr std::size_t kMaxBackwardScan = 64;
        auto it = std::upper_bound(sortedSpans.begin(), sortedSpans.end(), offset,
                                    [](std::size_t value, const IndexedSpan& span) { return value < span.startByte; });
        std::size_t          scanned = 0;
        std::size_t           bestIndex = 0;
        bool                  found     = false;
        editor::SyntaxClass   best      = editor::SyntaxClass::Default;
        while (it != sortedSpans.begin() && scanned < kMaxBackwardScan) {
            --it;
            ++scanned;
            if (it->startByte <= offset && offset < it->endByte) {
                if (!found || it->originalIndex > bestIndex) {
                    best      = it->syntaxClass;
                    bestIndex = it->originalIndex;
                    found     = true;
                }
            }
        }
        return best;
    }

    [[nodiscard]] bool IsBlank(char32_t codepoint) {
        return codepoint == U' ' || codepoint == U'\t' || codepoint == U'\n' || codepoint == U'\r';
    }

} // namespace

Minimap::Minimap(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme)
    : activeBuffer_(activeBuffer), mode_(mode), theme_(theme) {
}

void Minimap::SetOnScroll(std::function<void(int)> onScroll) {
    onScroll_ = std::move(onScroll);
}

void Minimap::EnsureCache() const {
    text::Buffer& buffer      = activeBuffer_.Get();
    const int      height     = size().height;
    const int      width      = editor::MinimapWidth();
    const int      charsPerDot = editor::MinimapCharsPerDot();

    if (cacheBuffer_ == &buffer && cacheContentGeneration_ == buffer.ContentGeneration() && cacheHeight_ == height &&
        cacheWidth_ == width && cacheCharsPerDot_ == charsPerDot) {
        return;
    }

    cacheBuffer_            = &buffer;
    cacheContentGeneration_ = buffer.ContentGeneration();
    cacheHeight_            = height;
    cacheWidth_             = width;
    cacheCharsPerDot_       = charsPerDot;

    grid_.assign(static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0)), MinimapCell{});
    if (width <= 0 || height <= 0) {
        return;
    }

    const text::Rope& content    = buffer.Content();
    const std::size_t totalLines = content.LineCount();
    if (totalLines == 0) {
        return;
    }

    std::vector<IndexedSpan> sortedSpans;
    if (mode_.highlight) {
        const std::vector<editor::HighlightSpan> spans = mode_.highlight(buffer.Text());
        sortedSpans.reserve(spans.size());
        for (std::size_t i = 0; i < spans.size(); ++i) {
            sortedSpans.push_back(IndexedSpan{spans[i].startByte, spans[i].endByte, spans[i].syntaxClass, i});
        }
        std::sort(sortedSpans.begin(), sortedSpans.end(),
                  [](const IndexedSpan& a, const IndexedSpan& b) { return a.startByte < b.startByte; });
    }

    const int subRows = height * 4;
    const int subCols = width * 2;
    // "Only go as deep to the right as 1-2 chars per pixel" -- the user's
    // own framing, not a compression ratio: a line longer than this simply
    // isn't rendered past this column, no attempt to squeeze it in.
    const int maxColumn = subCols * std::max(charsPerDot, 1);

    std::vector<bool> colorPicked(grid_.size(), false);

    for (int subRow = 0; subRow < subRows; ++subRow) {
        const std::size_t lineStart =
            static_cast<std::size_t>((static_cast<long long>(subRow) * static_cast<long long>(totalLines)) / subRows);
        std::size_t lineEnd =
            static_cast<std::size_t>((static_cast<long long>(subRow + 1) * static_cast<long long>(totalLines)) / subRows);
        if (lineEnd <= lineStart) {
            lineEnd = lineStart + 1;
        }
        lineEnd             = std::min(lineEnd, totalLines);
        const int cellRow   = subRow / 4;
        const int bitRow    = subRow % 4;

        for (std::size_t line = lineStart; line < lineEnd; ++line) {
            const std::size_t lineStartByte = content.LineToByteOffset(line);
            const std::size_t lineEndByte =
                (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) : content.ByteLength();

            std::size_t offset = lineStartByte;
            int         column = 0;
            while (offset < lineEndByte && column < maxColumn) {
                const auto decoded = content.CodepointAt(offset);
                if (!IsBlank(decoded.codepoint)) {
                    const int subCol = column / std::max(charsPerDot, 1);
                    if (subCol < subCols) {
                        const int         cellCol   = subCol / 2;
                        const int         bitCol    = subCol % 2;
                        const std::size_t cellIndex = static_cast<std::size_t>(cellRow) * static_cast<std::size_t>(width) +
                                                      static_cast<std::size_t>(cellCol);
                        grid_[cellIndex].glyph = static_cast<char32_t>(grid_[cellIndex].glyph | kBrailleBit[bitCol][bitRow]);

                        if (!colorPicked[cellIndex] && !sortedSpans.empty()) {
                            const editor::SyntaxClass cls = ClassAt(sortedSpans, offset);
                            if (cls != editor::SyntaxClass::Default) {
                                grid_[cellIndex].foreground = theme_.BrushFor(cls).foreground;
                                colorPicked[cellIndex]      = true;
                            }
                        }
                    }
                }
                ++column;
                offset += decoded.byteLength;
            }
        }
    }
}

void Minimap::Paint(Canvas c) {
    EnsureCache();

    const int width  = cacheWidth_;
    const int height = cacheHeight_;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Viewport band: which terminal rows correspond to the currently-
    // visible buffer range -- identical proportional math to ScrollBar's
    // own thumbStart/thumbRows (ScrollBar.cpp), just marking rows with a
    // tinted background instead of drawing a separate thumb glyph.
    const int total = std::max(scrollable_length, 1);
    const int bandRows =
        std::clamp(static_cast<int>((static_cast<long long>(item_visual_length) * height) / total), 1, height);
    const int maxBandStart = height - bandRows;
    const int bandStart =
        (total > item_visual_length)
            ? std::clamp(static_cast<int>((static_cast<long long>(position) * maxBandStart) /
                                          std::max(total - item_visual_length, 1)),
                         0, maxBandStart)
            : 0;

    for (int y = 0; y < height; ++y) {
        const bool inBand = y >= bandStart && y < bandStart + bandRows;
        for (int x = 0; x < width; ++x) {
            const MinimapCell& cell = grid_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(x)];
            Cell&               out  = c[{.x = x, .y = y}];
            out.character            = text::EncodeCodepointUtf8(cell.glyph);
            out.foreground_color     = (cell.foreground == Color::Default) ? theme_.lineNumberForeground : cell.foreground;
            out.background_color     = inBand ? theme_.selectionBackground : theme_.background;
        }
    }
}

int Minimap::PositionForRow(int row) const {
    const int height = size().height;
    if (height <= 0) {
        return 0;
    }
    const int total = std::max(scrollable_length, 1);
    if (total <= item_visual_length) {
        return 0;
    }
    const int maxPosition = total - item_visual_length;
    const int clampedRow  = std::clamp(row, 0, height - 1);
    return std::clamp(static_cast<int>((static_cast<long long>(clampedRow) * total) / height), 0, maxPosition);
}

bool Minimap::OnEvent(const Event& event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            dragging_ = true;
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
        if (mouse->motion == MouseEvent::Motion::Moved && dragging_) {
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
    }
    if (event.is_mouse() && event.mouse().motion == MouseEvent::Motion::Released && dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

} // namespace ned::ui
