#include "Indent.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include <limits>

#include "HugeStructuralWindow.h"
#include "TabWidth.h"
#include "TreeSitter/Node.h"

namespace ned::editor {

namespace {

    // A row value no real Node::StartRow() can ever return -- see the
    // non-dedent branch of IndentLevelForLine's own comment for why an
    // ancestor-walk seed sometimes needs "never matches any real row"
    // instead of a real row to compare against.
    constexpr std::size_t kNoRow = std::numeric_limits<std::size_t>::max();

    // First byte of the line's first non-space/non-tab codepoint, or lineEnd
    // if the whole [lineStart, lineEnd) span is blank -- the same "leading
    // whitespace end" LineIndentEnd computes, but bounded to a known [start,
    // end) span rather than scanning the whole buffer, since the tree-walk
    // already has lineEnd in hand.
    std::size_t FirstNonBlankByte(std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd) {
        for (std::size_t i = lineStart; i < lineEnd; ++i) {
            if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                return i;
            }
        }
        return lineEnd;
    }

    // @aligned-paren-column-alignment follow-up: visual column of byteOffset
    // within its own line, given that line's own start -- tab-stop aware
    // (using width as the tab-stop size, the same role IndentStyle::width
    // already plays for SetLineIndent/IndentString below), codepoint-count
    // rather than true display width otherwise. This is the same "count
    // codepoints, not real display columns" cut Mode.cpp's own Markdown
    // fenced-code passthrough already documents and accepts -- an aligned
    // continuation target is virtually always ASCII source code, not prose,
    // so codepoint count and byte count coincide in every real case this
    // matters for.
    int VisualColumnInLine(std::string_view bufferText, std::size_t lineStart, std::size_t byteOffset, int width) {
        int column = 0;
        for (std::size_t i = lineStart; i < byteOffset && i < bufferText.size(); ++i) {
            if (bufferText[i] == '\t') {
                column = ((column / width) + 1) * width;
            }
            else {
                ++column;
            }
        }
        return column;
    }

    // @aligned-paren-column-alignment follow-up: byte offset of byteOffset's
    // own line start -- shared by ResolveAlignedColumn and (real-per-form-
    // lisp-indent follow-up) ContainerOwnColumn below, both of which need
    // "what line is this byte on" before they can measure a visual column
    // within it.
    std::size_t LineStartFor(std::string_view bufferText, std::size_t byteOffset) {
        if (byteOffset == 0) {
            return 0;
        }
        const std::size_t newline = bufferText.rfind('\n', byteOffset - 1);
        return (newline == std::string_view::npos) ? 0 : newline + 1;
    }

    // @aligned-paren-column-alignment follow-up: an @aligned-captured
    // container's own opening delimiter is assumed to be a single-byte ASCII
    // token ("(", "[", "{") starting exactly at container.StartByte() -- true
    // for every bundled grammar's own call-argument/condition-list shape
    // this capture is meant for. Returns the visual column of the first
    // non-space/non-tab byte following that delimiter, PROVIDED it's still on
    // the delimiter's own source line -- std::nullopt when the delimiter is
    // the last real thing on its line (nothing to align to; the caller falls
    // back to treating the container as a plain @indent instead).
    std::optional<int> ResolveAlignedColumn(const treesitter::Node& container, std::string_view bufferText, int width) {
        const std::size_t delimiterEnd = container.StartByte() + 1;
        if (delimiterEnd > bufferText.size()) {
            return std::nullopt;
        }
        std::size_t lineEnd = bufferText.find('\n', delimiterEnd);
        if (lineEnd == std::string_view::npos) {
            lineEnd = bufferText.size();
        }
        std::size_t contentStart = delimiterEnd;
        while (contentStart < lineEnd && (bufferText[contentStart] == ' ' || bufferText[contentStart] == '\t')) {
            ++contentStart;
        }
        if (contentStart >= lineEnd) {
            return std::nullopt; // opener is alone on its own line -- nothing to align to
        }
        return VisualColumnInLine(bufferText, LineStartFor(bufferText, container.StartByte()), contentStart, width);
    }

