#include "MergeView.h"

#include <algorithm>

#include "Editor/ModeOverrides.h"
#include "Text/ConflictHunk.h"
#include "Text/LineDiff.h"

namespace ned::editor {

namespace {

    const char* KindLabel(text::MergeSideKind kind) {
        switch (kind) {
            case text::MergeSideKind::Ours:
                return "ours";
            case text::MergeSideKind::Base:
                return "base";
            case text::MergeSideKind::Theirs:
                return "theirs";
        }
        return "side";
    }

    // Byte range of lines [line, line + count) of `text`.
    std::pair<std::size_t, std::size_t> LineBytes(std::string_view text, std::size_t line, std::size_t count) {
        auto offsetOfLine = [&](std::size_t target) {
            std::size_t offset = 0;
            for (std::size_t i = 0; i < target; ++i) {
                const std::size_t newline = text.find('\n', offset);
                if (newline == std::string_view::npos) {
                    return text.size();
                }
                offset = newline + 1;
            }
            return offset;
        };
        const std::size_t start = offsetOfLine(line);
        return {start, count == 0 ? start : offsetOfLine(line + count)};
    }

    // A side that is the merged text itself: what a side falls back to once
    // its markers can no longer supply it.
    text::MergeSide Unchanged(const std::string& merged, std::size_t lineCount) {
        text::MergeSide side{.text = merged, .lineCount = lineCount, .chunks = {}};
        if (lineCount > 0) {
            side.chunks.push_back({.mergedLine = 0, .mergedCount = lineCount, .sideLine = 0, .sideCount = lineCount, .changed = false});
        }
        return side;
    }

