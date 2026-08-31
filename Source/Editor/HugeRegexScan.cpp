#include "HugeRegexScan.h"

#include <algorithm>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {
    constexpr std::size_t kWindowBody    = 4 * 1024 * 1024;
    constexpr std::size_t kOverlapMargin = 64 * 1024;
} // namespace

std::optional<HugeRegexMatch> FindNextRegexMatchHuge(const text::Buffer& buffer, const RegexPattern& pattern,
                                                     std::size_t searchFrom) {
    const std::size_t total = buffer.Content().ByteLength();
    if (searchFrom > total) {
        return std::nullopt;
    }

    std::size_t reach = kWindowBody;
    for (;;) {
        const std::size_t windowStart = (searchFrom >= kOverlapMargin) ? searchFrom - kOverlapMargin : 0;
        const std::size_t windowEnd   = std::min(total, searchFrom + reach);
        const std::string window      = buffer.Content().Substring(windowStart, windowEnd - windowStart);

        const bool                      atDocEnd = (windowEnd == total);
        const std::optional<RegexMatch> match    = pattern.Search(window, searchFrom - windowStart);

        if (match.has_value()) {
            const bool nearTail = !atDocEnd && (window.size() - match->end) < kOverlapMargin;
            if (!nearTail) {
                return HugeRegexMatch{window, windowStart, *match};
            }
            reach += kWindowBody; // possibly truncated -- widen and retry from the same searchFrom
            continue;
        }

        if (atDocEnd) {
            return std::nullopt;
        }
        // No match anywhere in [searchFrom, windowEnd) -- safe to skip the whole window;
        // a match can't start earlier than searchFrom (excluded from this search) or
        // inside a range just proven empty.
        searchFrom = windowEnd;
        reach      = kWindowBody;
    }
}

std::optional<HugeRegexMatch> FindLastRegexMatchHugeBefore(const text::Buffer& buffer, const RegexPattern& pattern,
                                                           std::size_t beforeOffset) {
    const std::size_t total      = buffer.Content().ByteLength();
    const std::size_t upperBound = std::min(beforeOffset, total);

    std::size_t reach = kWindowBody;
    for (;;) {
        const std::size_t windowStart = (upperBound >= reach) ? upperBound - reach : 0;
        const std::size_t windowEnd   = std::min(total, upperBound + kOverlapMargin);
        const std::string window      = buffer.Content().Substring(windowStart, windowEnd - windowStart);
        const bool        atDocStart  = (windowStart == 0);

        std::optional<RegexMatch> best;
        std::size_t               from = 0;
        while (from <= window.size()) {
            const std::optional<RegexMatch> match = pattern.Search(window, from);
            if (!match.has_value()) {
                break;
            }
            if (windowStart + match->start >= upperBound) {
                break; // matches are found in non-decreasing start order -- nothing further qualifies
            }
            best = match;
            if (match->end == match->start) {
                // Zero-width match: step one codepoint, or stop if there's nothing left
                // to step over (matches RegexPattern::ReplaceAll's own stepping).
                const std::size_t next = text::NextCodepointBoundary(window, match->end);
                if (next == match->end) {
                    break;
                }
                from = next;
            }
            else {
                from = match->end;
            }
        }

        if (best.has_value()) {
            return HugeRegexMatch{window, windowStart, *best};
        }

        if (atDocStart) {
            return std::nullopt;
        }
        reach += kWindowBody; // no candidate in this window at all -- look further back
    }
}

} // namespace ned::editor