    // real-per-form-lisp-indent follow-up: an @indent.body-captured
    // container's own visual column (where its opening "(" itself sits, NOT
    // its line's own leading indentation -- these differ whenever the form
    // isn't the first thing on its line, e.g. "(foo (let [x 1]" -- the let's
    // body indents relative to let's OWN column, matching real Emacs
    // lisp-indent-function behavior for a special form nested mid-line).
    int ContainerOwnColumn(const treesitter::Node& container, std::string_view bufferText, int width) {
        return VisualColumnInLine(bufferText, LineStartFor(bufferText, container.StartByte()), container.StartByte(),
                                  width);
    }

} // namespace

std::optional<IndentComputation> IndentLevelForLine(const treesitter::Tree& tree, std::string_view bufferText,
                                                     const treesitter::Query& indentQuery, std::size_t lineStart,
                                                     std::size_t lineEnd, const IndentStyle& style) {
    if (tree.IsNull()) {
        return std::nullopt;
    }

    // A dedent capture only decides how this LINE aligns when it's the
    // line's own first non-blank byte -- a closing delimiter that follows
    // real content earlier on the same line (e.g. a one-line "1}") isn't a
    // dedent LINE at all, just an ordinary content line that happens to end
    // with a closer; that case must fall through to the ordinary
    // content-based branch below, computing indent from the real leading
    // content ("1"), not from the trailing closer's own alignment rule.
    const std::size_t contentStart = FirstNonBlankByte(bufferText, lineStart, lineEnd);

    // Keyed by the captured node's own stable identity (Node::Id()), NOT its
    // byte range -- a (startByte, endByte) pair can't disambiguate two
    // DIFFERENT nodes that happen to span the exact same bytes, which is a
    // real, not hypothetical, case (see Node::Id()'s own doc comment for
    // tree-sitter-python's "block" node coinciding byte-for-byte with its
    // own single statement when that statement is the block's only one).
    std::unordered_set<const void*> indentIds;
    // @aligned-paren-column-alignment follow-up: a container captured
    // "aligned" instead of "indent" -- see ResolveAlignedColumn/the walk
    // below for what distinguishes it.
    std::unordered_set<const void*> alignedIds;
    // real-per-form-lisp-indent follow-up: a container captured
    // "indent.body" -- a Lisp special form (let/fn/defn/...) whose body
    // indents a fixed 2 columns past the form's own column, Emacs'
    // lisp-indent-function convention, rather than one style.width-multiple
    // level or an @aligned first-argument column. A node captured BOTH this
    // and "aligned" by the same query (the common case -- see
    // janet-indents.scm/clojure-indents.scm's own comments) is treated as
    // indent.body, checked first in the walk below.
    std::unordered_set<const void*> bodyIndentIds;
    const void*                     dedentNodeId = nullptr; // set only when a dedent capture starts this line
    // smart-blank-line-on-newline follow-up: every dedent capture's own
    // [startByte, endByte) range, not just whichever one (if any) starts at
    // contentStart -- the end-of-buffer rescue below needs to recognize
    // "the last real byte before this new blank line is itself a closing
    // delimiter" in general, not only when it happens to be THIS line's own
    // dedent.
    std::vector<std::pair<std::size_t, std::size_t>> dedentRanges;
    for (const treesitter::QueryCapture& capture : indentQuery.Captures(tree.RootNode(), bufferText)) {
        if (capture.name == "indent") {
            indentIds.insert(capture.nodeId);
        }
        else if (capture.name == "aligned") {
            alignedIds.insert(capture.nodeId);
        }
        else if (capture.name == "indent.body") {
            bodyIndentIds.insert(capture.nodeId);
        }
        else if (capture.name == "dedent") {
            dedentRanges.emplace_back(capture.startByte, capture.endByte);
            if (capture.startByte == contentStart) {
                dedentNodeId = capture.nodeId;
            }
        }
    }

    const auto isIndentCaptured  = [&indentIds](const treesitter::Node& node) { return indentIds.contains(node.Id()); };
    const auto isAlignedCaptured = [&alignedIds](const treesitter::Node& node) { return alignedIds.contains(node.Id()); };
    const auto isBodyIndentCaptured = [&bodyIndentIds](const treesitter::Node& node) {
        return bodyIndentIds.contains(node.Id());
    };

    // Resolves `position` (either a real line's contentStart, or -- for the
    // dedent branch below -- an align target's own StartByte, computing "as
    // if for that target's own opening line") to the walk's starting node:
    // the smallest NAMED node whose range contains `position`.
    //
    // For most bundled grammars, a captured container's own opening
    // delimiter is an ANONYMOUS token ("{", "(", an "if"/"def" keyword) --
    // NamedDescendantForByteRange transparently skips straight past it to
    // the smallest NAMED node, which for a bare-opener-alone-on-its-own-line
    // case IS the captured container itself (object, compound_statement,
    // ...), letting levelForWalkStart's own self-exclusion (below) fire
    // correctly with no extra help. HTML/XML are the one bundled exception
    // (confirmed via a real parse dump, not assumed): a "start_tag"/"STag"
    // node (e.g. the whole "<p>" opening tag) is itself NAMED, so resolution
    // stops there -- one level short of the captured "element" it's a part
    // of. Promoting past it here (once, only for this specific, well-known
    // node-type shape) is what lets levelForWalkStart treat HTML/XML exactly
    // like every other grammar's anonymous-opener case, without teaching the
    // otherwise-generic walk algorithm itself about any particular language.
    const auto resolveWalkStart = [&](std::size_t position) {
        treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(position, position);
        if (!node.IsNull() && (node.Type() == "start_tag" || node.Type() == "STag")) {
            const treesitter::Node parent = node.Parent();
            if (!parent.IsNull() && parent.StartByte() == position) {
                node = parent;
            }
        }
        return node;
    };

    // Given walkStart and the position it was resolved for, walks ancestors
    // (starting at walkStart itself) counting each @indent-captured node
    // whose own row differs from the last-counted one, EXCEPT that the
    // first (innermost) @aligned-captured ancestor encountered that (a)
    // doesn't itself open exactly at `position` (see the `!opensAtPosition`
    // guard below -- the same self-exclusion problem @indent's own lastRow
    // seed solves, needed separately here since alignment resolution isn't
    // gated on row-distinctness at all) and (b) has real content following
    // its own opening delimiter on the delimiter's own line, short-circuits
    // the walk entirely: the result is that content's own column PLUS
    // whatever indent levels were already counted strictly inside it (e.g. a
    // multi-line object literal passed as a call argument still indents its
    // OWN body relative to the call's alignment column, not from column
    // zero). An @aligned container with nothing following its opener on its
    // own line (bare "foo(\n") is treated exactly like a plain @indent
    // capture instead -- there's no column to align to, so it just
    // contributes one ordinary level and the walk continues outward as
    // usual. Self-disqualify
    // walkStart from counting (seed lastRow = its own row) ONLY when it is
    // ITSELF an @indent/@aligned/@indent.body-captured container that
    // genuinely opens exactly at `position` (a bare "{" alone on its own
    // line, or -- after resolveWalkStart's promotion above -- HTML/XML's
    // "element"): its own row truly IS the target line's row there. This
    // must cover @aligned/@indent.body too, not just @indent -- an @aligned
    // walkStart self-opening at `position` already skips the align-
    // resolution attempt itself (guarded by opensAtPosition inside the loop
    // below), but without this same exclusion here it would still fall
    // through and wrongly count itself as one plain level (caught by a real
    // failing test: a top-level Janet/Clojure form's own closing paren,
    // computed "as if for the opener's own line," must resolve to level 0,
    // not 1, when the form's head is an ordinary call and thus @aligned
    // rather than @indent). In every other case --
    // walkStart isn't captured at all (an "if" keyword's enclosing
    // if_statement, never captured; only its own ancestor "block" is), or
    // it's captured but opened on an earlier line (the innermost enclosing
    // object for an otherwise-blank line) -- seed kNoRow (never equal to a
    // real StartRow()) instead, so the walk's first REAL captured ancestor
    // always counts on its own merits.
    //
    // This distinction is load-bearing, not cosmetic: a language whose
    // indent-scope node has no distinct opening delimiter of its own
    // (Python's "block", scoped to exactly its first statement's own
    // start/row; YAML's nested block_mapping, scoped to exactly its first
    // pair's own start/row -- both confirmed via a real parse dump) can have
    // an uncaptured intermediate node share the EXACT row of a real captured
    // ancestor arbitrarily many levels up (Python's if_statement below its
    // own enclosing "block"; YAML's block_mapping_pair below ITS OWN
    // enclosing block_mapping). Seeding from an uncaptured node's row would
    // wrongly suppress a captured ancestor as if it were "the same visual
    // line already counted" -- it never was; only walkStart's OWN row ever
    // gets to seed that exclusion.
    const auto computeForWalkStart = [&](const treesitter::Node& walkStart, std::size_t position) -> IndentComputation {
        const bool  selfOpensHere = (isIndentCaptured(walkStart) || isAlignedCaptured(walkStart) ||
                                    isBodyIndentCaptured(walkStart)) &&
                                   walkStart.StartByte() == position;
        std::size_t lastRow = selfOpensHere ? walkStart.StartRow() : kNoRow;
        int         level   = 0;
        for (treesitter::Node node = walkStart; !node.IsNull(); node = node.Parent()) {
            const bool opensAtPosition = node.StartByte() == position;
            if (isBodyIndentCaptured(node) && !opensAtPosition) {
                // Unlike @aligned, a special form's body indent never falls
                // back -- it's always 2 columns past the form's own column,
                // regardless of what (if anything) follows the opener on
                // its own line.
                const int column = ContainerOwnColumn(node, bufferText, style.width) + 2;
                return IndentComputation{IndentComputation::Kind::Column, column + IndentColumnForLevel(level, style)};
            }
            if (isAlignedCaptured(node) && !opensAtPosition) {
                if (const std::optional<int> column = ResolveAlignedColumn(node, bufferText, style.width)) {
                    return IndentComputation{IndentComputation::Kind::Column, *column + IndentColumnForLevel(level, style)};
                }
                // Unresolved (opener alone on its own line) -- falls through
                // to the plain @indent-shaped counting below, same as any
                // other captured container.
            }
            if ((isIndentCaptured(node) || isAlignedCaptured(node) || isBodyIndentCaptured(node)) &&
                node.StartRow() != lastRow) {
                ++level;
                lastRow = node.StartRow();
            }
        }
        return IndentComputation{IndentComputation::Kind::Level, level};
    };

    std::optional<IndentComputation> result;
    if (dedentNodeId != nullptr) {
        // The dedent-captured node itself may be anonymous (a literal "}")
        // or named (HTML/XML's "end_tag" -- a whole "</div>" node, not a
        // single token) -- DescendantForByteRange (unnamed-inclusive) at
        // contentStart finds whatever is truly SMALLEST at that position,
        // which for a named capture can be one of ITS OWN anonymous
        // children (e.g. end_tag's own leading "</" token) rather than the
        // captured node itself. Walk up from there by real node identity
        // (Node::Id(), not a byte-range/IsNamed() guess) until the node
        // that identity-matches the actual capture is found -- correct
        // regardless of which shape the query captured.
        treesitter::Node dedentNode = tree.RootNode().DescendantForByteRange(contentStart, contentStart);
        while (!dedentNode.IsNull() && dedentNode.Id() != dedentNodeId) {
            dedentNode = dedentNode.Parent();
        }
        if (dedentNode.IsNull()) {
            return std::nullopt;
        }
        const treesitter::Node alignNode = dedentNode.Parent();
        if (alignNode.IsNull()) {
            result = IndentComputation{IndentComputation::Kind::Level, 0};
        }
        else {
            // Compute indent AS IF for alignNode's own opening line -- this
            // is what makes a closing delimiter align with its opener's own
            // line rather than one level deeper, whether or not alignNode
            // is itself @indent-captured (a bracket/brace container is;
            // Python's if_statement -- elif/else/except/finally's own
            // alignNode -- is not, and still resolves correctly via the
            // same self-exclusion rule rather than assuming captured-ness).
            const treesitter::Node walkStart = resolveWalkStart(alignNode.StartByte());
            result = walkStart.IsNull() ? std::optional<IndentComputation>(IndentComputation{IndentComputation::Kind::Level, 0})
                                        : computeForWalkStart(walkStart, alignNode.StartByte());
        }
    }
    else {
        const treesitter::Node walkStart = resolveWalkStart(contentStart);
        result = walkStart.IsNull() ? std::optional<IndentComputation>(IndentComputation{IndentComputation::Kind::Level, 0})
                                    : computeForWalkStart(walkStart, contentStart);

        // smart-blank-line-on-newline follow-up: a freshly inserted,
        // not-yet-typed blank line (Mode.h's own "lineStart == lineEnd"
        // convention -- what "newline", Commands.cpp, passes for the line
        // it just created) sitting at the very tail of the document can
        // fall entirely OUTSIDE every captured container's own byte range,
        // even though it's exactly where a user just pressed Enter from
        // inside one. Confirmed via a real failing test, not assumed:
        // Python's own "block" node (python-indents.scm -- no closing
        // delimiter to capture) ends its range exactly at its last real
        // statement's own end, one byte short of where the new blank line
        // starts, so resolveWalkStart(contentStart) lands outside it and
        // the walk above returns a bare Level(0) with no real information
        // in it at all. Re-resolve as if standing one byte EARLIER --
        // still inside whatever real content just precedes -- but ONLY
        // when the primary walk found nothing captured (Level(0)) AND
        // contentStart is genuinely the document's own tail: narrowly
        // scoped so it can never affect an already-correct dedent (real,
        // typed content resolving to level 0 keeps doing so unchanged),
        // and a mid-document blank line with more of the same block still
        // ahead never needs the rescue at all -- its primary resolution
        // already lands inside the block, since the block's own range
        // naturally spans the gap up to that later content.
        if (lineStart == lineEnd && contentStart == bufferText.size() && contentStart > 0 &&
            result->kind == IndentComputation::Kind::Level && result->value == 0) {
            // Skip back over ALL trailing whitespace (not just one byte) --
            // an earlier blank line or two between the new one and the last
            // real content (e.g. this is the THIRD consecutive Enter, or a
            // stray blank line was already there) must still resolve at the
            // last real, non-whitespace byte, not merely the byte
            // immediately before contentStart.
            const std::size_t rescuePosition = bufferText.find_last_not_of(" \t\n\r", contentStart - 1);
            // ...but NOT when that last real byte is itself part of a
            // closing delimiter (a real, not hypothetical, failure caught
            // by a test: the file's OWN final "}" is "the last real byte"
            // just as validly as an ordinary statement is, and resolving
            // there must NOT be treated as "still open" -- a closer ends
            // whatever it closes, it doesn't extend it). Checked against
            // every dedent capture's own range, not just an anonymous
            // single-char token -- HTML/XML's "end_tag" ("</div>") is a
            // whole multi-byte named node, not one byte, so a single-
            // character check wouldn't catch it.
            const bool rescuePositionIsDedent =
                rescuePosition != std::string_view::npos &&
                std::any_of(dedentRanges.begin(), dedentRanges.end(), [&](const std::pair<std::size_t, std::size_t>& range) {
                    return rescuePosition >= range.first && rescuePosition < range.second;
                });
            if (rescuePosition != std::string_view::npos && !rescuePositionIsDedent) {
                const treesitter::Node rescueWalkStart = resolveWalkStart(rescuePosition);
                if (!rescueWalkStart.IsNull()) {
                    result = computeForWalkStart(rescueWalkStart, rescuePosition);
                }
            }
        }
    }
    return result;
}

