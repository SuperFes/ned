#include "Editor/LinkedEditingSession.h"

namespace ned::editor {

std::optional<LinkedEditingSession> LinkedEditingSession::Start(text::Buffer& buffer, std::string bufferName,
                                                                const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
    if (ranges.size() < 2) {
        return std::nullopt;
    }
    const std::size_t                       point = buffer.Point();
    std::optional<std::size_t>              activeId;
    std::vector<text::Buffer::SnippetRange> snippetRanges;
    snippetRanges.reserve(ranges.size());
    std::size_t nextId = 1;
    for (const auto& [start, end] : ranges) {
        const std::size_t id = nextId++;
        if (!activeId && point >= start && point <= end) {
            activeId = id;
        }
        snippetRanges.push_back(text::Buffer::SnippetRange{id, 0, start, end, false});
    }
    if (!activeId) {
        return std::nullopt; // point isn't actually inside any reported range
    }
    buffer.SetSnippetRanges(std::move(snippetRanges));
    buffer.SetActiveSnippetRange(*activeId);

    LinkedEditingSession session;
    session.bufferName_    = std::move(bufferName);
    session.activeRangeId_ = *activeId;
    if (const text::Buffer::SnippetRange* active = session.FindRange(buffer, *activeId)) {
        session.lastSyncedText_ = buffer.Content().Substring(active->start, active->end - active->start);
    }
    return session;
}

void LinkedEditingSession::SyncMirrors(text::Buffer& buffer) {
    const text::Buffer::SnippetRange* active = FindRange(buffer, activeRangeId_);
    if (active == nullptr) {
        return;
    }
    const std::string current = buffer.Content().Substring(active->start, active->end - active->start);
    if (current == lastSyncedText_) {
        return;
    }
    std::vector<std::size_t> mirrorIds;
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.id != activeRangeId_) {
            mirrorIds.push_back(range.id);
        }
    }
    // Same point-preservation/deactivate-during-rewrite dance
    // SnippetSession::SyncMirrors uses -- see that method's own comments for
    // why: a rewrite of a mirror directly adjacent to the active range would
    // otherwise be absorbed by the active range's own grow-at-boundary
    // gravity, and point sitting inside the active range needs to survive
    // rewrites of every *other* range untouched.
    const std::size_t pointBefore      = buffer.Point();
    const bool        pointInActive    = pointBefore >= active->start && pointBefore <= active->end;
    const std::size_t pointFieldOffset = pointInActive ? pointBefore - active->start : 0;
    buffer.SetActiveSnippetRange(0); // 0 is never an assigned id -- clears every flag
    for (const std::size_t id : mirrorIds) {
        const text::Buffer::SnippetRange* mirror = FindRange(buffer, id);
        if (mirror == nullptr) {
            continue;
        }
        const std::size_t start = mirror->start;
        if (buffer.Content().Substring(start, mirror->end - start) == current) {
            continue;
        }
        if (mirror->end > start) {
            buffer.DeleteRange(start, mirror->end - start);
        }
        if (!current.empty()) {
            buffer.InsertAt(start, current);
        }
        buffer.UpdateSnippetRange(id, start, start + current.size());
    }
    buffer.SetActiveSnippetRange(activeRangeId_);
    if (pointInActive) {
        if (const text::Buffer::SnippetRange* after = FindRange(buffer, activeRangeId_)) {
            buffer.SetPoint(after->start + pointFieldOffset);
        }
    }
    lastSyncedText_ = current;
}

bool LinkedEditingSession::RangesValid(const text::Buffer& buffer) const {
    return !buffer.SnippetRanges().empty();
}

bool LinkedEditingSession::PointStillInside(const text::Buffer& buffer) const {
    const std::size_t point = buffer.Point();
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (point >= range.start && point <= range.end) {
            return true;
        }
    }
    return false;
}

void LinkedEditingSession::Finish(text::Buffer& buffer) {
    buffer.ClearSnippetRanges();
}

const std::string& LinkedEditingSession::BufferName() const {
    return bufferName_;
}

std::string LinkedEditingSession::StatusText() const {
    return "Linked editing (ESC to end)";
}

const text::Buffer::SnippetRange* LinkedEditingSession::FindRange(const text::Buffer& buffer, std::size_t id) const {
    for (const text::Buffer::SnippetRange& range : buffer.SnippetRanges()) {
        if (range.id == id) {
            return &range;
        }
    }
    return nullptr;
}

} // namespace ned::editor
