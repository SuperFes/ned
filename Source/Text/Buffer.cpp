#include "Buffer.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "Grapheme.h"

namespace ned::text {

namespace {
    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

    // ASCII alphanumeric + underscore -- deliberately not Unicode-aware (see
    // MoveForwardWord/MoveBackwardWord's doc comment in Buffer.h).
    bool IsWordCodepoint(char32_t codepoint) {
        return (codepoint >= U'a' && codepoint <= U'z') || (codepoint >= U'A' && codepoint <= U'Z') ||
               (codepoint >= U'0' && codepoint <= U'9') || codepoint == U'_';
    }

    // See VisualColumnForByteOffset's own doc comment in Buffer.h for why
    // this exists -- comfortably wider than any real terminal (even an
    // extreme ultra-wide setup), so every realistic file's tab-aware
    // goal-column tracking is exact; only bounds the walk for a
    // pathologically long single line. Each step calls Rope::CodepointAt, an
    // O(log document size) tree descent, not a free array index, so this
    // constant also caps the real per-call cost of landing a huge
    // carried-over goal column back onto an equally huge line -- a cost an
    // earlier version of this fix under-counted at 4096, regressing the
    // "hold next-line/previous-line" shape of the [Performance]
    // vertical-motion test to multiple seconds before it was caught.
    constexpr std::size_t kMaxTabAwareColumnScan = 512;
} // namespace

Buffer::Buffer(std::string name, Rope initialContent) : Name_(std::move(name)),
                                                        Rope_(initialContent),
                                                        UndoTree_(std::move(initialContent)) {
}

Buffer Buffer::FromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot open file for reading: " + path.string());
    }

    std::uintmax_t size;
    try {
        size = std::filesystem::file_size(path);
    }
    catch (const std::filesystem::filesystem_error&) {
        throw std::runtime_error("ned: cannot determine size of file: " + path.string());
    }

    // Bulk read rather than istreambuf_iterator's byte-at-a-time extraction.
    std::string content(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), static_cast<std::streamsize>(size));
    if (file.bad()) {
        throw std::runtime_error("ned: error reading file: " + path.string());
    }
    content.resize(static_cast<std::size_t>(file.gcount())); // handles a short read (e.g. file shrank concurrently)

    // Ned assumes UTF-8/ASCII content -- no charset auto-detection, matching
    // most modern editors' default -- so a leading BOM is the one
    // encoding-related artifact worth stripping explicitly.
    if (content.starts_with(kUtf8Bom)) {
        content.erase(0, kUtf8Bom.size());
    }

    Buffer buffer(path.filename().string(), Rope(content));
    buffer.Path_ = path;
    return buffer;
}

Buffer Buffer::NewFile(std::filesystem::path path) {
    Buffer buffer(path.filename().string());
    buffer.Path_ = std::move(path);
    return buffer;
}

void Buffer::SaveToFile(const std::filesystem::path& path) {
    // Write to a sibling temp file and rename over the target so a failure
    // partway through (e.g. disk full) can't leave the original truncated or
    // corrupted -- std::filesystem::rename is atomic on POSIX when both
    // paths are on the same filesystem, which a sibling file guarantees.
    const std::filesystem::path tempPath = path.string() + ".ned-tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open file for writing: " + tempPath.string());
        }

        const std::string content = Rope_.ToString();
        file.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!file) {
            file.close();
            std::filesystem::remove(tempPath);
            throw std::runtime_error("ned: error writing file: " + tempPath.string());
        }
    } // closed here, so its contents are flushed before the rename below

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("ned: cannot save file: " + path.string() + " (" + ec.message() + ")");
    }

    Path_     = path;
    Modified_ = false;
}

void Buffer::Save() {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    SaveToFile(*Path_);
}

const std::string& Buffer::Name() const {
    return Name_;
}

void Buffer::Rename(std::string name) {
    Name_ = std::move(name);
}

const std::optional<std::filesystem::path>& Buffer::Path() const {
    return Path_;
}

void Buffer::SetPath(std::filesystem::path path) {
    Path_ = std::move(path);
}

const Rope& Buffer::Content() const {
    return Rope_;
}

std::string Buffer::Text() const {
    return Rope_.ToString();
}

std::size_t Buffer::Size() const {
    return Rope_.ByteLength();
}

bool Buffer::Modified() const {
    return Modified_;
}

std::size_t Buffer::ContentGeneration() const {
    return ContentGeneration_;
}

std::size_t Buffer::Point() const {
    return Point_;
}

