#include "ThreeWayMerge.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace ned::text {

namespace {

    // Splits on '\n', each returned piece keeping its own trailing '\n' (the
    // last piece has none if text doesn't end with one) -- reassembly of a
    // sequence of pieces is then plain concatenation, no separator logic
    // needed anywhere downstream.
    std::vector<std::string_view> SplitLines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t                   start = 0;
        while (start < text.size()) {
            const std::size_t newline = text.find('\n', start);
            if (newline == std::string_view::npos) {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, newline - start + 1));
            start = newline + 1;
        }
        return lines;
    }

    // A base range [aStart, aStart+aCount) replaced by b[bStart, bStart+bCount).
    struct Hunk {
        std::size_t aStart;
        std::size_t aCount;
        std::size_t bStart;
        std::size_t bCount;
    };

    // Trims a common prefix/suffix of matching lines, then an LCS
    // dynamic-programming backtrack over the remaining core to extract a
    // minimal hunk list -- a hunk is a maximal run of non-matching lines,
    // aCount/bCount 0 for a pure insert/delete.
    std::vector<Hunk> DiffHunks(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
        const std::size_t aSize = a.size();
        const std::size_t bSize = b.size();

        std::size_t prefix = 0;
        while (prefix < aSize && prefix < bSize && a[prefix] == b[prefix]) {
            ++prefix;
        }
        std::size_t suffix = 0;
        while (suffix < aSize - prefix && suffix < bSize - prefix && a[aSize - 1 - suffix] == b[bSize - 1 - suffix]) {
            ++suffix;
        }

        const std::size_t m = aSize - prefix - suffix; // core length in a
        const std::size_t n = bSize - prefix - suffix; // core length in b

        std::vector<Hunk> hunks;
        if (m == 0 && n == 0) {
            return hunks;
        }
        if (m == 0 || n == 0) {
            hunks.push_back(Hunk{prefix, m, prefix, n});
            return hunks;
        }

        // dp[i][j] = LCS length of a[prefix+i .. prefix+m) and b[prefix+j .. prefix+n).
        std::vector<std::vector<std::size_t>> dp(m + 1, std::vector<std::size_t>(n + 1, 0));
        for (std::size_t i = m; i-- > 0;) {
            for (std::size_t j = n; j-- > 0;) {
                if (a[prefix + i] == b[prefix + j]) {
                    dp[i][j] = dp[i + 1][j + 1] + 1;
                }
                else {
                    dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
                }
            }
        }

        std::size_t i = 0, j = 0;
        while (i < m || j < n) {
            if (i < m && j < n && a[prefix + i] == b[prefix + j]) {
                ++i;
                ++j;
                continue;
            }
            const std::size_t hunkAStart = prefix + i;
            const std::size_t hunkBStart = prefix + j;
            // Consume the mismatch run: guided by the LCS table while both
            // sides still have content, else forced to drain whichever side
            // is left (one side exhausted before the other).
            while ((i < m || j < n) && !(i < m && j < n && a[prefix + i] == b[prefix + j])) {
                if (j >= n || (i < m && dp[i + 1][j] >= dp[i][j + 1])) {
                    ++i;
                }
                else {
                    ++j;
                }
            }
            hunks.push_back(Hunk{hunkAStart, i + prefix - hunkAStart, hunkBStart, j + prefix - hunkBStart});
        }
        return hunks;
    }

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

    const std::vector<Hunk> hunksOurs   = DiffHunks(baseLines, oursLines);
    const std::vector<Hunk> hunksTheirs = DiffHunks(baseLines, theirsLines);

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
