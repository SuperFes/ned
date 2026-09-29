#include "TurnReview.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/LineDiff.h"

namespace ned::editor::acp {

namespace {

    std::vector<std::string> Lines(std::string_view text) {
        std::vector<std::string> lines;
        for (const std::string_view line : text::SplitLines(text)) {
            lines.emplace_back(line);
        }
        return lines;
    }

    std::string Join(const std::vector<std::string>& lines) {
        std::string text;
        for (const std::string& line : lines) {
            text += line;
        }
        return text;
    }

    template <typename Range>
    std::vector<std::string> Slice(const Range& lines, std::size_t start, std::size_t end) {
        return {lines.begin() + static_cast<std::ptrdiff_t>(start), lines.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    // Where `middle` sits in `lines` between `before` and `after`, nearest
    // `expected`; the index of middle's first line.
    std::optional<std::size_t> Find(const std::vector<std::string_view>& lines, const std::vector<std::string>& before,
                                    const std::vector<std::string>& middle, const std::vector<std::string>& after, std::size_t expected) {
        std::vector<std::string_view> pattern;
        for (const auto* part : {&before, &middle, &after}) {
            pattern.insert(pattern.end(), part->begin(), part->end());
        }
        if (pattern.empty()) {
            return lines.empty() ? std::optional<std::size_t>(0) : std::nullopt;
        }
        if (pattern.size() > lines.size()) {
            return std::nullopt;
        }
        std::optional<std::size_t> best;
        std::size_t                bestDistance = 0;
        for (std::size_t start = 0; start + pattern.size() <= lines.size(); ++start) {
            if (!std::equal(pattern.begin(), pattern.end(), lines.begin() + static_cast<std::ptrdiff_t>(start))) {
                continue;
            }
            const std::size_t at       = start + before.size();
            const std::size_t distance = at > expected ? at - expected : expected - at;
            if (!best || distance < bestDistance) {
                best         = at;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::string DisplayPath(const std::filesystem::path& path, const std::filesystem::path& root) {
        if (!root.empty()) {
            const std::filesystem::path relative = path.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..") {
                return relative.generic_string();
            }
        }
        return path.string();
    }

    std::string WithoutNewline(std::string_view line) {
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        return std::string(line);
    }

    std::unordered_map<std::size_t, ReviewSession>& Registry() {
        static std::unordered_map<std::size_t, ReviewSession> registry;
        return registry;
    }

} // namespace

std::vector<ReviewHunk> TurnHunks(const TurnFile& file, std::size_t fileIndex) {
    std::vector<ReviewHunk> hunks;
    if (!file.after || (!file.before.exists && !file.after->exists)) {
        return hunks;
    }
    if (!file.before.exists) {
        hunks.push_back({.fileIndex = fileIndex, .newLines = Lines(file.after->text), .created = true});
        return hunks;
    }
    if (!file.after->exists) {
        hunks.push_back({.fileIndex = fileIndex, .oldLines = Lines(file.before.text), .deleted = true});
        return hunks;
    }

    const std::vector<std::string_view>   a   = text::SplitLines(file.before.text);
    const std::vector<std::string_view>   b   = text::SplitLines(file.after->text);
    const std::vector<text::LineDiffHunk> raw = text::DiffLines(a, b);
    // Changes closer than two contexts apart share one hunk, as in a
    // unified diff -- so a hunk's context is always unchanged text.
    for (std::size_t first = 0; first < raw.size();) {
        std::size_t last = first;
        while (last + 1 < raw.size() && raw[last + 1].aStart - (raw[last].aStart + raw[last].aCount) <= 2 * kReviewContextLines) {
            ++last;
        }
        const std::size_t aStart = raw[first].aStart;
        const std::size_t aEnd   = raw[last].aStart + raw[last].aCount;
        const std::size_t bStart = raw[first].bStart;
        const std::size_t bEnd   = raw[last].bStart + raw[last].bCount;
        hunks.push_back({.fileIndex     = fileIndex,
                         .oldStart      = aStart,
                         .newStart      = bStart,
                         .contextBefore = Slice(b, bStart - std::min(bStart, kReviewContextLines), bStart),
                         .oldLines      = Slice(a, aStart, aEnd),
                         .newLines      = Slice(b, bStart, bEnd),
                         .contextAfter  = Slice(b, bEnd, std::min(b.size(), bEnd + kReviewContextLines))});
        first = last + 1;
    }
    return hunks;
}

std::optional<std::size_t> LocateHunk(const FileState& current, const ReviewHunk& hunk) {
    if (hunk.created) {
        return current.exists && current.text == Join(hunk.newLines) ? std::optional<std::size_t>(0) : std::nullopt;
    }
    if (hunk.deleted) {
        return current.exists ? std::nullopt : std::optional<std::size_t>(0);
    }
    if (!current.exists) {
        return std::nullopt;
    }
    return Find(text::SplitLines(current.text), hunk.contextBefore, hunk.newLines, hunk.contextAfter, hunk.newStart);
}

HunkState StateOf(const FileState& current, const ReviewHunk& hunk) {
    if (LocateHunk(current, hunk)) {
        return HunkState::Applied;
    }
    if (hunk.created) {
        return current.exists ? HunkState::Changed : HunkState::Undone;
    }
    if (hunk.deleted) {
        return current.exists && current.text == Join(hunk.oldLines) ? HunkState::Undone : HunkState::Changed;
    }
    if (current.exists && Find(text::SplitLines(current.text), hunk.contextBefore, hunk.oldLines, hunk.contextAfter, hunk.newStart)) {
        return HunkState::Undone;
    }
    return HunkState::Changed;
}

std::optional<FileState> RevertHunk(const FileState& current, const ReviewHunk& hunk) {
    const std::optional<std::size_t> at = LocateHunk(current, hunk);
    if (!at) {
        return std::nullopt;
    }
    if (hunk.created) {
        return FileState{};
    }
    if (hunk.deleted) {
        return FileState{.exists = true, .text = Join(hunk.oldLines)};
    }
    std::vector<std::string> lines = Lines(current.text);
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*at), lines.begin() + static_cast<std::ptrdiff_t>(*at + hunk.newLines.size()));
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*at), hunk.oldLines.begin(), hunk.oldLines.end());
    return FileState{.exists = true, .text = Join(lines)};
}

ReviewSession MakeReviewSession(std::string title, std::filesystem::path root, std::vector<TurnFile> files) {
    ReviewSession session{.title = std::move(title), .root = std::move(root), .files = std::move(files)};
    for (std::size_t i = 0; i < session.files.size(); ++i) {
        std::vector<ReviewHunk> hunks = TurnHunks(session.files[i], i);
        session.hunks.insert(session.hunks.end(), std::make_move_iterator(hunks.begin()), std::make_move_iterator(hunks.end()));
    }
    session.kept.assign(session.hunks.size(), false);
    return session;
}

std::string UndoHunk(text::BufferList& bufferList, ReviewSession& session, std::size_t index) {
    if (index >= session.hunks.size()) {
        return "No change here.";
    }
    const ReviewHunk&              hunk    = session.hunks[index];
    const TurnFile&                file    = session.files[hunk.fileIndex];
    const std::string              name    = DisplayPath(file.path, session.root);
    const std::optional<FileState> current = ReadTurnFile(bufferList, file.path);
    if (!current) {
        return "Can't read " + name + ".";
    }
    const std::optional<FileState> reverted = RevertHunk(*current, hunk);
    if (!reverted) {
        return StateOf(*current, hunk) == HunkState::Undone ? "Already undone." : name + " has changed there since the turn.";
    }
    try {
        WriteTurnFile(bufferList, file.path, *reverted);
    }
    catch (const std::exception& e) {
        return e.what();
    }
    session.kept[index] = false;
    return hunk.created ? "Deleted " + name + "." : "Undid a change in " + name + ".";
}

std::string UndoFile(text::BufferList& bufferList, ReviewSession& session, std::size_t index) {
    if (index >= session.hunks.size()) {
        return "No change here.";
    }
    const std::size_t              fileIndex = session.hunks[index].fileIndex;
    const TurnFile&                file      = session.files[fileIndex];
    const std::string              name      = DisplayPath(file.path, session.root);
    const std::optional<FileState> current   = ReadTurnFile(bufferList, file.path);
    if (!current) {
        return "Can't read " + name + ".";
    }
    // Bottom-up, so reverting one hunk doesn't move the next one's lines.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < session.hunks.size(); ++i) {
        if (session.hunks[i].fileIndex == fileIndex) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return session.hunks[x].newStart > session.hunks[y].newStart; });

    FileState   state    = *current;
    std::size_t undone   = 0;
    std::size_t stranded = 0;
    for (const std::size_t i : order) {
        if (StateOf(state, session.hunks[i]) == HunkState::Undone) {
            continue;
        }
        if (const std::optional<FileState> reverted = RevertHunk(state, session.hunks[i])) {
            state = *reverted;
            ++undone;
        }
        else {
            ++stranded;
        }
    }
    if (stranded > 0) {
        return std::to_string(stranded) + " of " + name + "'s changes have changed since the turn; undo the others one at a time.";
    }
    if (undone == 0) {
        return "Nothing left to undo in " + name + ".";
    }
    try {
        WriteTurnFile(bufferList, file.path, state);
    }
    catch (const std::exception& e) {
        return e.what();
    }
    for (const std::size_t i : order) {
        session.kept[i] = false;
    }
    return "Undid " + std::to_string(undone) + (undone == 1 ? " change" : " changes") + " in " + name + ".";
}

