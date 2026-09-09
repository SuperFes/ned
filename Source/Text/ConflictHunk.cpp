#include "ConflictHunk.h"

namespace ned::text {

namespace {

    bool IsLineStart(std::string_view text, std::size_t pos) {
        return pos == 0 || text[pos - 1] == '\n';
    }

    // Next line-start occurrence of a 7-character labeled marker ("<<<<<<<",
    // "|||||||", ">>>>>>>") at or after `from` -- a labeled marker always
    // carries a trailing space before its label (matching
    // HasConflictMarkers's own "<<<<<<< " 8-byte check; an empty label after
    // the space still counts).
    std::size_t FindLabeledMarker(std::string_view text, std::size_t from, std::string_view marker) {
        for (std::size_t pos = from; pos < text.size(); ++pos) {
            if (IsLineStart(text, pos) && text.compare(pos, marker.size(), marker) == 0 &&
                pos + marker.size() < text.size() && text[pos + marker.size()] == ' ') {
                return pos;
            }
        }
        return std::string_view::npos;
    }

    // "=======" is the one marker with no label -- exactly 7 characters,
    // terminated by a newline or end of text.
    std::size_t FindSeparator(std::string_view text, std::size_t from) {
        for (std::size_t pos = from; pos < text.size(); ++pos) {
            if (IsLineStart(text, pos) && text.compare(pos, 7, "=======") == 0 &&
                (pos + 7 == text.size() || text[pos + 7] == '\n')) {
                return pos;
            }
        }
        return std::string_view::npos;
    }

    // One past the '\n' ending the line starting at `markerStart`, or
    // text.size() if that line has no trailing newline (EOF).
    std::size_t LineEndAfter(std::string_view text, std::size_t markerStart) {
        const std::size_t newline = text.find('\n', markerStart);
        return newline == std::string_view::npos ? text.size() : newline + 1;
    }

} // namespace

std::vector<ConflictHunk> ParseConflictHunks(std::string_view text) {
    std::vector<ConflictHunk> hunks;
    std::size_t               pos = 0;

    while (pos < text.size()) {
        const std::size_t startMarker = FindLabeledMarker(text, pos, "<<<<<<<");
        if (startMarker == std::string_view::npos) {
            break;
        }
        const std::size_t oursStart = LineEndAfter(text, startMarker);

        // A "|||||||" only counts as this hunk's diff3 base section if it
        // appears before the real separator -- a spurious match after it
        // (inside "theirs", or content that merely looks like a marker) is
        // ignored rather than misfiled.
        std::size_t       baseMarker = FindLabeledMarker(text, oursStart, "|||||||");
        const std::size_t sepMarker  = FindSeparator(text, oursStart);
        if (baseMarker != std::string_view::npos && (sepMarker == std::string_view::npos || baseMarker > sepMarker)) {
            baseMarker = std::string_view::npos;
        }

        // Bail on this "<<<<<<<" (malformed/unterminated) if there's no
        // separator at all, or a fresh "<<<<<<<" appears first -- nesting
        // isn't valid marker syntax. Resume scanning right after the
        // opening line so the fresh marker (if that's what caused the
        // bail) gets its own honest attempt next iteration.
        const std::size_t nestedBeforeSep = FindLabeledMarker(text, oursStart, "<<<<<<<");
        if (sepMarker == std::string_view::npos || (nestedBeforeSep != std::string_view::npos && nestedBeforeSep < sepMarker)) {
            pos = oursStart;
            continue;
        }

        std::optional<ConflictHunk::Range> baseRange;
        std::size_t                        oursEnd = sepMarker;
        if (baseMarker != std::string_view::npos) {
            oursEnd   = baseMarker;
            baseRange = ConflictHunk::Range{LineEndAfter(text, baseMarker), sepMarker};
        }

        const std::size_t theirsStart    = LineEndAfter(text, sepMarker);
        const std::size_t endMarker      = FindLabeledMarker(text, theirsStart, ">>>>>>>");
        const std::size_t nestedAfterSep = FindLabeledMarker(text, theirsStart, "<<<<<<<");
        if (endMarker == std::string_view::npos || (nestedAfterSep != std::string_view::npos && nestedAfterSep < endMarker)) {
            pos = oursStart;
            continue;
        }

        const std::size_t hunkEnd = LineEndAfter(text, endMarker);
        hunks.push_back(ConflictHunk{
            .startByte   = startMarker,
            .endByte     = hunkEnd,
            .oursRange   = ConflictHunk::Range{oursStart, oursEnd},
            .baseRange   = baseRange,
            .theirsRange = ConflictHunk::Range{theirsStart, endMarker},
        });

        pos = hunkEnd;
    }

    return hunks;
}

} // namespace ned::text
