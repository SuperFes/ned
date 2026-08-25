#include "VimTextObject.h"

#include <vector>

#include "Text/Grapheme.h"
#include "VimLineUtil.h"

namespace ned::editor::vim {

namespace {

    enum class CharClass { Blank,
                           Word,
                           Punct,
                           NonBlank };

    CharClass ClassOf(char32_t cp, bool bigWord) {
        if (IsBlankChar(cp)) {
            return CharClass::Blank;
        }
        if (bigWord) {
            return CharClass::NonBlank;
        }
        return IsWordChar(cp) ? CharClass::Word : CharClass::Punct;
    }

    std::size_t Next(const text::Buffer& buffer, std::size_t offset) {
        return text::NextGraphemeBoundary(buffer.Content(), offset);
    }

    std::size_t Prev(const text::Buffer& buffer, std::size_t offset) {
        return text::PreviousGraphemeBoundary(buffer.Content(), offset);
    }

    char32_t CodepointAt(const text::Buffer& buffer, std::size_t offset) {
        return buffer.Content().CodepointAt(offset).codepoint;
    }

} // namespace

ObjectRange InnerWord(const text::Buffer& buffer, std::size_t point, bool bigWord) {
    const std::size_t end = buffer.Content().ByteLength();
    if (point >= end) {
        return ObjectRange{point, point, false, false};
    }
    const CharClass cls = ClassOf(CodepointAt(buffer, point), bigWord);

    std::size_t start = point;
    while (start > 0) {
        const std::size_t prevOffset = Prev(buffer, start);
        const char32_t    cp         = CodepointAt(buffer, prevOffset);
        if (cp == U'\n' || ClassOf(cp, bigWord) != cls) {
            break;
        }
        start = prevOffset;
    }
    std::size_t stop = point;
    while (stop < end) {
        const char32_t cp = CodepointAt(buffer, stop);
        if (cp == U'\n' || ClassOf(cp, bigWord) != cls) {
            break;
        }
        stop = Next(buffer, stop);
    }
    return ObjectRange{start, stop, false, true};
}

ObjectRange AroundWord(const text::Buffer& buffer, std::size_t point, bool bigWord) {
    const ObjectRange inner = InnerWord(buffer, point, bigWord);
    if (!inner.found) {
        return inner;
    }
    const std::size_t end = buffer.Content().ByteLength();

    std::size_t trailing = inner.end;
    while (trailing < end) {
        const char32_t cp = CodepointAt(buffer, trailing);
        if (cp != U' ' && cp != U'\t') {
            break;
        }
        trailing = Next(buffer, trailing);
    }
    if (trailing > inner.end) {
        return ObjectRange{inner.start, trailing, false, true};
    }
    std::size_t leading = inner.start;
    while (leading > 0) {
        const std::size_t prevOffset = Prev(buffer, leading);
        const char32_t    cp         = CodepointAt(buffer, prevOffset);
        if (cp != U' ' && cp != U'\t') {
            break;
        }
        leading = prevOffset;
    }
    return ObjectRange{leading, inner.end, false, true};
}

namespace {

    // Every unescaped occurrence of quote on point's own line, in order.
    std::vector<std::size_t> QuotePositionsOnLine(const text::Buffer& buffer, std::size_t point, char32_t quote) {
        const std::size_t line  = LineOf(buffer, point);
        const std::size_t start = LineStart(buffer, line);
        const std::size_t end   = LineContentEnd(buffer, line);

        std::vector<std::size_t> positions;
        std::size_t              p = start;
        while (p < end) {
            if (CodepointAt(buffer, p) == quote) {
                positions.push_back(p);
            }
            p = Next(buffer, p);
        }
        return positions;
    }

    // Pairs up consecutive quote positions and returns the pair containing point, or (if
    // point isn't inside any pair) the first pair starting at/after point -- real vim's
    // own "jump forward to the next quoted string on the line" behavior.
    bool FindQuotePair(const text::Buffer& buffer, std::size_t point, char32_t quote, std::size_t& openOut, std::size_t& closeOut) {
        const std::vector<std::size_t> positions = QuotePositionsOnLine(buffer, point, quote);
        for (std::size_t i = 0; i + 1 < positions.size(); i += 2) {
            const std::size_t openPos  = positions[i];
            const std::size_t closePos = positions[i + 1];
            if (point >= openPos && point <= closePos) {
                openOut  = openPos;
                closeOut = closePos;
                return true;
            }
        }
        for (std::size_t i = 0; i + 1 < positions.size(); i += 2) {
            if (positions[i] >= point) {
                openOut  = positions[i];
                closeOut = positions[i + 1];
                return true;
            }
        }
        return false;
    }

} // namespace

ObjectRange InnerQuote(const text::Buffer& buffer, std::size_t point, char32_t quote) {
    std::size_t openPos  = 0;
    std::size_t closePos = 0;
    if (!FindQuotePair(buffer, point, quote, openPos, closePos)) {
        return ObjectRange{point, point, false, false};
    }
    return ObjectRange{Next(buffer, openPos), closePos, false, true};
}

ObjectRange AroundQuote(const text::Buffer& buffer, std::size_t point, char32_t quote) {
    std::size_t openPos  = 0;
    std::size_t closePos = 0;
    if (!FindQuotePair(buffer, point, quote, openPos, closePos)) {
        return ObjectRange{point, point, false, false};
    }
    return ObjectRange{openPos, Next(buffer, closePos), false, true};
}

namespace {