std::vector<multibuffer::ExcerptSource> ReviewExcerpts(const text::BufferList& bufferList, const ReviewSession& session) {
    std::vector<std::optional<FileState>>   current(session.files.size());
    std::vector<bool>                       read(session.files.size(), false);
    std::vector<multibuffer::ExcerptSource> excerpts;
    for (std::size_t i = 0; i < session.hunks.size(); ++i) {
        const ReviewHunk& hunk = session.hunks[i];
        const TurnFile&   file = session.files[hunk.fileIndex];
        if (!read[hunk.fileIndex]) {
            current[hunk.fileIndex] = ReadTurnFile(bufferList, file.path);
            read[hunk.fileIndex]    = true;
        }
        const std::optional<FileState>&  now   = current[hunk.fileIndex];
        const HunkState                  state = now ? StateOf(*now, hunk) : HunkState::Changed;
        const std::optional<std::size_t> at    = now && state == HunkState::Applied ? LocateHunk(*now, hunk) : std::nullopt;
        const std::size_t                line  = (at ? *at : hunk.newStart) + 1;

        std::string header = "▸ " + DisplayPath(file.path, session.root);
        if (!hunk.created && !hunk.deleted) {
            header += ":" + std::to_string(line);
        }
        header += "  +" + std::to_string(hunk.newLines.size()) + " −" + std::to_string(hunk.oldLines.size());
        header += hunk.created ? "  new file" : hunk.deleted ? "  deleted"
                                                             : "";
        header += state == HunkState::Undone    ? "  ↶ undone"
                  : state == HunkState::Changed ? "  (changed since)"
                  : session.kept[i]             ? "  ✓ kept"
                                                : "";

        std::string                        body;
        std::vector<multibuffer::LineTint> tints;
        auto                               add = [&](const std::vector<std::string>& lines, std::string_view prefix, multibuffer::LineTint tint) {
            for (const std::string& line : lines) {
                body += std::string(prefix) + WithoutNewline(line) + "\n";
                tints.push_back(tint);
            }
        };
        add(hunk.contextBefore, "  ", multibuffer::LineTint::None);
        add(hunk.oldLines, "- ", multibuffer::LineTint::Removed);
        add(hunk.newLines, "+ ", multibuffer::LineTint::Added);
        add(hunk.contextAfter, "  ", multibuffer::LineTint::None);

        const std::size_t sourceLine = hunk.deleted ? 0 : line;
        excerpts.push_back(multibuffer::ExcerptSource{.sourcePath      = file.path,
                                                      .sourceStartLine = sourceLine,
                                                      .sourceEndLine   = sourceLine,
                                                      .headerText      = std::move(header),
                                                      .bodyText        = std::move(body),
                                                      .lineTints       = std::move(tints)});
    }
    return excerpts;
}

void AttachReview(const text::Buffer& buffer, ReviewSession session) {
    Registry()[buffer.InstanceId()] = std::move(session);
}

ReviewSession* ReviewFor(const text::Buffer& buffer) {
    const auto it = Registry().find(buffer.InstanceId());
    return it == Registry().end() ? nullptr : &it->second;
}

void DetachReview(const text::Buffer& buffer) {
    Registry().erase(buffer.InstanceId());
}

} // namespace ned::editor::acp
