#include "LineDiff.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ned::text {

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

namespace {

    // Myers' greedy shortest-edit search over the cores a[prefix, prefix+m)
    // and b[prefix, prefix+n), keeping each step's diagonal frontier for the
    // backtrack -- O(D^2) memory for an edit distance of D. An edit distance
    // past kMaxEditDistance (two unrelated texts) reports the whole core as
    // one hunk instead.
    std::vector<LineDiffHunk> MyersHunks(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b, std::size_t prefix,
                                         std::size_t m, std::size_t n) {
        constexpr std::ptrdiff_t kMaxEditDistance = 2000;
        const std::ptrdiff_t     aLength          = static_cast<std::ptrdiff_t>(m);
        const std::ptrdiff_t     bLength          = static_cast<std::ptrdiff_t>(n);
        const std::ptrdiff_t     limit            = std::min(aLength + bLength, kMaxEditDistance);
        const std::ptrdiff_t     offset           = aLength + bLength + 1;
        auto                     same             = [&](std::ptrdiff_t x, std::ptrdiff_t y) {
            return a[prefix + static_cast<std::size_t>(x)] == b[prefix + static_cast<std::size_t>(y)];
        };

        std::vector<std::ptrdiff_t>              frontier(static_cast<std::size_t>(2 * offset + 1), 0);
        std::vector<std::vector<std::ptrdiff_t>> trace; // trace[d][k + d] = furthest x on diagonal k after d edits
        std::ptrdiff_t                           distance = -1;
        for (std::ptrdiff_t d = 0; d <= limit && distance < 0; ++d) {
            std::vector<std::ptrdiff_t> step(static_cast<std::size_t>(2 * d + 1), 0);
            for (std::ptrdiff_t k = -d; k <= d; k += 2) {
                const bool     down = k == -d || (k != d && frontier[offset + k - 1] < frontier[offset + k + 1]);
                std::ptrdiff_t x    = down ? frontier[offset + k + 1] : frontier[offset + k - 1] + 1;
                std::ptrdiff_t y    = x - k;
                while (x < aLength && y < bLength && same(x, y)) {
                    ++x;
                    ++y;
                }
                frontier[offset + k] = x;
                step[k + d]          = x;
                if (x >= aLength && y >= bLength) {
                    distance = d;
                }
            }
            trace.push_back(std::move(step));
        }
        if (distance < 0) {
            return {LineDiffHunk{prefix, m, prefix, n}};
        }

        // Walk back from the end, collecting the matched pairs of each snake.
        std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> matches;
        std::ptrdiff_t                                         x = aLength;
        std::ptrdiff_t                                         y = bLength;
        for (std::ptrdiff_t d = distance; d > 0; --d) {
            const std::vector<std::ptrdiff_t>& previous = trace[static_cast<std::size_t>(d - 1)];
            const std::ptrdiff_t               k        = x - y;
            auto                               at       = [&](std::ptrdiff_t diagonal) { return previous[diagonal + d - 1]; };
            const bool                         down     = k == -d || (k != d && at(k - 1) < at(k + 1));
            const std::ptrdiff_t               fromK    = down ? k + 1 : k - 1;
            const std::ptrdiff_t               fromX    = at(fromK);
            const std::ptrdiff_t               fromY    = fromX - fromK;
            const std::ptrdiff_t               snakeX   = down ? fromX : fromX + 1;
            const std::ptrdiff_t               snakeY   = down ? fromY + 1 : fromY;
            for (std::ptrdiff_t i = x - 1, j = y - 1; i >= snakeX; --i, --j) {
                matches.emplace_back(i, j);
            }
            x = fromX;
            y = fromY;
        }
        for (std::ptrdiff_t i = x - 1, j = y - 1; i >= 0; --i, --j) {
            matches.emplace_back(i, j);
        }
        std::reverse(matches.begin(), matches.end());

        std::vector<LineDiffHunk> hunks;
        std::ptrdiff_t            i     = 0;
        std::ptrdiff_t            j     = 0;
        auto                      flush = [&](std::ptrdiff_t toI, std::ptrdiff_t toJ) {
            if (i < toI || j < toJ) {
                hunks.push_back(LineDiffHunk{prefix + static_cast<std::size_t>(i), static_cast<std::size_t>(toI - i),
                                             prefix + static_cast<std::size_t>(j), static_cast<std::size_t>(toJ - j)});
            }
        };
        for (const auto& [matchI, matchJ] : matches) {
            flush(matchI, matchJ);
            i = matchI + 1;
            j = matchJ + 1;
        }
        flush(aLength, bLength);
        return hunks;
    }

} // namespace

