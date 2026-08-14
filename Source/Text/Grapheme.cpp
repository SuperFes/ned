#include "Grapheme.h"

#include <utf8proc.h>

#include "Rope.h"

namespace ned::text {

namespace {

std::size_t BackNCodepoints(const Rope& rope, std::size_t byteOffset, std::size_t n) {
    std::size_t offset = byteOffset;
    for (std::size_t i = 0; i < n && offset > 0; ++i) {
        offset = rope.PreviousCodepointBoundary(offset);
    }
    return offset;
}

} // namespace

std::size_t NextGraphemeBoundary(const Rope& rope, std::size_t byteOffset) {
    const std::size_t total = rope.ByteLength();
    if (byteOffset >= total) {
        return total;
    }

    utf8proc_int32_t state = 0;

    const auto first = rope.CodepointAt(byteOffset);
    char32_t   cp1    = first.codepoint;
    std::size_t offset = byteOffset + first.byteLength;

    while (offset < total) {
        const auto next = rope.CodepointAt(offset);

        if (utf8proc_grapheme_break_stateful(static_cast<utf8proc_int32_t>(cp1), static_cast<utf8proc_int32_t>(next.codepoint), &state)) {
            return offset;
        }

        cp1 = next.codepoint;
        offset += next.byteLength;
    }

    return total;
}

std::size_t PreviousGraphemeBoundary(const Rope& rope, std::size_t byteOffset) {
    if (byteOffset == 0) {
        return 0;
    }

    std::size_t lookback = 32;

    // A grapheme cluster can in principle be arbitrarily long (a base character
    // followed by many combining marks). Scan backward a bounded window first
    // and widen it only if that window turns out not to contain a boundary,
    // rather than always paying for a full-document scan.
    for (int attempt = 0; attempt < 10; ++attempt) {
        const std::size_t scanStart = BackNCodepoints(rope, byteOffset, lookback);

        std::size_t lastBoundary   = scanStart;
        bool        foundBoundary = false;

        if (scanStart < byteOffset) {
            utf8proc_int32_t state = 0;

            const auto first = rope.CodepointAt(scanStart);
            char32_t   cp1    = first.codepoint;
            std::size_t offset = scanStart + first.byteLength;

            while (offset < byteOffset) {
                const auto next = rope.CodepointAt(offset);

                if (utf8proc_grapheme_break_stateful(static_cast<utf8proc_int32_t>(cp1), static_cast<utf8proc_int32_t>(next.codepoint), &state)) {
                    lastBoundary   = offset;
                    foundBoundary = true;
                }

                cp1 = next.codepoint;
                offset += next.byteLength;
            }
        }

        if (foundBoundary || scanStart == 0) {
            return lastBoundary;
        }

        lookback *= 4;
    }

    // Pathologically long cluster beyond the retry cap: return the widest point
    // scanned rather than looping forever.
    return BackNCodepoints(rope, byteOffset, lookback);
}

std::size_t SnapToGraphemeBoundary(const Rope& rope, std::size_t byteOffset) {
    const std::size_t total = rope.ByteLength();
    byteOffset              = byteOffset > total ? total : byteOffset;

    // Defensively snap to a codepoint boundary first: callers of this function
    // are exactly the ones that might hand in an arbitrary, unvalidated offset
    // (e.g. a future mouse click or a Janet-supplied integer). Round-tripping
    // through the codepoint index lands on the start of the *next* codepoint
    // when byteOffset falls strictly inside one (ByteOffsetToCodepointOffset
    // counts codepoint-starts before byteOffset, which is one past the
    // containing codepoint's own index) -- so step back one codepoint in that
    // case to land on the containing codepoint's start instead.
    {
        const std::size_t codepointOffset = rope.ByteOffsetToCodepointOffset(byteOffset);
        std::size_t        snapped         = rope.CodepointOffsetToByteOffset(codepointOffset);
        if (snapped != byteOffset) {
            snapped = rope.CodepointOffsetToByteOffset(codepointOffset - 1);
        }
        byteOffset = snapped;
    }

    if (byteOffset == 0 || byteOffset == total) {
        return byteOffset;
    }

    std::size_t lookback = 32;

    for (int attempt = 0; attempt < 10; ++attempt) {
        const std::size_t scanStart = BackNCodepoints(rope, byteOffset, lookback);

        std::size_t lastBoundary   = scanStart;
        bool        foundBoundary = false;

        utf8proc_int32_t state = 0;

        const auto first = rope.CodepointAt(scanStart);
        char32_t   cp1    = first.codepoint;
        std::size_t offset = scanStart + first.byteLength;

        while (offset <= byteOffset) {
            const auto next = rope.CodepointAt(offset);

            if (utf8proc_grapheme_break_stateful(static_cast<utf8proc_int32_t>(cp1), static_cast<utf8proc_int32_t>(next.codepoint), &state)) {
                lastBoundary   = offset;
                foundBoundary = true;
                if (offset == byteOffset) {
                    return byteOffset; // byteOffset is itself a real boundary
                }
            }

            cp1 = next.codepoint;
            offset += next.byteLength;
        }

        if (foundBoundary || scanStart == 0) {
            return lastBoundary;
        }

        lookback *= 4;
    }

    return BackNCodepoints(rope, byteOffset, lookback);
}

} // namespace ned::text
