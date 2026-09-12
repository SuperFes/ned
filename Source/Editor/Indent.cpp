#include "Indent.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include <limits>

#include "HugeStructuralWindow.h"
#include "ImprintIndent.h"
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

IndentCaptures IndentCapturesFromQuery(const treesitter::Tree& tree, std::string_view bufferText,
                                       const treesitter::Query& indentQuery) {
    // Keyed by the captured node's own stable identity (Node::Id()), NOT its
    // byte range -- a (startByte, endByte) pair can't disambiguate two
    // DIFFERENT nodes that happen to span the exact same bytes, which is a
    // real, not hypothetical, case (see Node::Id()'s own doc comment for
    // tree-sitter-python's "block" node coinciding byte-for-byte with its
    // own single statement when that statement is the block's only one).
    //
    // "aligned" (@aligned-paren-column-alignment follow-up): a container
    // captured "aligned" instead of "indent" -- see ResolveAlignedColumn/the
    // walk for what distinguishes it.
    //
    // "indent.body" (real-per-form-lisp-indent follow-up): a Lisp special
    // form (let/fn/defn/...) whose body indents a fixed 2 columns past the
    // form's own column, Emacs' lisp-indent-function convention, rather than
    // one style.width-multiple level or an @aligned first-argument column. A
    // node captured BOTH this and "aligned" by the same query (the common
    // case -- see janet-indents.scm/clojure-indents.scm's own comments) is
    // treated as indent.body, checked first in the walk.
    //
    // "align.barrier" (lambda-body-alignment follow-up): a brace-delimited
    // STATEMENT/DECLARATION body (C/C++'s compound_statement, JS's
    // statement_block, Java's block/class_body, ...) through which an OUTER
    // @aligned container's column alignment does not reach. Alignment is a
    // continuation-line rule ("foo(a,\n    b)"); a callable argument with a
    // real block body ("std::jthread t([fd] {") is not a continuation of the
    // argument list at all, and clang-format/prettier/gofmt all indent its
    // body from the statement's own column, not from the "(" it happens to
    // sit behind. Deliberately NOT applied to data literals
    // (initializer_list/object/array/literal_value): a multi-line literal
    // argument aligning its own body relative to the call's alignment column
    // is existing, documented, tested behavior (see the walk's own comment),
    // and only statement bodies change here. Carried by the query rather
    // than hardcoded node types, so a language whose nested @indent
    // containers genuinely SHOULD inherit an outer alignment (janet/clojure
    // -- a "[...]" inside a "(foo ...)" call) just never uses the capture.
    //
    // "dedent": every capture's own [startByte, endByte) range is kept, not
    // just whichever one starts the target line -- the end-of-buffer rescue
    // in the walk needs to recognize "the last real byte before this new
    // blank line is itself a closing delimiter" in general.
    IndentCaptures captures;
    if (tree.IsNull()) {
        return captures;
    }
    for (const treesitter::QueryCapture& capture : indentQuery.Captures(tree.RootNode(), bufferText)) {
        if (capture.name == "indent") {
            captures.indent.emplace(capture.nodeId, capture.startByte);
        }
        else if (capture.name == "aligned") {
            captures.aligned.insert(capture.nodeId);
        }
        else if (capture.name == "indent.body") {
            captures.body.insert(capture.nodeId);
        }
        else if (capture.name == "align.barrier") {
            captures.barrier.insert(capture.nodeId);
        }
        else if (capture.name == "indent.suppress") {
            captures.suppressed.insert(capture.nodeId);
        }
        else if (capture.name == "dedent") {
            captures.dedents.push_back(IndentCaptures::Dedent{capture.startByte, capture.endByte, capture.nodeId});
        }
    }
    return captures;
}

void AddImprintCaptures(IndentCaptures& captures, const treesitter::Tree& tree, std::string_view languageKey,
                        std::string_view bufferText) {
    if (tree.IsNull()) {
        return;
    }
    const imprint::ImprintIndentCaptures fromImprint =
        imprint::CollectIndentCaptures(tree.RootNode(), languageKey, bufferText);
    for (const imprint::ImprintContainer& container : fromImprint.containers) {
        if (!captures.suppressed.contains(container.nodeId)) {
            // emplace, not assignment: a node the query also captured keeps
            // the query's own interior start.
            captures.indent.emplace(container.nodeId, container.interiorStart);
        }
    }
    // A suppressed container's closer still dedents: the `}` of a top-level
    // namespace aligns with the `namespace` line whether or not its body
    // indented, which is what the hand-written query said too.
    for (const imprint::ImprintDedent& dedent : fromImprint.dedents) {
        captures.dedents.push_back(IndentCaptures::Dedent{dedent.startByte, dedent.endByte, dedent.nodeId});
    }
}