IndentFunction BuildIndentFunction(std::shared_ptr<treesitter::Parser> parser, std::shared_ptr<treesitter::Query> indentQuery,
                                   std::shared_ptr<treesitter::IncrementalParseCache> sharedParse, std::string modeName) {
    return [parser, indentQuery, sharedParse, modeName](std::string_view bufferText, std::size_t lineStart,
                                                        std::size_t lineEnd) -> std::optional<int> {
        const treesitter::Tree& tree  = sharedParse->Update(*parser, bufferText);
        const IndentStyle       style = EffectiveIndentStyle(modeName);
        const std::optional<IndentComputation> result =
            IndentLevelForLine(tree, bufferText, *indentQuery, lineStart, lineEnd, style);
        if (!result) {
            return std::nullopt;
        }
        if (result->kind == IndentComputation::Kind::Column) {
            return result->value;
        }
        return IndentColumnForLevel(result->value, style);
    };
}

int IndentColumnForLevel(int level, const IndentStyle& style) {
    return std::max(0, level) * std::max(1, style.width);
}

std::size_t LineIndentEnd(const text::ITextStorage& content, std::size_t lineStart) {
    const std::size_t length = content.ByteLength();
    std::size_t        offset = lineStart;
    while (offset < length) {
        const text::ITextStorage::DecodedCodepoint decoded = content.CodepointAt(offset);
        if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
            break;
        }
        offset += decoded.byteLength;
    }
    return offset;
}

