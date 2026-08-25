#include "VimTextObject.h"

#include <cctype>
#include <optional>
#include <string>
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

ObjectRange InnerWord(const text::Buffer& buffer, std::size_t point, bool bigWord, long count) {
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
    // A count beyond 1 extends stop by that many additional same-class runs -- real vim's
    // own "2iw" spans a word plus the whitespace/word run(s) after it, alternating
    // naturally since each run's class differs from its predecessor.
    for (long i = 1; i < count && stop < end; ++i) {
        const char32_t nextCp = CodepointAt(buffer, stop);
        if (nextCp == U'\n') {
            break;
        }
        const CharClass nextCls = ClassOf(nextCp, bigWord);
        while (stop < end) {
            const char32_t cp = CodepointAt(buffer, stop);
            if (cp == U'\n' || ClassOf(cp, bigWord) != nextCls) {
                break;
            }
            stop = Next(buffer, stop);
        }
    }
    return ObjectRange{start, stop, false, true};
}

ObjectRange AroundWord(const text::Buffer& buffer, std::size_t point, bool bigWord, long count) {
    const ObjectRange inner = InnerWord(buffer, point, bigWord, count);
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

namespace {

    // Mirrors Buffer::MoveForwardSentence/MoveBackwardSentence's own "., !, ?" end-mark
    // convention (Text/Buffer.cpp) by hand, kept in sync manually -- reused directly would
    // be the wrong shape here: those are *motions*, and their own documented real-vim-
    // faithful behavior is that landing exactly on a sentence's first character and moving
    // backward again jumps to the *previous* sentence, whereas a text object's "the
    // sentence containing point" query must still select the current sentence even when
    // point already sits at its first character.
    bool IsSentenceEndCp(char32_t cp) {
        return cp == U'.' || cp == U'!' || cp == U'?';
    }

    bool IsSentenceSpaceCp(char32_t cp) {
        return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r';
    }

    std::size_t SentenceStart(const text::Buffer& buffer, std::size_t point) {
        std::size_t scan = point;
        while (scan > 0) {
            const std::size_t prevOffset = Prev(buffer, scan);
            if (IsSentenceEndCp(CodepointAt(buffer, prevOffset))) {
                break;
            }
            scan = prevOffset;
        }
        const std::size_t end = buffer.Content().ByteLength();
        while (scan < end && IsSentenceSpaceCp(CodepointAt(buffer, scan))) {
            scan = Next(buffer, scan);
        }
        return scan;
    }

    // Position right after `start`'s own sentence-ending mark (trailing whitespace
    // excluded); buffer length if the sentence runs off the end unterminated.
    std::size_t SentenceEnd(const text::Buffer& buffer, std::size_t start) {
        const std::size_t end  = buffer.Content().ByteLength();
        std::size_t       scan = start;
        while (scan < end && !IsSentenceEndCp(CodepointAt(buffer, scan))) {
            scan = Next(buffer, scan);
        }
        if (scan < end) {
            scan = Next(buffer, scan);
        }
        return scan;
    }

} // namespace

ObjectRange InnerSentence(const text::Buffer& buffer, std::size_t point, long count) {
    const std::size_t total = buffer.Content().ByteLength();
    if (total == 0) {
        return ObjectRange{0, 0, false, false};
    }
    const std::size_t probe = point < total ? point : Prev(buffer, total);
    const std::size_t start = SentenceStart(buffer, probe);
    std::size_t       end   = SentenceEnd(buffer, start);
    for (long i = 1; i < count && end < total; ++i) {
        std::size_t nextStart = end;
        while (nextStart < total && IsSentenceSpaceCp(CodepointAt(buffer, nextStart))) {
            nextStart = Next(buffer, nextStart);
        }
        if (nextStart >= total) {
            break;
        }
        end = SentenceEnd(buffer, nextStart);
    }
    return ObjectRange{start, end, false, true};
}

ObjectRange AroundSentence(const text::Buffer& buffer, std::size_t point, long count) {
    const ObjectRange inner = InnerSentence(buffer, point, count);
    if (!inner.found) {
        return inner;
    }
    const std::size_t total = buffer.Content().ByteLength();

    std::size_t trailing = inner.end;
    while (trailing < total && IsSentenceSpaceCp(CodepointAt(buffer, trailing))) {
        trailing = Next(buffer, trailing);
    }
    if (trailing > inner.end) {
        return ObjectRange{inner.start, trailing, false, true};
    }
    std::size_t leading = inner.start;
    while (leading > 0) {
        const std::size_t prevOffset = Prev(buffer, leading);
        if (!IsSentenceSpaceCp(CodepointAt(buffer, prevOffset))) {
            break;
        }
        leading = prevOffset;
    }
    return ObjectRange{leading, inner.end, false, true};
}

namespace {

    struct TagToken {
        std::size_t start = 0; // index of '<'
        std::size_t end   = 0; // index right after '>' (exclusive)
        std::string name;
        bool        isClosing     = false;
        bool        isSelfClosing = false;
    };

    bool IsTagNameChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':';
    }

    // Plain byte scanning, not a real HTML/XML parser -- matching every other object in
    // this file. Known limitation: a '>' inside a quoted attribute value (e.g.
    // `<div title="a>b">`) ends the token early; rare enough in practice not to be worth
    // a real attribute-value scanner here.
    std::optional<TagToken> ParseTagAt(const std::string& text, std::size_t lt) {
        const std::size_t gt = text.find('>', lt + 1);
        if (gt == std::string::npos) {
            return std::nullopt;
        }
        TagToken token;
        token.start = lt;
        token.end   = gt + 1;

        std::size_t nameStart = lt + 1;
        if (nameStart < text.size() && text[nameStart] == '/') {
            token.isClosing = true;
            ++nameStart;
        }
        std::size_t nameEnd = nameStart;
        while (nameEnd < gt && IsTagNameChar(text[nameEnd])) {
            ++nameEnd;
        }
        if (nameEnd == nameStart) {
            return std::nullopt; // "<>"/"< foo>"/a stray '<' used as a comparison operator
        }
        token.name          = text.substr(nameStart, nameEnd - nameStart);
        token.isSelfClosing = gt > lt && text[gt - 1] == '/';
        return token;
    }

    // Scans backward from point for the nearest unmatched opening tag (a stack of
    // pending closing-tag names, mirroring FindEnclosingBracket's depth counter but keyed
    // by name since tags nest by name rather than by character), then forward from it for
    // the matching closing tag, tracking same-name nesting depth the same way. Self-
    // closing tags (<br/>) are skipped entirely on both scans -- they never enclose
    // anything.
    bool FindEnclosingTag(const text::Buffer& buffer, std::size_t point, ObjectRange& innerOut, ObjectRange& aroundOut) {
        const std::string text = buffer.Content().Substring(0, buffer.Content().ByteLength());

        std::vector<std::string> pendingCloses;
        std::size_t              searchFrom = std::min(point, text.size());
        std::optional<TagToken>  openTag;
        while (searchFrom > 0) {
            const std::size_t lt = text.rfind('<', searchFrom - 1);
            if (lt == std::string::npos) {
                break;
            }
            const auto token = ParseTagAt(text, lt);
            if (!token) {
                searchFrom = lt;
                continue;
            }
            if (token->isSelfClosing) {
                // Never encloses anything -- skip.
            }
            else if (token->isClosing) {
                pendingCloses.push_back(token->name);
            }
            else if (!pendingCloses.empty() && pendingCloses.back() == token->name) {
                pendingCloses.pop_back(); // a fully-nested pair, already balanced
            }
            else {
                openTag = token;
                break;
            }
            searchFrom = lt;
        }
        if (!openTag) {
            return false;
        }

        std::size_t             depth = 0;
        std::size_t             pos   = openTag->end;
        std::optional<TagToken> closeTag;
        while (pos < text.size()) {
            const std::size_t lt = text.find('<', pos);
            if (lt == std::string::npos) {
                break;
            }
            const auto token = ParseTagAt(text, lt);
            if (!token) {
                pos = lt + 1;
                continue;
            }
            if (!token->isSelfClosing && token->name == openTag->name) {
                if (token->isClosing) {
                    if (depth == 0) {
                        closeTag = token;
                        break;
                    }
                    --depth;
                }
                else {
                    ++depth;
                }
            }
            pos = token->end;
        }
        if (!closeTag) {
            return false;
        }

        innerOut  = ObjectRange{openTag->end, closeTag->start, false, true};
        aroundOut = ObjectRange{openTag->start, closeTag->end, false, true};
        return true;
    }

} // namespace

ObjectRange InnerTag(const text::Buffer& buffer, std::size_t point) {
    ObjectRange inner;
    ObjectRange around;
    if (!FindEnclosingTag(buffer, point, inner, around)) {
        return ObjectRange{point, point, false, false};
    }
    return inner;
}

ObjectRange AroundTag(const text::Buffer& buffer, std::size_t point) {
    ObjectRange inner;
    ObjectRange around;
    if (!FindEnclosingTag(buffer, point, inner, around)) {
        return ObjectRange{point, point, false, false};
    }
    return around;
}

} // namespace ned::editor::vim
