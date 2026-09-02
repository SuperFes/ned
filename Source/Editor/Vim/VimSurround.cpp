#include "VimSurround.h"

#include "VimTextObject.h"
#include "VimTypes.h"

namespace ned::editor::vim {

namespace {

    // Resolves `from` to the same (open, close)-delimiter bracket/quote/tag pair
    // VimTextObject.h's own i(/a( family already scans for, returning both the inner and
    // around ranges in one call (ds/cs need both: around's edges are exactly the delimiter
    // bytes to remove/replace, inner's edges are where the replacement delimiter goes).
    bool FindDelimiterPair(const text::Buffer& buffer, std::size_t point, char32_t from, ObjectRange& innerOut, ObjectRange& aroundOut) {
        switch (from) {
            case U'"':
            case U'\'':
            case U'`':
                innerOut  = InnerQuote(buffer, point, from);
                aroundOut = AroundQuote(buffer, point, from);
                return innerOut.found;
            case U'(':
            case U')':
            case U'b':
                innerOut  = InnerBracket(buffer, point, U'(', U')');
                aroundOut = AroundBracket(buffer, point, U'(', U')');
                return innerOut.found;
            case U'[':
            case U']':
                innerOut  = InnerBracket(buffer, point, U'[', U']');
                aroundOut = AroundBracket(buffer, point, U'[', U']');
                return innerOut.found;
            case U'{':
            case U'}':
            case U'B':
                innerOut  = InnerBracket(buffer, point, U'{', U'}');
                aroundOut = AroundBracket(buffer, point, U'{', U'}');
                return innerOut.found;
            case U'<':
            case U'>':
                innerOut  = InnerBracket(buffer, point, U'<', U'>');
                aroundOut = AroundBracket(buffer, point, U'<', U'>');
                return innerOut.found;
            case U't':
                innerOut  = InnerTag(buffer, point);
                aroundOut = AroundTag(buffer, point);
                return innerOut.found;
            default:
                return false;
        }
    }

} // namespace

std::optional<SurroundDelim> ResolveSurroundTarget(char32_t target) {
    switch (target) {
        case U'"':
            return SurroundDelim{"\"", "\""};
        case U'\'':
            return SurroundDelim{"'", "'"};
        case U'`':
            return SurroundDelim{"`", "`"};
        case U'(':
            return SurroundDelim{"( ", " )"};
        case U')':
        case U'b':
            return SurroundDelim{"(", ")"};
        case U'[':
            return SurroundDelim{"[ ", " ]"};
        case U']':
            return SurroundDelim{"[", "]"};
        case U'{':
            return SurroundDelim{"{ ", " }"};
        case U'}':
        case U'B':
            return SurroundDelim{"{", "}"};
        case U'<':
        case U'>':
            return SurroundDelim{"<", ">"};
        default:
            return std::nullopt;
    }
}

bool DeleteSurroundAtPoint(text::Buffer& buffer, char32_t from) {
    ObjectRange inner;
    ObjectRange around;
    if (!FindDelimiterPair(buffer, buffer.Point(), from, inner, around)) {
        return false;
    }
    buffer.BeginUndoGroup();
    // Close delimiter first (the higher offset) so removing it can't shift the open
    // delimiter's own [around.start, inner.start) offsets out from under the second
    // deletion.
    if (around.end > inner.end) {
        buffer.DeleteRange(inner.end, around.end - inner.end);
    }
    if (inner.start > around.start) {
        buffer.DeleteRange(around.start, inner.start - around.start);
    }
    buffer.SetPoint(around.start);
    buffer.EndUndoGroup();
    return true;
}

bool ChangeSurroundAtPoint(text::Buffer& buffer, char32_t from, char32_t to) {
    ObjectRange inner;
    ObjectRange around;
    if (!FindDelimiterPair(buffer, buffer.Point(), from, inner, around)) {
        return false;
    }
    const std::optional<SurroundDelim> delim = ResolveSurroundTarget(to);
    if (!delim) {
        return false;
    }
    buffer.BeginUndoGroup();
    // Same high-to-low ordering as DeleteSurroundAtPoint: replace the close delimiter
    // (touches only offsets >= inner.end) before the open one, so around.start/inner.start
    // stay valid throughout.
    if (around.end > inner.end) {
        buffer.DeleteRange(inner.end, around.end - inner.end);
    }
    buffer.InsertAt(inner.end, delim->close);
    if (inner.start > around.start) {
        buffer.DeleteRange(around.start, inner.start - around.start);
    }
    buffer.InsertAt(around.start, delim->open);
    buffer.SetPoint(around.start);
    buffer.EndUndoGroup();
    return true;
}

bool AddSurround(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t to, std::size_t& pointOut) {
    const std::optional<SurroundDelim> delim = ResolveSurroundTarget(to);
    if (!delim) {
        return false;
    }
    buffer.BeginUndoGroup();
    // Close first (the higher offset) so inserting it doesn't shift `start` out from
    // under the second insertion.
    buffer.InsertAt(end, delim->close);
    buffer.InsertAt(start, delim->open);
    buffer.EndUndoGroup();
    pointOut = start;
    return true;
}

} // namespace ned::editor::vim
