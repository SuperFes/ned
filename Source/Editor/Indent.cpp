#include "Indent.h"

#include <algorithm>
#include <unordered_set>

#include <limits>

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

} // namespace

std::optional<int> IndentLevelForLine(const treesitter::Tree& tree, std::string_view bufferText,
                                      const treesitter::Query& indentQuery, std::size_t lineStart, std::size_t lineEnd) {
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
    const void*                     dedentNodeId = nullptr; // set only when a dedent capture starts this line
    for (const treesitter::QueryCapture& capture : indentQuery.Captures(tree.RootNode(), bufferText)) {
        if (capture.name == "indent") {
            indentIds.insert(capture.nodeId);
        }
        else if (capture.name == "dedent" && capture.startByte == contentStart) {
            dedentNodeId = capture.nodeId;
        }
    }

    const auto isIndentCaptured = [&indentIds](const treesitter::Node& node) { return indentIds.contains(node.Id()); };

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
    // whose own row differs from the last-counted one. Self-disqualify
    // walkStart from counting (seed lastRow = its own row) ONLY when it is
    // ITSELF an @indent-captured container that genuinely opens exactly at
    // `position` (a bare "{" alone on its own line, or -- after
    // resolveWalkStart's promotion above -- HTML/XML's "element"): its own
    // row truly IS the target line's row there. In every other case --
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
    const auto levelForWalkStart = [&](const treesitter::Node& walkStart, std::size_t position) {
        const bool  selfOpensHere = isIndentCaptured(walkStart) && walkStart.StartByte() == position;
        std::size_t lastRow       = selfOpensHere ? walkStart.StartRow() : kNoRow;
        int         level         = 0;
        for (treesitter::Node node = walkStart; !node.IsNull(); node = node.Parent()) {
            if (isIndentCaptured(node) && node.StartRow() != lastRow) {
                ++level;
                lastRow = node.StartRow();
            }
        }
        return level;
    };

    std::optional<int> level;
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
            level = 0;
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
            level = walkStart.IsNull() ? std::optional<int>(0) : levelForWalkStart(walkStart, alignNode.StartByte());
        }
    }
    else {
        const treesitter::Node walkStart = resolveWalkStart(contentStart);
        level                            = walkStart.IsNull() ? std::optional<int>(0) : levelForWalkStart(walkStart, contentStart);
    }
    return level;
}

IndentFunction BuildIndentFunction(std::shared_ptr<treesitter::Parser> parser, std::shared_ptr<treesitter::Query> indentQuery,
                                   std::shared_ptr<treesitter::IncrementalParseCache> sharedParse, std::string modeName) {
    return [parser, indentQuery, sharedParse, modeName](std::string_view bufferText, std::size_t lineStart,
                                                        std::size_t lineEnd) -> std::optional<int> {
        const treesitter::Tree&  tree  = sharedParse->Update(*parser, bufferText);
        const std::optional<int> level = IndentLevelForLine(tree, bufferText, *indentQuery, lineStart, lineEnd);
        if (!level) {
            return std::nullopt;
        }
        return IndentColumnForLevel(*level, EffectiveIndentStyle(modeName));
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

        const std::string        text   = buffer.Text(); // see this function's own doc comment on this cost
        const std::optional<int> column = mode.indentColumn(text, lineStart, lineEnd);
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

} // namespace ned::editor