std::optional<IndentComputation> IndentLevelForLine(const treesitter::Tree& tree, std::string_view bufferText,
                                                    const treesitter::Query& indentQuery, std::size_t lineStart,
                                                    std::size_t lineEnd, const IndentStyle& style) {
    if (tree.IsNull()) {
        return std::nullopt;
    }
    return IndentLevelForLine(tree, bufferText, IndentCapturesFromQuery(tree, bufferText, indentQuery), lineStart,
                              lineEnd, style);
}

std::optional<IndentComputation> IndentLevelForLine(const treesitter::Tree& tree, std::string_view bufferText,
                                                    const IndentCaptures& captures, std::size_t lineStart,
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

    const void*                                      dedentNodeId = nullptr; // set only when a dedent capture starts this line
    std::vector<std::pair<std::size_t, std::size_t>> dedentRanges;
    dedentRanges.reserve(captures.dedents.size());
    for (const IndentCaptures::Dedent& dedent : captures.dedents) {
        dedentRanges.emplace_back(dedent.startByte, dedent.endByte);
        if (dedent.startByte == contentStart) {
            dedentNodeId = dedent.nodeId;
        }
    }

    const auto isIndentCaptured = [&captures](const treesitter::Node& node) { return captures.indent.contains(node.Id()); };
    // Whether `position` sits inside an "indent"-captured node's interior --
    // see IndentCaptures::indent. A node captured only "aligned"/"indent.body"
    // has no entry and its own start is its opener, so the answer is yes.
    const auto interiorContains = [&captures](const treesitter::Node& node, std::size_t position) {
        const auto found = captures.indent.find(node.Id());
        return found == captures.indent.end() || position >= found->second;
    };
    const auto isAlignedCaptured    = [&captures](const treesitter::Node& node) { return captures.aligned.contains(node.Id()); };
    const auto isBodyIndentCaptured = [&captures](const treesitter::Node& node) { return captures.body.contains(node.Id()); };
    const auto isBarrierCaptured    = [&captures](const treesitter::Node& node) { return captures.barrier.contains(node.Id()); };

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
        std::size_t lastRow       = selfOpensHere ? walkStart.StartRow() : kNoRow;
        int         level         = 0;
        // lambda-body-alignment follow-up: set once the walk has passed an
        // @align.barrier-captured body, after which no OUTER @aligned
        // ancestor may short-circuit with a column any more (it still counts
        // as an ordinary level). Set at the END of an iteration, not the
        // start, so a node captured both ways would still align itself --
        // a barrier blocks alignment from outside it, not its own.
        // Deliberately NOT gated on !opensAtPosition the way the @aligned/
        // @indent.body checks are: the dedent branch computes a closing
        // line ("    });") "as if for the opener's own line," which seeds
        // walkStart AT the barrier itself, and that line must still resolve
        // to the enclosing statement's own level rather than the call's
        // alignment column.
        bool crossedBarrier = false;
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
            if (isAlignedCaptured(node) && !opensAtPosition && !crossedBarrier) {
                if (const std::optional<int> column = ResolveAlignedColumn(node, bufferText, style.width)) {
                    return IndentComputation{IndentComputation::Kind::Column, *column + IndentColumnForLevel(level, style)};
                }
                // Unresolved (opener alone on its own line) -- falls through
                // to the plain @indent-shaped counting below, same as any
                // other captured container.
            }
            if ((isIndentCaptured(node) || isAlignedCaptured(node) || isBodyIndentCaptured(node)) &&
                node.StartRow() != lastRow && interiorContains(node, position)) {
                ++level;
                lastRow = node.StartRow();
            }
            if (isBarrierCaptured(node)) {
                crossedBarrier = true;
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
            result                           = walkStart.IsNull() ? std::optional<IndentComputation>(IndentComputation{IndentComputation::Kind::Level, 0})
                                                                  : computeForWalkStart(walkStart, alignNode.StartByte());
        }
    }
    else {
        const treesitter::Node walkStart = resolveWalkStart(contentStart);
        result                           = walkStart.IsNull() ? std::optional<IndentComputation>(IndentComputation{IndentComputation::Kind::Level, 0})
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
                                   std::shared_ptr<treesitter::IncrementalParseCache> sharedParse, std::string modeName,
                                   std::string languageKey) {
    return [parser, indentQuery, sharedParse, modeName, languageKey](std::string_view bufferText, std::size_t lineStart,
                                                                     std::size_t lineEnd) -> std::optional<int> {
        const treesitter::Tree& tree     = sharedParse->Update(*parser, bufferText);
        const IndentStyle       style    = EffectiveIndentStyle(modeName);
        IndentCaptures          captures = indentQuery ? IndentCapturesFromQuery(tree, bufferText, *indentQuery)
                                                       : IndentCaptures{};
        AddImprintCaptures(captures, tree, languageKey, bufferText);
        const std::optional<IndentComputation> result =
            IndentLevelForLine(tree, bufferText, captures, lineStart, lineEnd, style);
        if (!result) {
            return std::nullopt;
        }
        if (result->kind == IndentComputation::Kind::Column) {
            return result->value;
        }
        return IndentColumnForLevel(result->value, style);
    };
}

