#include "LineDiff.h"

#include <algorithm>
#include <cstddef>
#include <optional>
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

    // Linear-space Myers (1986, section 4b): search forward from the start
    // and backward from the end of a range at once until the two frontiers
    // overlap, split the range there, and recurse on both halves. O(N + M)
    // memory and O((N + M) * D) time for an edit distance of D.
    class MyersDiff {
      public:
        MyersDiff(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b, std::ptrdiff_t halfLimit) : a_(a), b_(b), halfLimit_(halfLimit) {
        }

        std::vector<LineDiffHunk> Run() && {
            Compare(0, static_cast<std::ptrdiff_t>(a_.size()), 0, static_cast<std::ptrdiff_t>(b_.size()));
            return std::move(hunks_);
        }

      private:
        void Compare(std::ptrdiff_t a0, std::ptrdiff_t a1, std::ptrdiff_t b0, std::ptrdiff_t b1) {
            while (a0 < a1 && b0 < b1 && Same(a0, b0)) {
                ++a0;
                ++b0;
            }
            while (a0 < a1 && b0 < b1 && Same(a1 - 1, b1 - 1)) {
                --a1;
                --b1;
            }
            if (a0 < a1 && b0 < b1) {
                const std::optional<std::pair<std::ptrdiff_t, std::ptrdiff_t>> split = MiddleSnake(a0, a1, b0, b1);
                // A split on either corner would recurse on the same range.
                if (split && !(split->first == a0 && split->second == b0) && !(split->first == a1 && split->second == b1)) {
                    Compare(a0, split->first, b0, split->second);
                    Compare(split->first, a1, split->second, b1);
                    return;
                }
            }
            Emit(a0, a1, b0, b1);
        }

        // Where the forward and backward searches over a[a0, a1) and
        // b[b0, b1) meet, or nullopt once halfLimit_ edits each way haven't
        // met (the range is then reported as one hunk).
        std::optional<std::pair<std::ptrdiff_t, std::ptrdiff_t>> MiddleSnake(std::ptrdiff_t a0, std::ptrdiff_t a1, std::ptrdiff_t b0,
                                                                             std::ptrdiff_t b1) const {
            const std::ptrdiff_t n      = a1 - a0;
            const std::ptrdiff_t m      = b1 - b0;
            const std::ptrdiff_t maxD   = std::min((n + m + 1) / 2, halfLimit_);
            const std::ptrdiff_t offset = maxD + 1;
            // forward[offset + k]: furthest x on diagonal k = x - y from the
            // start; backward likewise, with x and y counted from the end.
            std::vector<std::ptrdiff_t> forward(static_cast<std::size_t>(2 * offset + 1), -1);
            std::vector<std::ptrdiff_t> backward(forward.size(), -1);
            forward[static_cast<std::size_t>(offset + 1)]  = 0;
            backward[static_cast<std::size_t>(offset + 1)] = 0;

            auto at      = [&](std::vector<std::ptrdiff_t>& v, std::ptrdiff_t k) -> std::ptrdiff_t& { return v[static_cast<std::size_t>(offset + k)]; };
            auto inRange = [&](std::ptrdiff_t k) { return offset + k >= 0 && offset + k < static_cast<std::ptrdiff_t>(forward.size()); };

            const std::ptrdiff_t delta = n - m;
            // With an odd delta the frontiers can only meet on a forward
            // step, with an even one on a backward step.
            const bool     front        = delta % 2 != 0;
            std::ptrdiff_t forwardStart = 0, forwardEnd = 0, backwardStart = 0, backwardEnd = 0;
            for (std::ptrdiff_t d = 0; d < maxD; ++d) {
                for (std::ptrdiff_t k = -d + forwardStart; k <= d - forwardEnd; k += 2) {
                    std::ptrdiff_t x = k == -d || (k != d && at(forward, k - 1) < at(forward, k + 1)) ? at(forward, k + 1) : at(forward, k - 1) + 1;
                    std::ptrdiff_t y = x - k;
                    while (x < n && y < m && Same(a0 + x, b0 + y)) {
                        ++x;
                        ++y;
                    }
                    at(forward, k) = x;
                    if (x > n) {
                        forwardEnd += 2;
                    }
                    else if (y > m) {
                        forwardStart += 2;
                    }
                    else if (front && inRange(delta - k) && at(backward, delta - k) != -1 && x >= n - at(backward, delta - k)) {
                        return std::pair{a0 + x, b0 + y};
                    }
                }
                for (std::ptrdiff_t k = -d + backwardStart; k <= d - backwardEnd; k += 2) {
                    std::ptrdiff_t x = k == -d || (k != d && at(backward, k - 1) < at(backward, k + 1)) ? at(backward, k + 1) : at(backward, k - 1) + 1;
                    std::ptrdiff_t y = x - k;
                    while (x < n && y < m && Same(a1 - x - 1, b1 - y - 1)) {
                        ++x;
                        ++y;
                    }
                    at(backward, k) = x;
                    if (x > n) {
                        backwardEnd += 2;
                    }
                    else if (y > m) {
                        backwardStart += 2;
                    }
                    else if (!front && inRange(delta - k) && at(forward, delta - k) != -1) {
                        const std::ptrdiff_t forwardX = at(forward, delta - k);
                        if (forwardX >= n - x) {
                            return std::pair{a0 + forwardX, b0 + forwardX - (delta - k)};
                        }
                    }
                }
            }
            return std::nullopt;
        }

        bool Same(std::ptrdiff_t i, std::ptrdiff_t j) const {
            return a_[static_cast<std::size_t>(i)] == b_[static_cast<std::size_t>(j)];
        }

        // Records a[a0, a1) replaced by b[b0, b1), joined onto the previous
        // hunk when no matching line separates them.
        void Emit(std::ptrdiff_t a0, std::ptrdiff_t a1, std::ptrdiff_t b0, std::ptrdiff_t b1) {
            if (a0 == a1 && b0 == b1) {
                return;
            }
            const auto aStart = static_cast<std::size_t>(a0);
            const auto bStart = static_cast<std::size_t>(b0);
            if (!hunks_.empty()) {
                LineDiffHunk& last = hunks_.back();
                if (last.aStart + last.aCount == aStart && last.bStart + last.bCount == bStart) {
                    last.aCount += static_cast<std::size_t>(a1 - a0);
                    last.bCount += static_cast<std::size_t>(b1 - b0);
                    return;
                }
            }
            hunks_.push_back(LineDiffHunk{aStart, static_cast<std::size_t>(a1 - a0), bStart, static_cast<std::size_t>(b1 - b0)});
        }

        const std::vector<std::string_view>& a_;
        const std::vector<std::string_view>& b_;
        std::ptrdiff_t                       halfLimit_;
        std::vector<LineDiffHunk>            hunks_;
    };

} // namespace

std::vector<LineDiffHunk> DiffLines(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
    // Past this much work, or this edit distance on small input, the texts
    // are effectively unrelated and the differing core is reported as one
    // hunk rather than searched to the end.
    constexpr std::ptrdiff_t kMaxWork         = 16'000'000;
    constexpr std::ptrdiff_t kMaxEditDistance = 2000;
    const std::ptrdiff_t     total            = std::max<std::ptrdiff_t>(1, static_cast<std::ptrdiff_t>(a.size() + b.size()));
    const std::ptrdiff_t     halfLimit        = std::max(kMaxEditDistance / 2, kMaxWork / total);
    return MyersDiff(a, b, halfLimit).Run();
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