void Buffer::SetPoint(std::size_t byteOffset) {
    Point_    = SnapToGraphemeBoundary(Rope_, byteOffset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::SetMark(std::size_t byteOffset) {
    Mark_ = SnapToGraphemeBoundary(Rope_, byteOffset);
}

void Buffer::ClearMark() {
    Mark_.reset();
}

bool Buffer::HasMark() const {
    return Mark_.has_value();
}

std::size_t Buffer::Mark() const {
    return *Mark_;
}

std::pair<std::size_t, std::size_t> Buffer::Region() const {
    const std::size_t mark = *Mark_;
    return Point_ <= mark ? std::pair{Point_, mark} : std::pair{mark, Point_};
}

void Buffer::InsertAtPoint(std::string_view text) {
    if (text.empty()) {
        return;
    }

    const std::size_t insertOffset = Point_;
    Rope_                          = Rope_.Inserted(insertOffset, text);
    Point_                         = insertOffset + text.size();

    if (Mark_ && *Mark_ >= insertOffset) {
        *Mark_ += text.size();
    }

    if (CanAmend_) {
        UndoTree_.Amend(Rope_);
    }
    else {
        UndoTree_.Record(Rope_);
        CanAmend_ = true;
    }
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
}

void Buffer::DeleteBackwardAtPoint() {
    if (Point_ == 0) {
        return;
    }

    const std::size_t start = PreviousGraphemeBoundary(Rope_, Point_);
    Rope_                   = Rope_.Erased(start, Point_ - start);

    if (Mark_) {
        if (*Mark_ > Point_) {
            *Mark_ -= (Point_ - start);
        }
        else if (*Mark_ > start) {
            *Mark_ = start;
        }
    }

    Point_    = start;
    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
    UndoTree_.Record(Rope_);
}

void Buffer::DeleteForwardAtPoint() {
    if (Point_ >= Rope_.ByteLength()) {
        return;
    }

    const std::size_t end = NextGraphemeBoundary(Rope_, Point_);
    Rope_                 = Rope_.Erased(Point_, end - Point_);

    if (Mark_) {
        if (*Mark_ >= end) {
            *Mark_ -= (end - Point_);
        }
        else if (*Mark_ > Point_) {
            *Mark_ = Point_;
        }
    }

    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
    UndoTree_.Record(Rope_);
}

std::string Buffer::DeleteRange(std::size_t byteOffset, std::size_t byteLength) {
    byteOffset = std::min(byteOffset, Rope_.ByteLength());
    byteLength = std::min(byteLength, Rope_.ByteLength() - byteOffset);

    if (byteLength == 0) {
        return {};
    }

    const std::size_t rangeEnd = byteOffset + byteLength;
    std::string       deleted  = Rope_.Substring(byteOffset, byteLength);
    Rope_                      = Rope_.Erased(byteOffset, byteLength);

    if (Point_ >= rangeEnd) {
        Point_ -= byteLength;
    }
    else if (Point_ > byteOffset) {
        Point_ = byteOffset;
    }

    if (Mark_) {
        if (*Mark_ >= rangeEnd) {
            *Mark_ -= byteLength;
        }
        else if (*Mark_ > byteOffset) {
            *Mark_ = byteOffset;
        }
    }

    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
    UndoTree_.Record(Rope_);
    return deleted;
}

void Buffer::InsertAt(std::size_t byteOffset, std::string_view text) {
    byteOffset = std::min(byteOffset, Rope_.ByteLength());

    if (text.empty()) {
        return;
    }

    Rope_ = Rope_.Inserted(byteOffset, text);

    if (Point_ >= byteOffset) {
        Point_ += text.size();
    }
    if (Mark_ && *Mark_ >= byteOffset) {
        *Mark_ += text.size();
    }

    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
    UndoTree_.Record(Rope_);
}

void Buffer::MoveForward() {
    Point_    = NextGraphemeBoundary(Rope_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackward() {
    Point_    = PreviousGraphemeBoundary(Rope_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveForwardWord() {
    const std::size_t total  = Rope_.ByteLength();
    std::size_t       offset = Point_;

    while (offset < total && !IsWordCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }
    while (offset < total && IsWordCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackwardWord() {
    std::size_t offset = Point_;

    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (IsWordCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (!IsWordCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

std::size_t Buffer::ByteOffsetForLineAndColumn(std::size_t line, std::size_t column, std::size_t tabWidth) const {
    const std::size_t totalLines = Rope_.LineCount();
    line                         = std::min(line, totalLines - 1);

    const std::size_t lineStart = Rope_.LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? Rope_.LineToByteOffset(line + 1) - 1 : Rope_.ByteLength();

    if (tabWidth <= 1) {
        const std::size_t lineStartCodepoint = Rope_.ByteOffsetToCodepointOffset(lineStart);
        const std::size_t lineLength         = Rope_.ByteOffsetToCodepointOffset(lineEnd) - lineStartCodepoint;

        const std::size_t landingCodepoint = lineStartCodepoint + std::min(column, lineLength);
        return Rope_.CodepointOffsetToByteOffset(landingCodepoint);
    }

    // In the common case this is bounded by `column` itself -- the walk
    // stops the moment the accumulated visual column reaches it. But
    // `column` isn't always screen-width-small: MoveToLine can carry over a
    // GoalColumn_ approximated from a pathologically long *other* line (see
    // VisualColumnForByteOffset), and landing that huge column on an equally
    // long target line would walk the whole thing. kMaxTabAwareColumnScan
    // caps that the same way, falling back to plain codepoint arithmetic
    // (clamped to the line's actual end) for the remainder.
    std::size_t offset       = lineStart;
    std::size_t visualColumn = 0;
    std::size_t steps        = 0;
    while (offset < lineEnd && visualColumn < column) {
        if (steps >= kMaxTabAwareColumnScan) {
            const std::size_t remainingColumns = column - visualColumn;
            const std::size_t lineEndCodepoint = Rope_.ByteOffsetToCodepointOffset(lineEnd);
            const std::size_t landingCodepoint = std::min(Rope_.ByteOffsetToCodepointOffset(offset) + remainingColumns,
                                                          lineEndCodepoint);
            return Rope_.CodepointOffsetToByteOffset(landingCodepoint);
        }
        const auto decoded = Rope_.CodepointAt(offset);
        visualColumn += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return offset;
}

std::size_t Buffer::VisualColumnForByteOffset(std::size_t lineStart, std::size_t byteOffset,
                                              std::size_t tabWidth) const {
    if (tabWidth <= 1) {
        return Rope_.ByteOffsetToCodepointOffset(byteOffset) - Rope_.ByteOffsetToCodepointOffset(lineStart);
    }

    std::size_t offset = lineStart;
    std::size_t column = 0;
    std::size_t steps  = 0;
    while (offset < byteOffset) {
        if (steps >= kMaxTabAwareColumnScan) {
            // Bail out to a plain codepoint-distance approximation for the
            // remainder -- see kMaxTabAwareColumnScan's own comment.
            return column + (Rope_.ByteOffsetToCodepointOffset(byteOffset) - Rope_.ByteOffsetToCodepointOffset(offset));
        }
        const auto decoded = Rope_.CodepointAt(offset);
        column += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return column;
}

void Buffer::MoveToLine(std::size_t targetLine, std::size_t tabWidth) {
    const std::size_t currentLineStart = Rope_.LineToByteOffset(Rope_.ByteOffsetToLine(Point_));
    const std::size_t desiredColumn    = GoalColumn_.value_or(VisualColumnForByteOffset(currentLineStart, Point_, tabWidth));

    const std::size_t landingByte = ByteOffsetForLineAndColumn(targetLine, desiredColumn, tabWidth);

    Point_      = SnapToGraphemeBoundary(Rope_, landingByte);
    GoalColumn_ = desiredColumn; // the un-clamped goal, not necessarily where we landed
    CanAmend_   = false;
}

void Buffer::MoveDownLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Rope_.ByteOffsetToLine(Point_);
    const std::size_t lastLine    = Rope_.LineCount() - 1;
    if (currentLine == lastLine) {
        return; // already on the last line -- true no-op, regardless of any stale goal column
    }
    // Clamped rather than a plain currentLine + count: a page-down whose
    // count overshoots the end of a short buffer should still land on the
    // last line instead of doing nothing.
    MoveToLine(std::min(currentLine + count, lastLine), tabWidth);
}

void Buffer::MoveUpLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Rope_.ByteOffsetToLine(Point_);
    if (currentLine == 0) {
        return; // already on the first line -- true no-op
    }
    MoveToLine(count > currentLine ? 0 : currentLine - count, tabWidth);
}

void Buffer::MoveToNextLine(std::size_t tabWidth) {
    MoveDownLines(1, tabWidth);
}

void Buffer::MoveToPreviousLine(std::size_t tabWidth) {
    MoveUpLines(1, tabWidth);
}

bool Buffer::CanUndo() const {
    return UndoTree_.CanUndo();
}

bool Buffer::CanRedo() const {
    return UndoTree_.CanRedo();
}

void Buffer::ClampCursorsToContent() {
    Point_ = SnapToGraphemeBoundary(Rope_, std::min(Point_, Rope_.ByteLength()));
    if (Mark_) {
        Mark_ = SnapToGraphemeBoundary(Rope_, std::min(*Mark_, Rope_.ByteLength()));
    }
}

void Buffer::Undo() {
    if (!UndoTree_.CanUndo()) {
        return;
    }
    UndoTree_.Undo();
    Rope_ = UndoTree_.Current();
    ClampCursorsToContent();
    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
}

void Buffer::Redo() {
    if (!UndoTree_.CanRedo()) {
        return;
    }
    UndoTree_.Redo();
    Rope_ = UndoTree_.Current();
    ClampCursorsToContent();
    CanAmend_ = false;
    GoalColumn_.reset();
    Modified_ = true;
    ++ContentGeneration_;
}

} // namespace ned::text