std::vector<std::pair<std::size_t, std::size_t>> VerbatimRanges(const Mode& mode, std::string_view bufferText) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (!mode.highlight) {
        return ranges;
    }
    for (const HighlightSpan& span : mode.highlight(bufferText, HighlightWindow{})) {
        if (span.syntaxClass != SyntaxClass::String || span.startByte >= span.endByte) {
            continue;
        }
        // Only a span that crosses a line boundary can contain a line start
        // strictly inside it, and a source file is mostly single-line
        // strings -- keeping the list to the ones that can matter is what
        // makes the per-line check a scan over a handful of entries.
        const std::size_t end = std::min(span.endByte, bufferText.size());
        if (bufferText.substr(span.startByte, end - span.startByte).find('\n') == std::string_view::npos) {
            continue;
        }
        ranges.emplace_back(span.startByte, end);
    }
    return ranges;
}

bool LineIsVerbatim(const std::vector<std::pair<std::size_t, std::size_t>>& ranges, std::size_t lineStart) {
    return std::any_of(ranges.begin(), ranges.end(), [lineStart](const std::pair<std::size_t, std::size_t>& range) {
        return range.first < lineStart && lineStart < range.second;
    });
}

std::optional<int> IndentColumnForLine(const Mode& mode, std::string_view bufferText, std::size_t lineStart,
                                       std::size_t                                             lineEnd,
                                       const std::vector<std::pair<std::size_t, std::size_t>>* ranges) {
    if (!mode.indentColumn) {
        return std::nullopt;
    }
    if (ranges != nullptr) {
        if (LineIsVerbatim(*ranges, lineStart)) {
            return std::nullopt;
        }
    }
    else if (LineIsVerbatim(VerbatimRanges(mode, bufferText), lineStart)) {
        return std::nullopt;
    }
    return mode.indentColumn(bufferText, lineStart, lineEnd);
}

int IndentColumnForLevel(int level, const IndentStyle& style) {
    return std::max(0, level) * std::max(1, style.width);
}

std::size_t LineIndentEnd(const text::ITextStorage& content, std::size_t lineStart) {
    const std::size_t length = content.ByteLength();
    std::size_t       offset = lineStart;
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
    const auto        width  = static_cast<std::size_t>(std::max(1, style.width));
    const std::size_t tabs   = columns / width;
    const std::size_t spaces = columns % width;
    std::string       result(tabs, '\t');
    result.append(spaces, ' ');
    return result;
}