std::string IndentString(int column, const IndentStyle& style) {
    if (column <= 0) {
        return {};
    }
    const auto columns = static_cast<std::size_t>(column);
    if (!style.useTabs) {
        return std::string(columns, ' ');
    }
    const auto width  = static_cast<std::size_t>(std::max(1, style.width));
    const std::size_t tabs    = columns / width;
    const std::size_t spaces = columns % width;
    std::string        result(tabs, '\t');
    result.append(spaces, ' ');
    return result;
}

std::ptrdiff_t SetLineIndent(text::Buffer& buffer, std::size_t lineStart, int column, const IndentStyle& style) {
    const std::size_t indentEnd  = LineIndentEnd(buffer.Content(), lineStart);
    const std::size_t oldLength  = indentEnd - lineStart;
    const std::string desired    = IndentString(column, style);
    if (desired.size() == oldLength && buffer.Content().Substring(lineStart, oldLength) == desired) {
        return 0; // already correct -- don't touch the buffer/undo tree for a genuine no-op
    }

    buffer.BeginUndoGroup();
    buffer.DeleteRange(lineStart, oldLength);
    buffer.InsertAt(lineStart, desired);
    buffer.EndUndoGroup();
    return static_cast<std::ptrdiff_t>(desired.size()) - static_cast<std::ptrdiff_t>(oldLength);
}

