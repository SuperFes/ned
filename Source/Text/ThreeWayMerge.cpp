#include "ThreeWayMerge.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "LineDiff.h"

namespace ned::text {

namespace {

    // diff-preview-line-diff-utility follow-up: SplitLines/the LCS hunk diff
    // (LineDiffHunk/DiffLines) used to be private to this file -- both now
    // live in LineDiff.h as a reusable public utility (AcpPanel's tool-call/
    // permission-prompt diff rendering is the other consumer), so this file
    // just aliases the shared name rather than keeping its own copy.
    using Hunk = LineDiffHunk;

    enum class Side { Ours,
                      Theirs };

    struct TaggedHunk {
        Hunk hunk;
        Side side;
    };

    // Reconstructs one side's effective content over [start, end) of base by
    // walking it and substituting that side's own hunk replacement wherever
    // one of its hunks covers a sub-range, else the unchanged base line.
    std::vector<std::string_view> EffectiveContent(const std::vector<std::string_view>& base,
                                                   const std::vector<std::string_view>& side, std::size_t start,
                                                   std::size_t end, const std::vector<Hunk>& hunksInGroup) {
        std::vector<std::string_view> out;
        std::size_t                   pos = start;
        for (const Hunk& hunk : hunksInGroup) {
            while (pos < hunk.aStart) {
                out.push_back(base[pos]);
                ++pos;
            }
            for (std::size_t k = 0; k < hunk.bCount; ++k) {
                out.push_back(side[hunk.bStart + k]);
            }
            pos = hunk.aStart + hunk.aCount;
        }
        while (pos < end) {
            out.push_back(base[pos]);
            ++pos;
        }
        return out;
    }

} // namespace

MergeResult ThreeWayMerge(std::string_view base, std::string_view ours, std::string_view theirs) {
    const std::vector<std::string_view> baseLines   = SplitLines(base);
    const std::vector<std::string_view> oursLines   = SplitLines(ours);
    const std::vector<std::string_view> theirsLines = SplitLines(theirs);

    const std::vector<Hunk> hunksOurs   = DiffLines(baseLines, oursLines);
    const std::vector<Hunk> hunksTheirs = DiffLines(baseLines, theirsLines);

    std::vector<TaggedHunk> all;
    all.reserve(hunksOurs.size() + hunksTheirs.size());
    for (const Hunk& h : hunksOurs) {
        all.push_back(TaggedHunk{h, Side::Ours});
    }
    for (const Hunk& h : hunksTheirs) {
        all.push_back(TaggedHunk{h, Side::Theirs});
    }
    std::sort(all.begin(), all.end(), [](const TaggedHunk& x, const TaggedHunk& y) { return x.hunk.aStart < y.hunk.aStart; });

    // Group hunks (from either side) whose base [start,end) intervals
    // numerically overlap (touching at a boundary does NOT count -- two
    // adjacent-but-independent edits stay independent, not a false
    // conflict), transitively, into maximal runs.
    struct Group {
        std::size_t       start;
        std::size_t       end;
        std::vector<Hunk> ours;
        std::vector<Hunk> theirs;
    };
    std::vector<Group> groups;
    for (const TaggedHunk& t : all) {
        const std::size_t hStart = t.hunk.aStart;
        const std::size_t hEnd   = t.hunk.aStart + t.hunk.aCount;
        // Strict overlap (hStart < groupEnd) is the general rule -- merely
        // touching at a boundary stays independent, not a false conflict.
        // The one exception: two pure insertions (zero-width hunks) at the
        // exact same base position aren't "touching," they're colliding at
        // the same point -- both groupStart==groupEnd and hStart==hEnd
        // catches that without loosening the general rule for anything else.
        const bool collidingInsertion =
            !groups.empty() && hStart == hEnd && groups.back().start == groups.back().end && hStart == groups.back().end;
        if (!groups.empty() && (hStart < groups.back().end || collidingInsertion)) {
            groups.back().end = std::max(groups.back().end, hEnd);
        }
        else {
            groups.push_back(Group{hStart, hEnd, {}, {}});
        }
        (t.side == Side::Ours ? groups.back().ours : groups.back().theirs).push_back(t.hunk);
    }

    std::string                mergedText;
    std::size_t                conflictCount = 0;
    std::optional<std::size_t> firstConflictOffset;
    std::size_t                cursor = 0;

    auto appendLines = [&mergedText](const std::vector<std::string_view>& lines) {
        for (const std::string_view line : lines) {
            mergedText.append(line);
        }
    };

    for (const Group& group : groups) {
        for (std::size_t pos = cursor; pos < group.start; ++pos) {
            mergedText.append(baseLines[pos]);
        }

        if (group.theirs.empty()) {
            // ours-only: exactly one hunk by construction (same-side hunks
            // never overlap each other).
            appendLines(EffectiveContent(baseLines, oursLines, group.start, group.end, group.ours));
        }
        else if (group.ours.empty()) {
            appendLines(EffectiveContent(baseLines, theirsLines, group.start, group.end, group.theirs));
        }
        else {
            const std::vector<std::string_view> oursEffective =
                EffectiveContent(baseLines, oursLines, group.start, group.end, group.ours);
            const std::vector<std::string_view> theirsEffective =
                EffectiveContent(baseLines, theirsLines, group.start, group.end, group.theirs);
            if (oursEffective == theirsEffective) {
                appendLines(oursEffective);
            }
            else {
                ++conflictCount;
                if (!firstConflictOffset) {
                    firstConflictOffset = mergedText.size();
                }
                mergedText += "<<<<<<< buffer\n";
                appendLines(oursEffective);
                mergedText += "=======\n";
                appendLines(theirsEffective);
                mergedText += ">>>>>>> disk\n";
            }
        }

        cursor = group.end;
    }
    for (std::size_t pos = cursor; pos < baseLines.size(); ++pos) {
        mergedText.append(baseLines[pos]);
    }

    return MergeResult{std::move(mergedText), conflictCount, firstConflictOffset};
}

bool HasConflictMarkers(std::string_view text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        if ((pos == 0 || text[pos - 1] == '\n') && text.compare(pos, 8, "<<<<<<< ") == 0) {
            return true;
        }
        ++pos;
    }
    return false;
}

} // namespace ned::text
