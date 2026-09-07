#include "ConflictResolution.h"

namespace ned::editor {

std::optional<text::ConflictHunk> ConflictHunkAtPoint(const text::Buffer& buffer, std::size_t point) {
    for (const text::ConflictHunk& hunk : text::ParseConflictHunks(buffer.Text())) {
        if (point >= hunk.startByte && point < hunk.endByte) {
            return hunk;
        }
    }
    return std::nullopt;
}

bool ResolveConflictHunk(text::Buffer& buffer, const text::ConflictHunk& hunk, ConflictResolution resolution) {
    if (resolution == ConflictResolution::KeepBase && !hunk.baseRange.has_value()) {
        return false;
    }

    const text::ITextStorage& content = buffer.Content();
    std::string                replacement;
    switch (resolution) {
        case ConflictResolution::TakeOurs:
            replacement = content.Substring(hunk.oursRange.start, hunk.oursRange.end - hunk.oursRange.start);
            break;
        case ConflictResolution::TakeTheirs:
            replacement = content.Substring(hunk.theirsRange.start, hunk.theirsRange.end - hunk.theirsRange.start);
            break;
        case ConflictResolution::TakeBoth:
            replacement = content.Substring(hunk.oursRange.start, hunk.oursRange.end - hunk.oursRange.start) +
                          content.Substring(hunk.theirsRange.start, hunk.theirsRange.end - hunk.theirsRange.start);
            break;
        case ConflictResolution::TakeNeither:
            replacement.clear();
            break;
        case ConflictResolution::KeepBase:
            replacement = content.Substring(hunk.baseRange->start, hunk.baseRange->end - hunk.baseRange->start);
            break;
    }

    buffer.BeginUndoGroup();
    buffer.DeleteRange(hunk.startByte, hunk.endByte - hunk.startByte);
    if (!replacement.empty()) {
        buffer.InsertAt(hunk.startByte, replacement);
    }
    buffer.EndUndoGroup();
    buffer.SetPoint(hunk.startByte);
    return true;
}

std::optional<std::size_t> NextConflictHunkStart(const text::Buffer& buffer, std::size_t point) {
    const std::vector<text::ConflictHunk> hunks = text::ParseConflictHunks(buffer.Text());
    if (hunks.empty()) {
        return std::nullopt;
    }
    for (const text::ConflictHunk& hunk : hunks) {
        if (hunk.startByte > point) {
            return hunk.startByte;
        }
    }
    return hunks.front().startByte; // wrap
}

std::optional<std::size_t> PreviousConflictHunkStart(const text::Buffer& buffer, std::size_t point) {
    const std::vector<text::ConflictHunk> hunks = text::ParseConflictHunks(buffer.Text());
    if (hunks.empty()) {
        return std::nullopt;
    }
    for (auto it = hunks.rbegin(); it != hunks.rend(); ++it) {
        if (it->startByte < point) {
            return it->startByte;
        }
    }
    return hunks.back().startByte; // wrap
}

} // namespace ned::editor