std::size_t IndentRegion(text::Buffer& buffer, const Mode& mode, std::size_t startLine, std::size_t endLineExclusive) {
    if (!mode.indentColumn) {
        return 0;
    }

    const IndentStyle style = EffectiveIndentStyle(mode.name);

    // huge-file-indent-windowing follow-up: for a huge (ITextStorage::
    // IsHuge()) buffer, bound what gets handed to mode.indentColumn to a
    // window around [startLine, endLineExclusive) padded by
    // HugeStructuralWindowBytes() on each side, mirroring BufferView::
    // HugeStructuralWindow's exact shape (byte-padded, then snapped to line
    // boundaries) -- the same "huge-file structural gutters" pattern
    // CLAUDE.md documents for BufferView's own fold/symbol/test caches,
    // applied here instead of buffer.Text()'s unconditional full
    // materialize. windowStartLine/windowEndLineExclusive are LINE INDICES,
    // computed once and stable for the whole loop below: SetLineIndent only
    // ever changes a line's own leading whitespace BYTE length, never the
    // document's line count. An ordinary (non-huge) buffer is completely
    // unaffected -- the window always spans the whole document, and huge
    // stays false, so every line still takes the original buffer.Text() path
    // byte-for-byte unchanged.
    const text::ITextStorage& initialContent          = buffer.Content();
    const bool                 huge                    = initialContent.IsHuge();
    std::size_t                 windowStartLine         = 0;
    std::size_t                 windowEndLineExclusive = initialContent.LineCount();
    if (huge) {
        const std::size_t margin          = HugeStructuralWindowBytes();
        const std::size_t regionStartByte = initialContent.LineToByteOffset(startLine);
        const std::size_t regionEndByte   = (endLineExclusive < initialContent.LineCount())
                                                ? initialContent.LineToByteOffset(endLineExclusive)
                                                : initialContent.ByteLength();
        const std::size_t rawWindowStartByte = regionStartByte > margin ? regionStartByte - margin : 0;
        windowStartLine                     = initialContent.ByteOffsetToLine(rawWindowStartByte);

        const std::size_t byteLength      = initialContent.ByteLength();
        const std::size_t rawWindowEndByte = (byteLength - regionEndByte > margin) ? regionEndByte + margin : byteLength;
        windowEndLineExclusive = std::min(initialContent.ByteOffsetToLine(rawWindowEndByte) + 1, initialContent.LineCount());
    }

    buffer.BeginUndoGroup();
    std::size_t changed = 0;
    // Bottom-to-top: reindenting a line's own leading whitespace never
    // shifts the byte offsets of any earlier, still-to-process line, so no
    // re-derivation pass is needed between lines (see this function's own
    // doc comment in Indent.h).
    for (std::size_t line = endLineExclusive; line-- > startLine;) {
        const text::ITextStorage& content = buffer.Content();
        if (line >= content.LineCount()) {
            continue; // out of range -- nothing to do (defensive, shouldn't happen bottom-to-top)
        }
        const std::size_t lineStart = content.LineToByteOffset(line);
        std::size_t        lineEnd   = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
        if (line + 1 < content.LineCount() && lineEnd > lineStart) {
            --lineEnd; // exclude the line's own trailing '\n'
        }

        std::optional<int> column;
        if (huge) {
            // Re-fetched every line, like buffer.Text() was before -- an
            // earlier iteration's own edit (still below windowStart in file
            // order, since we walk bottom-to-top) can shift windowEnd's own
            // byte offset, but never windowStart's; bounded to the window's
            // size either way, not the whole document's.
            const std::size_t windowStartByte = content.LineToByteOffset(windowStartLine);
            const std::size_t windowEndByte   = (windowEndLineExclusive < content.LineCount())
                                                    ? content.LineToByteOffset(windowEndLineExclusive)
                                                    : content.ByteLength();
            const std::string windowText = content.Substring(windowStartByte, windowEndByte - windowStartByte);
            column = mode.indentColumn(windowText, lineStart - windowStartByte, lineEnd - windowStartByte);
        }
        else {
            const std::string text = buffer.Text(); // see this function's own doc comment on this cost
            column                 = mode.indentColumn(text, lineStart, lineEnd);
        }
        if (!column) {
            continue;
        }
        if (SetLineIndent(buffer, lineStart, *column, style) != 0) {
            ++changed;
        }
    }
    buffer.EndUndoGroup();
    return changed;
}

