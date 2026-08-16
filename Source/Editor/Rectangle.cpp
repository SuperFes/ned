#include "Rectangle.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

namespace ned::editor {

void RectangleClipboard::Set(std::vector<std::string> lines) {
    lines_ = std::move(lines);
}

const std::vector<std::string>& RectangleClipboard::Lines() const {
    return lines_;
}

bool RectangleClipboard::Empty() const {
    return lines_.empty();
}

namespace {

    std::mutex& ClipboardMutex() {
        static std::mutex mutex;
        return mutex;
    }

    RectangleClipboard& ClipboardStorage() {
        static RectangleClipboard clipboard;
        return clipboard;
    }

} // namespace

void SetRectangleClipboard(std::vector<std::string> lines) {
    const std::lock_guard<std::mutex> lock(ClipboardMutex());
    ClipboardStorage().Set(std::move(lines));
}

const RectangleClipboard& GlobalRectangleClipboard() {
    const std::lock_guard<std::mutex> lock(ClipboardMutex());
    return ClipboardStorage();
}

RectangleBounds ComputeRectangleBounds(const text::Buffer& buffer, std::size_t tabWidth) {
    const std::size_t pointOffset = buffer.Point();
    const std::size_t markOffset  = buffer.Mark();

    const std::size_t pointLine = buffer.Content().ByteOffsetToLine(pointOffset);
    const std::size_t markLine  = buffer.Content().ByteOffsetToLine(markOffset);

    const std::size_t pointColumn =
        buffer.VisualColumnForByteOffset(buffer.Content().LineToByteOffset(pointLine), pointOffset, tabWidth);
    const std::size_t markColumn =
        buffer.VisualColumnForByteOffset(buffer.Content().LineToByteOffset(markLine), markOffset, tabWidth);

    return RectangleBounds{
        std::min(pointLine, markLine),
        std::max(pointLine, markLine),
        std::min(pointColumn, markColumn),
        std::max(pointColumn, markColumn),
    };
}

std::vector<std::string> DeleteRectangleLines(text::Buffer& buffer, const RectangleBounds& bounds,
                                              std::size_t tabWidth) {
    std::vector<std::string> lines;
    lines.reserve(bounds.endLine - bounds.startLine + 1);
    for (std::size_t line = bounds.startLine; line <= bounds.endLine; ++line) {
        const std::size_t byteStart = buffer.ByteOffsetForLineAndColumn(line, bounds.startColumn, tabWidth);
        const std::size_t byteEnd   = buffer.ByteOffsetForLineAndColumn(line, bounds.endColumn, tabWidth);
        lines.push_back(buffer.DeleteRange(byteStart, byteEnd - byteStart));
    }
    return lines;
}

void KillRectangle(text::Buffer& buffer, std::size_t tabWidth) {
    const RectangleBounds    bounds = ComputeRectangleBounds(buffer, tabWidth);
    std::vector<std::string> lines  = DeleteRectangleLines(buffer, bounds, tabWidth);
    SetRectangleClipboard(std::move(lines));
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(bounds.startLine, bounds.startColumn, tabWidth));
    buffer.ClearMark();
}

void DeleteRectangle(text::Buffer& buffer, std::size_t tabWidth) {
    const RectangleBounds bounds = ComputeRectangleBounds(buffer, tabWidth);
    (void)DeleteRectangleLines(buffer, bounds, tabWidth); // discarded on purpose -- delete-rectangle doesn't save
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(bounds.startLine, bounds.startColumn, tabWidth));
    buffer.ClearMark();
}

void YankRectangle(text::Buffer& buffer, std::size_t tabWidth) {
    const RectangleClipboard& clipboard = GlobalRectangleClipboard();

    const std::size_t startLine = buffer.Content().ByteOffsetToLine(buffer.Point());
    const std::size_t targetColumn =
        buffer.VisualColumnForByteOffset(buffer.Content().LineToByteOffset(startLine), buffer.Point(), tabWidth);

    for (std::size_t i = 0; i < clipboard.Lines().size(); ++i) {
        const std::size_t currentLine = startLine + i;
        if (currentLine >= buffer.Content().LineCount()) {
            buffer.InsertAt(buffer.Size(), "\n");
        }

        const std::size_t lineStart = buffer.Content().LineToByteOffset(currentLine);
        // Clamping column to a huge value lands exactly at the line's own
        // real end -- ByteOffsetForLineAndColumn's own existing clamping
        // behavior, confirmed safe regardless of how large the column is.
        const std::size_t lineEnd =
            buffer.ByteOffsetForLineAndColumn(currentLine, std::numeric_limits<std::size_t>::max(), tabWidth);
        const std::size_t lineVisualLength = buffer.VisualColumnForByteOffset(lineStart, lineEnd, tabWidth);

        std::size_t insertAt;
        if (lineVisualLength < targetColumn) {
            // Pad short destination lines with spaces so the yanked columns
            // stay visually aligned, real Emacs' own yank-rectangle
            // behavior -- not just inserted wherever the line's actual
            // (short) end happens to be.
            const std::string pad(targetColumn - lineVisualLength, ' ');
            buffer.InsertAt(lineEnd, pad);
            insertAt = lineEnd + pad.size();
        }
        else {
            insertAt = buffer.ByteOffsetForLineAndColumn(currentLine, targetColumn, tabWidth);
        }
        buffer.InsertAt(insertAt, clipboard.Lines()[i]);
    }
}

void StringRectangle(text::Buffer& buffer, std::string_view replacement, std::size_t tabWidth) {
    const RectangleBounds bounds = ComputeRectangleBounds(buffer, tabWidth);
    // Delete-then-insert per line, immediately, rather than deleting every
    // line first and inserting afterward -- both are equally correct (line
    // numbers stay stable either way, see DeleteRectangleLines' own doc
    // comment), but this needs no second pass recomputing byte offsets.
    for (std::size_t line = bounds.startLine; line <= bounds.endLine; ++line) {
        const std::size_t byteStart = buffer.ByteOffsetForLineAndColumn(line, bounds.startColumn, tabWidth);
        const std::size_t byteEnd   = buffer.ByteOffsetForLineAndColumn(line, bounds.endColumn, tabWidth);
        buffer.DeleteRange(byteStart, byteEnd - byteStart);
        buffer.InsertAt(byteStart, replacement);
    }
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(bounds.startLine, bounds.startColumn, tabWidth));
    buffer.ClearMark();
}

} // namespace ned::editor
