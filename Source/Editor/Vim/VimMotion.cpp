#include "VimMotion.h"

#include <algorithm>

#include "Text/Grapheme.h"
#include "VimLineUtil.h"

namespace ned::editor::vim {

namespace {

    enum class CharClass { Blank,
                           Word,
                           Punct,
                           NonBlank }; // NonBlank only used for bigWord

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

    // Real vim's "a run of empty lines is itself a word" quirk -- true when offset is
    // exactly the start of a blank line.
    bool IsEmptyLineStart(const text::Buffer& buffer, std::size_t offset) {
        const std::size_t line = LineOf(buffer, offset);
        return LineStart(buffer, line) == offset && IsBlankLine(buffer, line);
    }

    // True when offset is the last grapheme of a maximal same-class run -- i.e. a "word
    // end" in vim's sense (the grapheme after it is absent, a newline, or a different
    // class).
    bool IsWordEndPosition(const text::Buffer& buffer, std::size_t offset, std::size_t end, bool bigWord) {
        const char32_t cp = CodepointAt(buffer, offset);
        if (cp == U'\n' || IsBlankChar(cp)) {
            return false;
        }
        const std::size_t next = Next(buffer, offset);
        if (next >= end) {
            return true;
        }
        const char32_t cpNext = CodepointAt(buffer, next);
        return cpNext == U'\n' || ClassOf(cpNext, bigWord) != ClassOf(cp, bigWord);
    }

} // namespace

MotionResult CharLeft(const text::Buffer& buffer, std::size_t point, long count) {
    const std::size_t lineStart = LineStart(buffer, LineOf(buffer, point));
    std::size_t       p         = point;
    for (long i = 0; i < count && p > lineStart; ++i) {
        p = Prev(buffer, p);
    }
    return MotionResult{p, false, false, true};
}

MotionResult CharRight(const text::Buffer& buffer, std::size_t point, long count) {
    const std::size_t line       = LineOf(buffer, point);
    const std::size_t lineStart  = LineStart(buffer, line);
    const std::size_t contentEnd = LineContentEnd(buffer, line);
    // l rests ON the last character, never past it -- contentEnd is one-past-the-end
    // (Emacs-style), so the rightmost grapheme boundary l may land on is one grapheme
    // back from there.
    const std::size_t rightmost = contentEnd > lineStart ? Prev(buffer, contentEnd) : lineStart;
    std::size_t       p         = point;
    for (long i = 0; i < count && p < rightmost; ++i) {
        p = Next(buffer, p);
    }
    return MotionResult{p, false, false, true};
}

MotionResult LineStartMotion(const text::Buffer& buffer, std::size_t point) {
    return MotionResult{LineStart(buffer, LineOf(buffer, point)), false, false, true};
}

MotionResult FirstNonBlankMotion(const text::Buffer& buffer, std::size_t point) {
    return MotionResult{FirstNonBlankOffset(buffer, LineOf(buffer, point)), false, false, true};
}

MotionResult LineEndMotion(const text::Buffer& buffer, std::size_t point, long count) {
    const std::size_t lastLine   = EffectiveLastLine(buffer);
    const std::size_t targetLine = std::min(lastLine, LineOf(buffer, point) + static_cast<std::size_t>(count > 0 ? count - 1 : 0));
    const std::size_t end        = LineContentEnd(buffer, targetLine);
    const std::size_t start      = LineStart(buffer, targetLine);
    return MotionResult{end > start ? Prev(buffer, end) : start, false, true, true};
}

namespace {

    std::size_t ColumnToOffset(const text::Buffer& buffer, std::size_t line, std::size_t goalColumn, std::size_t tabWidth) {
        return buffer.ByteOffsetForLineAndColumn(line, goalColumn, tabWidth);
    }

} // namespace

MotionResult LineDown(const text::Buffer& buffer, std::size_t point, long count, std::size_t goalColumn, std::size_t tabWidth) {
    const std::size_t lastLine   = EffectiveLastLine(buffer);
    const std::size_t targetLine = std::min(lastLine, LineOf(buffer, point) + static_cast<std::size_t>(std::max<long>(0, count)));
    return MotionResult{ColumnToOffset(buffer, targetLine, goalColumn, tabWidth), true, false, true};
}

MotionResult LineUp(const text::Buffer& buffer, std::size_t point, long count, std::size_t goalColumn, std::size_t tabWidth) {
    const std::size_t currentLine = LineOf(buffer, point);
    const std::size_t delta       = static_cast<std::size_t>(std::max<long>(0, count));
    const std::size_t targetLine  = delta > currentLine ? 0 : currentLine - delta;
    return MotionResult{ColumnToOffset(buffer, targetLine, goalColumn, tabWidth), true, false, true};
}

MotionResult GotoFirstLine(const text::Buffer& buffer, long count) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    const std::size_t line     = count > 0 ? std::min(lastLine, static_cast<std::size_t>(count - 1)) : 0;
    return MotionResult{FirstNonBlankOffset(buffer, line), true, false, true};
}