std::vector<LineDiffHunk> DiffLines(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
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

    std::vector<LineDiffHunk> hunks;
    if (m == 0 && n == 0) {
        return hunks;
    }
    if (m == 0 || n == 0) {
        hunks.push_back(LineDiffHunk{prefix, m, prefix, n});
        return hunks;
    }

    // The LCS table is m*n; past a few million cells (a long file edited
    // near both ends) Myers' O((m+n)*D) search takes over.
    constexpr std::size_t kMaxLcsCells = 4'000'000;
    if (m * n > kMaxLcsCells) {
        return MyersHunks(a, b, prefix, m, n);
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
        // Consume the mismatch run: guided by the LCS table while both sides
        // still have content, else forced to drain whichever side is left
        // (one side exhausted before the other).
        while ((i < m || j < n) && !(i < m && j < n && a[prefix + i] == b[prefix + j])) {
            if (j >= n || (i < m && dp[i + 1][j] >= dp[i][j + 1])) {
                ++i;
            }
            else {
                ++j;
            }
        }
        hunks.push_back(LineDiffHunk{hunkAStart, i + prefix - hunkAStart, hunkBStart, j + prefix - hunkBStart});
    }
    return hunks;
}

namespace {

    std::string StripNewline(std::string_view line) {
        if (!line.empty() && line.back() == '\n') {
            line.remove_suffix(1);
        }
        return std::string(line);
    }

    std::string OmittedText(std::size_t count) {
        return "... " + std::to_string(count) + (count == 1 ? " unchanged line ..." : " unchanged lines ...");
    }

} // namespace

std::vector<DiffLine> UnifiedDiff(std::string_view oldText, std::string_view newText, std::size_t contextLines) {
    const std::vector<std::string_view> a     = SplitLines(oldText);
    const std::vector<std::string_view> b     = SplitLines(newText);
    const std::vector<LineDiffHunk>     hunks = DiffLines(a, b);

    std::vector<DiffLine> out;
    if (hunks.empty()) {
        return out;
    }

    auto appendContext = [&](std::size_t start, std::size_t end) {
        for (std::size_t idx = start; idx < end; ++idx) {
            out.push_back(DiffLine{DiffLineKind::Context, StripNewline(a[idx])});
        }
    };

    std::size_t shown      = 0; // a-index: everything before this has already been shown or omitted
    std::size_t groupStart = 0;
    while (groupStart < hunks.size()) {
        std::size_t groupEnd = groupStart;
        while (groupEnd + 1 < hunks.size() &&
               hunks[groupEnd + 1].aStart <= hunks[groupEnd].aStart + hunks[groupEnd].aCount + 2 * contextLines) {
            ++groupEnd;
        }

        const std::size_t leadingFrom  = hunks[groupStart].aStart >= contextLines ? hunks[groupStart].aStart - contextLines : 0;
        const std::size_t leadingStart = std::max(shown, leadingFrom);
        if (leadingStart > shown) {
            out.push_back(DiffLine{DiffLineKind::Omitted, OmittedText(leadingStart - shown)});
        }
        appendContext(leadingStart, hunks[groupStart].aStart);

        for (std::size_t h = groupStart; h <= groupEnd; ++h) {
            const LineDiffHunk& hunk = hunks[h];
            for (std::size_t k = 0; k < hunk.aCount; ++k) {
                out.push_back(DiffLine{DiffLineKind::Removed, StripNewline(a[hunk.aStart + k])});
            }
            for (std::size_t k = 0; k < hunk.bCount; ++k) {
                out.push_back(DiffLine{DiffLineKind::Added, StripNewline(b[hunk.bStart + k])});
            }
            if (h < groupEnd) {
                // Group-merge condition above guarantees this connecting gap
                // is short enough to show in full, never elided.
                appendContext(hunk.aStart + hunk.aCount, hunks[h + 1].aStart);
            }
        }

        const LineDiffHunk& last       = hunks[groupEnd];
        const std::size_t   trailingTo = std::min(a.size(), last.aStart + last.aCount + contextLines);
        appendContext(last.aStart + last.aCount, trailingTo);
        shown = trailingTo;

        groupStart = groupEnd + 1;
    }
    if (shown < a.size()) {
        out.push_back(DiffLine{DiffLineKind::Omitted, OmittedText(a.size() - shown)});
    }

    return out;
}

} // namespace ned::text