std::size_t IndentBuffer(text::Buffer& buffer, const Mode& mode) {
    return IndentRegion(buffer, mode, 0, buffer.Content().LineCount());
}

std::size_t RigidShiftRegion(text::Buffer& buffer, const IndentStyle& style, std::size_t startLine,
                             std::size_t endLineExclusive, int deltaLevels) {
    const int width    = std::max(1, style.width);
    const int tabWidth  = TabWidth();

    buffer.BeginUndoGroup();
    std::size_t changed = 0;
    // Bottom-to-top, same reasoning as IndentRegion.
    for (std::size_t line = endLineExclusive; line-- > startLine;) {
        const text::ITextStorage& content = buffer.Content();
        if (line >= content.LineCount()) {
            continue;
        }
        const std::size_t lineStart      = content.LineToByteOffset(line);
        const std::size_t indentEnd      = LineIndentEnd(content, lineStart);
        const std::size_t currentColumn  = buffer.VisualColumnForByteOffset(lineStart, indentEnd, static_cast<std::size_t>(tabWidth));
        const int          newColumn      = std::max(0, static_cast<int>(currentColumn) + deltaLevels * width);
        if (SetLineIndent(buffer, lineStart, newColumn, style) != 0) {
            ++changed;
        }
    }
    buffer.EndUndoGroup();
    return changed;
}

} // namespace ned::editor