    // The changed chunk of `side` that `line` (a side line) points at. A
    // chunk with no side lines is reached from the line above the gap its
    // blank rows fill, or the line below it.
    const text::AlignedChunk* ChunkAtSideLine(const text::MergeSide& side, std::size_t line) {
        const text::AlignedChunk* adjacent = nullptr;
        for (const text::AlignedChunk& chunk : side.chunks) {
            if (!chunk.changed) {
                continue;
            }
            if (line >= chunk.sideLine && line < chunk.sideLine + chunk.sideCount) {
                return &chunk;
            }
            if (chunk.sideCount == 0 && !adjacent && (chunk.sideLine == line + 1 || chunk.sideLine == line)) {
                adjacent = &chunk;
            }
        }
        return adjacent;
    }

} // namespace

std::string MergeSideBufferName(text::MergeSideKind kind, const std::string& mergedName) {
    return std::string("*merge ") + KindLabel(kind) + ": " + mergedName + "*";
}

void RewriteReadOnlyBuffer(text::Buffer& buffer, std::string_view text) {
    const std::string current = buffer.Text();
    if (current == text) {
        buffer.SetReadOnly(true);
        return;
    }

    // Common prefix and suffix, both snapped to whole lines so the rewrite
    // never splits a multi-byte character.
    const std::size_t shorter = std::min(current.size(), text.size());
    std::size_t       prefix  = 0;
    while (prefix < shorter && current[prefix] == text[prefix]) {
        ++prefix;
    }
    if (prefix > 0) {
        const std::size_t newline = current.rfind('\n', prefix - 1);
        prefix                    = newline == std::string::npos ? 0 : newline + 1;
    }
    std::size_t suffix = 0;
    while (suffix < shorter - prefix && current[current.size() - 1 - suffix] == text[text.size() - 1 - suffix]) {
        ++suffix;
    }
    if (const std::size_t start = current.size() - suffix; suffix > 0 && start > 0 && current[start - 1] != '\n') {
        const std::size_t newline = current.find('\n', start);
        suffix                    = newline == std::string::npos ? 0 : current.size() - (newline + 1);
    }

    buffer.SetReadOnly(false);
    buffer.BeginUndoGroup();
    buffer.DeleteRange(prefix, current.size() - suffix - prefix);
    buffer.InsertAt(prefix, text.substr(prefix, text.size() - suffix - prefix));
    buffer.EndUndoGroup();
    buffer.SetReadOnly(true);
}

MergeViewSession::MergeViewSession(text::BufferList& bufferList, text::Buffer& merged,
                                   const std::vector<text::MergeSideKind>& kinds) : bufferList_(bufferList), mergedName_(merged.Name()) {
    const std::string                     text     = merged.Text();
    const std::vector<text::ConflictHunk> hunks    = text::ParseConflictHunks(text);
    const std::string                     modeName = CachedModeForBuffer(merged).name;
    for (const text::MergeSideKind kind : kinds) {
        if (!text::MergeSideFromMarkers(text, hunks, kind)) {
            continue;
        }
        text::Buffer& buffer = bufferList_.CreateBuffer(MergeSideBufferName(kind, mergedName_));
        buffer.SetReadOnly(true);
        // Path-less, so it would otherwise highlight as Fundamental.
        SetChosenModeForBuffer(buffer, modeName);
        sides_.push_back(Side{.kind = kind, .name = buffer.Name(), .model = {}});
    }
    Refresh();
}

MergeViewSession::~MergeViewSession() {
    for (const Side& side : sides_) {
        if (const text::Buffer* buffer = BufferOf(side)) {
            // Keyed by Buffer*, so it must not outlive the buffer.
            ClearModeCacheFor(*buffer);
            bufferList_.Close(side.name);
        }
    }
}

text::Buffer* MergeViewSession::Merged() const {
    return bufferList_.Find(mergedName_);
}

text::Buffer* MergeViewSession::BufferOf(const Side& side) const {
    return bufferList_.Find(side.name);
}

std::vector<text::Buffer*> MergeViewSession::Sides() const {
    std::vector<text::Buffer*> buffers;
    for (const Side& side : sides_) {
        if (text::Buffer* buffer = BufferOf(side)) {
            buffers.push_back(buffer);
        }
    }
    return buffers;
}

const MergeViewSession::Side* MergeViewSession::SideFor(const text::Buffer& buffer) const {
    const auto it = std::ranges::find(sides_, buffer.Name(), &Side::name);
    return it == sides_.end() ? nullptr : &*it;
}

std::optional<text::MergeSideKind> MergeViewSession::KindOf(const text::Buffer& buffer) const {
    const auto it = std::ranges::find(sides_, buffer.Name(), &Side::name);
    return it == sides_.end() ? std::nullopt : std::optional(it->kind);
}

bool MergeViewSession::Contains(const text::Buffer& buffer) const {
    return buffer.Name() == mergedName_ || KindOf(buffer).has_value();
}

std::size_t MergeViewSession::ConflictCount() {
    Refresh();
    return conflictCount_;
}

void MergeViewSession::Refresh() {
    text::Buffer* merged = Merged();
    if (!merged) {
        return;
    }
    const bool sideTouched = std::ranges::any_of(sides_, [&](const Side& side) {
        const text::Buffer* buffer = BufferOf(side);
        return buffer && buffer->ContentGeneration() != side.writtenGeneration;
    });
    if (mergedGeneration_ == merged->ContentGeneration() && !sideTouched) {
        return;
    }

    const std::string                     text        = merged->Text();
    const std::vector<text::ConflictHunk> hunks       = text::ParseConflictHunks(text);
    const std::size_t                     mergedLines = text::SplitLines(text).size();
    conflictCount_                                    = hunks.size();

    std::vector<const text::MergeSide*> models;
    for (Side& side : sides_) {
        side.model = text::MergeSideFromMarkers(text, hunks, side.kind).value_or(Unchanged(text, mergedLines));
        if (text::Buffer* buffer = BufferOf(side)) {
            RewriteReadOnlyBuffer(*buffer, side.model.text);
            side.writtenGeneration = buffer->ContentGeneration();
        }
        models.push_back(&side.model);
    }
    alignments_       = text::AlignPanes(mergedLines, models);
    mergedGeneration_ = merged->ContentGeneration();
}

const text::PaneAlignment* MergeViewSession::AlignmentFor(const text::Buffer& buffer) const {
    if (alignments_.empty()) {
        return nullptr;
    }
    if (buffer.Name() == mergedName_) {
        return &alignments_[0];
    }
    const auto it = std::ranges::find(sides_, buffer.Name(), &Side::name);
    return it == sides_.end() ? nullptr : &alignments_[1 + static_cast<std::size_t>(it - sides_.begin())];
}

std::optional<std::size_t> MergeViewSession::CorrespondingLine(const text::Buffer& from, std::size_t line,
                                                               const text::Buffer& to) const {
    const text::PaneAlignment* fromAlignment = AlignmentFor(from);
    const text::PaneAlignment* toAlignment   = AlignmentFor(to);
    if (!fromAlignment || !toAlignment) {
        return std::nullopt;
    }
    return toAlignment->LineAtAlignedRow(fromAlignment->AlignedRow(line));
}

std::vector<std::pair<std::size_t, std::size_t>> MergeViewSession::ChangedLines(const text::Buffer& buffer) const {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (buffer.Name() == mergedName_) {
        for (const Side& side : sides_) {
            for (const text::AlignedChunk& chunk : side.model.chunks) {
                if (chunk.changed && chunk.mergedCount > 0) {
                    ranges.emplace_back(chunk.mergedLine, chunk.mergedLine + chunk.mergedCount);
                }
            }
        }
        std::ranges::sort(ranges);
        const auto [first, last] = std::ranges::unique(ranges);
        ranges.erase(first, last);
        return ranges;
    }
    if (const Side* side = SideFor(buffer)) {
        for (const text::AlignedChunk& chunk : side->model.chunks) {
            if (chunk.changed && chunk.sideCount > 0) {
                ranges.emplace_back(chunk.sideLine, chunk.sideLine + chunk.sideCount);
            }
        }
    }
    return ranges;
}

std::optional<text::MergeSideKind> MergeViewSession::ChangedSideAt(const text::Buffer& buffer, std::size_t line) const {
    const Side* side = SideFor(buffer);
    if (!side) {
        return std::nullopt;
    }
    const bool changed = std::ranges::any_of(side->model.chunks, [line](const text::AlignedChunk& chunk) {
        return chunk.changed && line >= chunk.sideLine && line < chunk.sideLine + chunk.sideCount;
    });
    return changed ? std::optional(side->kind) : std::nullopt;
}

bool MergeViewSession::TakeSide(const text::Buffer& sideBuffer, std::size_t line) {
    Refresh();
    const Side*   side   = SideFor(sideBuffer);
    text::Buffer* merged = Merged();
    if (!side || !merged || merged->ReadOnly()) {
        return false;
    }
    const text::AlignedChunk* chunk = ChunkAtSideLine(side->model, line);
    if (!chunk) {
        return false;
    }

    const std::string mergedText        = merged->Text();
    const auto [mergedStart, mergedEnd] = LineBytes(mergedText, chunk->mergedLine, chunk->mergedCount);
    const auto [sideStart, sideEnd]     = LineBytes(side->model.text, chunk->sideLine, chunk->sideCount);
    const std::string replacement       = side->model.text.substr(sideStart, sideEnd - sideStart);

    merged->BeginUndoGroup();
    merged->DeleteRange(mergedStart, mergedEnd - mergedStart);
    merged->InsertAt(mergedStart, replacement);
    merged->EndUndoGroup();
    merged->SetPoint(mergedStart);
    Refresh();
    return true;
}

std::vector<std::size_t> MergeViewSession::MergedChangedStarts() const {
    std::vector<std::size_t> starts;
    for (const Side& side : sides_) {
        for (const text::AlignedChunk& chunk : side.model.chunks) {
            if (chunk.changed) {
                starts.push_back(chunk.mergedLine);
            }
        }
    }
    std::ranges::sort(starts);
    const auto [first, last] = std::ranges::unique(starts);
    starts.erase(first, last);
    return starts;
}

std::optional<std::size_t> MergeViewSession::NextChangedLine(std::size_t line) {
    Refresh();
    const std::vector<std::size_t> starts = MergedChangedStarts();
    if (starts.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::upper_bound(starts, line);
    return it == starts.end() ? starts.front() : *it;
}

std::optional<std::size_t> MergeViewSession::PreviousChangedLine(std::size_t line) {
    Refresh();
    const std::vector<std::size_t> starts = MergedChangedStarts();
    if (starts.empty()) {
        return std::nullopt;
    }
    const auto it = std::ranges::lower_bound(starts, line);
    return it == starts.begin() ? starts.back() : *std::prev(it);
}

} // namespace ned::editor