std::ptrdiff_t SetLineIndent(text::Buffer& buffer, std::size_t lineStart, int column, const IndentStyle& style) {
    const std::size_t indentEnd = LineIndentEnd(buffer.Content(), lineStart);
    const std::size_t oldLength = indentEnd - lineStart;
    const std::string desired   = IndentString(column, style);
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
    const text::ITextStorage& initialContent         = buffer.Content();
    const bool                huge                   = initialContent.IsHuge();
    std::size_t               windowStartLine        = 0;
    std::size_t               windowEndLineExclusive = initialContent.LineCount();
    if (huge) {
        const std::size_t margin             = HugeStructuralWindowBytes();
        const std::size_t regionStartByte    = initialContent.LineToByteOffset(startLine);
        const std::size_t regionEndByte      = (endLineExclusive < initialContent.LineCount())
                                                   ? initialContent.LineToByteOffset(endLineExclusive)
                                                   : initialContent.ByteLength();
        const std::size_t rawWindowStartByte = regionStartByte > margin ? regionStartByte - margin : 0;
        windowStartLine                      = initialContent.ByteOffsetToLine(rawWindowStartByte);

        const std::size_t byteLength       = initialContent.ByteLength();
        const std::size_t rawWindowEndByte = (byteLength - regionEndByte > margin) ? regionEndByte + margin : byteLength;
        windowEndLineExclusive             = std::min(initialContent.ByteOffsetToLine(rawWindowEndByte) + 1, initialContent.LineCount());
    }

    // One highlight pass for the whole run, not one per line. Valid for
    // every line the loop still has to visit: it walks BOTTOM-TO-TOP and a
    // reindent only ever changes its own line's leading whitespace, so every
    // byte offset at or above the current line is exactly where it was when
    // these were measured.
    const std::vector<std::pair<std::size_t, std::size_t>> verbatim =
        huge ? VerbatimRanges(mode, initialContent.Substring(initialContent.LineToByteOffset(windowStartLine),
                                                             (windowEndLineExclusive < initialContent.LineCount()
                                                                  ? initialContent.LineToByteOffset(windowEndLineExclusive)
                                                                  : initialContent.ByteLength()) -
                                                                 initialContent.LineToByteOffset(windowStartLine)))
             : VerbatimRanges(mode, buffer.Text());

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
        std::size_t       lineEnd   = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
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
            const std::string windowText      = content.Substring(windowStartByte, windowEndByte - windowStartByte);
            column                            = IndentColumnForLine(mode, windowText, lineStart - windowStartByte, lineEnd - windowStartByte,
                                                                    &verbatim);
        }
        else {
            const std::string text = buffer.Text(); // see this function's own doc comment on this cost
            column                 = IndentColumnForLine(mode, text, lineStart, lineEnd, &verbatim);
        }
        if (!column) {
            continue;
        }
        // A line with no content of its own is left alone. Indenting one
        // writes pure trailing whitespace, and this is a BATCH reformat --
        // nobody's cursor is sitting there waiting to type.
        //
        // Found by the idempotence property test (Tests/FormatterPropertiesTest.cpp)
        // on its first run, against real Python: indent-buffer gave the final
        // empty line four spaces, so running it twice differed from running it
        // once. A formatter that does not converge makes every save churn the
        // file.
        //
        // Only the batch path, deliberately. The live path (newline,
        // indent-for-tab-command in Commands.cpp) calls SetLineIndent on an
        // empty line on purpose -- that is the cursor's own line, and putting
        // the caret at the right column is the entire point. Stripping the
        // whitespace already there is a different concern again, and belongs
        // to Editor/TrimOnSave.h rather than here.
        if (LineIndentEnd(buffer.Content(), lineStart) >= lineEnd) {
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
    const int tabWidth = TabWidth();

    buffer.BeginUndoGroup();
    std::size_t changed = 0;
    // Bottom-to-top, same reasoning as IndentRegion.
    for (std::size_t line = endLineExclusive; line-- > startLine;) {
        const text::ITextStorage& content = buffer.Content();
        if (line >= content.LineCount()) {
            continue;
        }
        const std::size_t lineStart     = content.LineToByteOffset(line);
        const std::size_t indentEnd     = LineIndentEnd(content, lineStart);
        const std::size_t currentColumn = buffer.VisualColumnForByteOffset(lineStart, indentEnd, static_cast<std::size_t>(tabWidth));
        const int         newColumn     = std::max(0, static_cast<int>(currentColumn) + deltaLevels * width);
        if (SetLineIndent(buffer, lineStart, newColumn, style) != 0) {
            ++changed;
        }
    }
    buffer.EndUndoGroup();
    return changed;
}

} // namespace ned::editor
