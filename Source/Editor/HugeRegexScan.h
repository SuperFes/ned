//
// Windowed regex matching over a huge (Text/ITextStorage.h's IsHuge()) buffer, without
// ever materializing its content -- the shared primitive behind QueryReplace's huge-
// buffer path and Vim mode's / ? n N * # search on a huge buffer. Two consumers with a
// genuinely subtle, easy-to-get-wrong offset scheme (an earlier draft of this exact
// algorithm had a real bug caught only by a straddling-window test) are reason enough
// to share one implementation rather than risk two diverging copies.
//
// FindNextRegexMatchHuge (forward): two cursors drive the scan -- searchFrom (where the
// next match is allowed to start, monotonic) and an internal window reach that grows on
// retry. Each window starts kOverlapMargin bytes behind searchFrom (real lookbehind
// context) and is searched from searchFrom's own offset within it, so a match can never
// be reported before searchFrom, and the window's own synthetic start is never itself a
// reachable match position. A match found within kOverlapMargin bytes of the window's
// own end is not trusted unless the window reached the real document end -- reach grows
// and the same searchFrom is retried with a wider window (never advancing searchFrom
// past an unconfirmed candidate, which would skip a match starting in what becomes the
// new window's lead-in).
//
// FindLastRegexMatchHugeBefore (backward, for Vim's `?`): PCRE2 has no native backward
// search, so this scans forward within a window ending just past beforeOffset (a fixed
// kOverlapMargin of trailing completion context, matching FindNextRegexMatchHuge's own
// accepted-limit framing rather than growing it) and keeps the rightmost match with
// start < beforeOffset. Unlike the forward function, a match found this way needs no
// separate "near an edge, don't trust it" retry: any match located inside a window is
// unambiguously the correct answer regardless of how far the window's own start reaches
// back, since a match starting even earlier (outside the window) would only be *more*
// leftward, never a better ("more rightward") candidate -- widening the window (via
// reach) is only ever needed when a window comes up with no candidate at all.
//
// Both bound correctness to "a single match, or the lookaround/multi-line span it
// depends on, is at most kOverlapMargin bytes wide" -- the same class of accepted limit
// ProjectSearch's line-bounded RE2 path already lives with (no lookaround at all there).
//

#ifndef NED_EDITOR_HUGEREGEXSCAN_H
#define NED_EDITOR_HUGEREGEXSCAN_H

#include <cstddef>
#include <optional>
#include <string>

#include "RegexPattern.h"
#include "Text/Buffer.h"

namespace ned::editor {

// window/match are exactly what a caller needs to call
// RegexPattern::FormatReplacement itself (window as subject, match's offsets relative
// to it, including capture groups); windowStart converts a window-relative offset back
// to an absolute buffer offset.
struct HugeRegexMatch {
    std::string window;
    std::size_t windowStart = 0;
    RegexMatch  match;
};

// First match at or after searchFrom, or nullopt if the document holds no match from
// there to its end.
[[nodiscard]] std::optional<HugeRegexMatch> FindNextRegexMatchHuge(const text::Buffer& buffer,
                                                                   const RegexPattern& pattern,
                                                                   std::size_t         searchFrom);

// The last (rightmost) match with match.start < beforeOffset, or nullopt if none exists
// in [0, beforeOffset).
[[nodiscard]] std::optional<HugeRegexMatch> FindLastRegexMatchHugeBefore(const text::Buffer& buffer,
                                                                         const RegexPattern& pattern,
                                                                         std::size_t         beforeOffset);

} // namespace ned::editor

#endif // NED_EDITOR_HUGEREGEXSCAN_H