MotionResult GotoLastLine(const text::Buffer& buffer, long count) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    const std::size_t line     = count > 0 ? std::min(lastLine, static_cast<std::size_t>(count - 1)) : lastLine;
    return MotionResult{FirstNonBlankOffset(buffer, line), true, false, true};
}

MotionResult WordForward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord) {
    const std::size_t end = buffer.Content().ByteLength();
    std::size_t       p   = point;
    for (long i = 0; i < count; ++i) {
        if (p >= end) {
            break;
        }
        const CharClass startClass = ClassOf(CodepointAt(buffer, p), bigWord);
        if (startClass != CharClass::Blank) {
            while (p < end) {
                const char32_t cp = CodepointAt(buffer, p);
                if (cp == U'\n' || ClassOf(cp, bigWord) != startClass) {
                    break;
                }
                p = Next(buffer, p);
            }
        }
        // Skip blanks/newlines, stopping immediately on landing at an empty line's start
        // (vim's "empty lines are their own word" rule).
        while (p < end) {
            const char32_t cp = CodepointAt(buffer, p);
            if (cp != U'\n' && !IsBlankChar(cp)) {
                break;
            }
            p = Next(buffer, p);
            if (p < end && IsEmptyLineStart(buffer, p)) {
                break;
            }
        }
    }
    return MotionResult{p, false, false, true};
}

MotionResult WordBackward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord) {
    std::size_t p = point;
    for (long i = 0; i < count; ++i) {
        if (p == 0) {
            break;
        }
        p = Prev(buffer, p);
        while (p > 0) {
            const char32_t cp = CodepointAt(buffer, p);
            if (cp != U'\n' && !IsBlankChar(cp)) {
                break;
            }
            if (IsEmptyLineStart(buffer, p)) {
                break;
            }
            p = Prev(buffer, p);
        }
        if (IsEmptyLineStart(buffer, p)) {
            continue; // landed on an empty-line "word" -- nothing more to do this step
        }
        const CharClass cls = ClassOf(CodepointAt(buffer, p), bigWord);
        while (p > 0) {
            const std::size_t prevOffset = Prev(buffer, p);
            const char32_t    cp         = CodepointAt(buffer, prevOffset);
            if (cp == U'\n' || ClassOf(cp, bigWord) != cls) {
                break;
            }
            p = prevOffset;
        }
    }
    return MotionResult{p, false, false, true};
}

MotionResult WordEndForward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord) {
    const std::size_t end = buffer.Content().ByteLength();
    std::size_t       p   = point;
    for (long i = 0; i < count; ++i) {
        if (p >= end) {
            break;
        }
        p = Next(buffer, p);
        while (p < end) {
            const char32_t cp = CodepointAt(buffer, p);
            if (cp != U'\n' && !IsBlankChar(cp)) {
                break;
            }
            p = Next(buffer, p);
        }
        if (p >= end) {
            break;
        }
        const CharClass cls = ClassOf(CodepointAt(buffer, p), bigWord);
        while (true) {
            const std::size_t peek = Next(buffer, p);
            if (peek >= end) {
                break;
            }
            const char32_t cp = CodepointAt(buffer, peek);
            if (cp == U'\n' || ClassOf(cp, bigWord) != cls) {
                break;
            }
            p = peek;
        }
    }
    return MotionResult{p, false, true, true};
}

MotionResult WordEndBackward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord) {
    const std::size_t end = buffer.Content().ByteLength();
    std::size_t       p   = point;
    for (long i = 0; i < count; ++i) {
        if (p == 0) {
            break;
        }
        p = Prev(buffer, p);
        while (p > 0 && !IsWordEndPosition(buffer, p, end, bigWord) && !IsEmptyLineStart(buffer, p)) {
            p = Prev(buffer, p);
        }
    }
    return MotionResult{p, false, true, true};
}

MotionResult FindChar(const text::Buffer& buffer, std::size_t point, long count, char32_t target, bool forward, bool till) {
    const std::size_t line      = LineOf(buffer, point);
    const std::size_t lineStart = LineStart(buffer, line);
    const std::size_t lineEnd   = LineContentEnd(buffer, line);

    std::size_t p         = point;
    long        remaining = count;
    if (forward) {
        while (remaining > 0) {
            std::size_t scan = Next(buffer, p);
            bool        hit  = false;
            while (scan < lineEnd) {
                if (CodepointAt(buffer, scan) == target) {
                    p   = scan;
                    hit = true;
                    break;
                }
                scan = Next(buffer, scan);
            }
            if (!hit) {
                return MotionResult{point, false, false, false};
            }
            --remaining;
        }
        if (till) {
            p = Prev(buffer, p);
        }
        return MotionResult{p, false, true, true};
    }
    while (remaining > 0) {
        if (p <= lineStart) {
            return MotionResult{point, false, false, false};
        }
        std::size_t scan = Prev(buffer, p);
        bool        hit  = false;
        while (true) {
            if (CodepointAt(buffer, scan) == target) {
                p   = scan;
                hit = true;
                break;
            }
            if (scan <= lineStart) {
                break;
            }
            scan = Prev(buffer, scan);
        }
        if (!hit) {
            return MotionResult{point, false, false, false};
        }
        --remaining;
    }
    if (till) {
        p = Next(buffer, p);
    }
    return MotionResult{p, false, false, true};
}

