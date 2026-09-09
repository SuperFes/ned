#include "LineDiff.h"

#include <algorithm>

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