    bool FindEnclosingBracket(const text::Buffer& buffer, std::size_t point, char32_t open, char32_t close, std::size_t& openOut,
                              std::size_t& closeOut) {
        // Backward scan from point: a `close` seen before point needs one more nested
        // `open` to cancel out; a `close` exactly AT point (scan == point) doesn't count
        // against itself, so a cursor resting on the closing bracket still resolves to
        // its own pair.
        long        depth     = 0;
        std::size_t scan      = point;
        bool        foundOpen = false;
        while (true) {
            const char32_t cp = CodepointAt(buffer, scan);
            if (cp == close && scan != point) {
                ++depth;
            }
            else if (cp == open) {
                if (depth == 0) {
                    foundOpen = true;
                    openOut   = scan;
                    break;
                }
                --depth;
            }
            if (scan == 0) {
                break;
            }
            scan = Prev(buffer, scan);
        }
        if (!foundOpen) {
            return false;
        }

        const std::size_t end = buffer.Content().ByteLength();
        depth                 = 0;
        scan                  = Next(buffer, openOut);
        while (scan < end) {
            const char32_t cp = CodepointAt(buffer, scan);
            if (cp == open) {
                ++depth;
            }
            else if (cp == close) {
                if (depth == 0) {
                    closeOut = scan;
                    return true;
                }
                --depth;
            }
            scan = Next(buffer, scan);
        }
        return false;
    }

} // namespace

ObjectRange InnerBracket(const text::Buffer& buffer, std::size_t point, char32_t open, char32_t close) {
    std::size_t openPos  = 0;
    std::size_t closePos = 0;
    if (!FindEnclosingBracket(buffer, point, open, close, openPos, closePos)) {
        return ObjectRange{point, point, false, false};
    }
    return ObjectRange{Next(buffer, openPos), closePos, false, true};
}

ObjectRange AroundBracket(const text::Buffer& buffer, std::size_t point, char32_t open, char32_t close) {
    std::size_t openPos  = 0;
    std::size_t closePos = 0;
    if (!FindEnclosingBracket(buffer, point, open, close, openPos, closePos)) {
        return ObjectRange{point, point, false, false};
    }
    return ObjectRange{openPos, Next(buffer, closePos), false, true};
}

namespace {

    struct LineRun {
        std::size_t startLine;
        std::size_t endLine; // inclusive
    };

    LineRun BlankOrContentRun(const text::Buffer& buffer, std::size_t line, bool blank) {
        const std::size_t last  = EffectiveLastLine(buffer);
        std::size_t       start = line;
        while (start > 0 && IsBlankLine(buffer, start - 1) == blank) {
            --start;
        }
        std::size_t stop = line;
        while (stop < last && IsBlankLine(buffer, stop + 1) == blank) {
            ++stop;
        }
        return LineRun{start, stop};
    }

    std::size_t RegionEnd(const text::Buffer& buffer, std::size_t endLine) {
        const std::size_t last = EffectiveLastLine(buffer);
        return endLine < last ? LineStart(buffer, endLine + 1) : buffer.Content().ByteLength();
    }

} // namespace

ObjectRange InnerParagraph(const text::Buffer& buffer, std::size_t point) {
    const std::size_t line = LineOf(buffer, point);
    const LineRun     run  = BlankOrContentRun(buffer, line, IsBlankLine(buffer, line));
    return ObjectRange{LineStart(buffer, run.startLine), RegionEnd(buffer, run.endLine), true, true};
}

ObjectRange AroundParagraph(const text::Buffer& buffer, std::size_t point) {
    const std::size_t line       = LineOf(buffer, point);
    const bool        startBlank = IsBlankLine(buffer, line);
    const LineRun     inner      = BlankOrContentRun(buffer, line, startBlank);
    const std::size_t last       = EffectiveLastLine(buffer);

    if (inner.endLine < last && IsBlankLine(buffer, inner.endLine + 1) != startBlank) {
        const LineRun trailing = BlankOrContentRun(buffer, inner.endLine + 1, !startBlank);
        return ObjectRange{LineStart(buffer, inner.startLine), RegionEnd(buffer, trailing.endLine), true, true};
    }
    if (inner.startLine > 0 && IsBlankLine(buffer, inner.startLine - 1) != startBlank) {
        const LineRun leading = BlankOrContentRun(buffer, inner.startLine - 1, !startBlank);
        return ObjectRange{LineStart(buffer, leading.startLine), RegionEnd(buffer, inner.endLine), true, true};
    }
    return ObjectRange{LineStart(buffer, inner.startLine), RegionEnd(buffer, inner.endLine), true, true};
}

} // namespace ned::editor::vim