MotionResult ParagraphForward(const text::Buffer& buffer, std::size_t point, long count) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    std::size_t       line     = LineOf(buffer, point);
    for (long i = 0; i < count; ++i) {
        std::size_t scan = line + 1;
        while (scan < lastLine && !IsBlankLine(buffer, scan)) {
            ++scan;
        }
        line = std::min(scan, lastLine);
    }
    return MotionResult{LineStart(buffer, line), false, false, true};
}

MotionResult ParagraphBackward(const text::Buffer& buffer, std::size_t point, long count) {
    std::size_t line = LineOf(buffer, point);
    for (long i = 0; i < count; ++i) {
        if (line == 0) {
            break;
        }
        std::size_t scan = line - 1;
        while (scan > 0 && !IsBlankLine(buffer, scan)) {
            --scan;
        }
        line = scan;
    }
    return MotionResult{LineStart(buffer, line), false, false, true};
}

MotionResult MatchPair(const text::Buffer& buffer, std::size_t point) {
    static constexpr std::string_view kOpeners = "([{";
    static constexpr std::string_view kClosers = ")]}";

    const std::size_t lineEnd = LineContentEnd(buffer, LineOf(buffer, point));
    std::size_t       p       = point;
    char              ch      = 0;
    bool              isOpen  = false;
    std::size_t       idx     = 0;
    while (p < lineEnd) {
        const char32_t cp = CodepointAt(buffer, p);
        if (cp < 128) {
            const auto openPos  = kOpeners.find(static_cast<char>(cp));
            const auto closePos = kClosers.find(static_cast<char>(cp));
            if (openPos != std::string_view::npos) {
                ch     = static_cast<char>(cp);
                isOpen = true;
                idx    = openPos;
                break;
            }
            if (closePos != std::string_view::npos) {
                ch     = static_cast<char>(cp);
                isOpen = false;
                idx    = closePos;
                break;
            }
        }
        p = Next(buffer, p);
    }
    if (p >= lineEnd) {
        return MotionResult{point, false, false, false};
    }

    const char        openChar  = kOpeners[idx];
    const char        closeChar = kClosers[idx];
    const std::size_t end       = buffer.Content().ByteLength();
    int               depth     = 0;
    if (isOpen) {
        std::size_t scan = p;
        while (scan < end) {
            const char32_t cp = CodepointAt(buffer, scan);
            if (cp == static_cast<char32_t>(openChar)) {
                ++depth;
            }
            else if (cp == static_cast<char32_t>(closeChar)) {
                --depth;
                if (depth == 0) {
                    return MotionResult{scan, false, true, true};
                }
            }
            scan = Next(buffer, scan);
        }
    }
    else {
        std::size_t scan = p;
        while (true) {
            const char32_t cp = CodepointAt(buffer, scan);
            if (cp == static_cast<char32_t>(closeChar)) {
                ++depth;
            }
            else if (cp == static_cast<char32_t>(openChar)) {
                --depth;
                if (depth == 0) {
                    return MotionResult{scan, false, true, true};
                }
            }
            if (scan == 0) {
                break;
            }
            scan = Prev(buffer, scan);
        }
    }
    return MotionResult{point, false, false, false};
}

MotionResult ScreenTop(const text::Buffer& buffer, std::size_t topLine, std::size_t /*viewportHeight*/) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    const std::size_t line     = std::min(topLine, lastLine);
    return MotionResult{FirstNonBlankOffset(buffer, line), true, false, true};
}

MotionResult ScreenBottom(const text::Buffer& buffer, std::size_t topLine, std::size_t viewportHeight) {
    const std::size_t lastLine = EffectiveLastLine(buffer);
    const std::size_t line     = std::min(lastLine, topLine + (viewportHeight > 0 ? viewportHeight - 1 : 0));
    return MotionResult{FirstNonBlankOffset(buffer, line), true, false, true};
}

MotionResult ScreenMiddle(const text::Buffer& buffer, std::size_t topLine, std::size_t viewportHeight) {
    const std::size_t lastLine     = EffectiveLastLine(buffer);
    const std::size_t visibleCount = std::min(viewportHeight, lastLine - std::min(topLine, lastLine) + 1);
    const std::size_t line         = std::min(lastLine, topLine + visibleCount / 2);
    return MotionResult{FirstNonBlankOffset(buffer, line), true, false, true};
}

} // namespace ned::editor::vim
