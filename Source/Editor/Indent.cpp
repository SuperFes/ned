#include "Indent.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <limits>

#include "HugeStructuralWindow.h"
#include "ImprintIndent.h"
#include "IndentRuleOverride.h"
#include "TabWidth.h"
#include "Grammar/MatchCache.h"
#include "Grammar/Node.h"

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
    std::optional<int> ResolveAlignedColumn(const grammar::Node& container, std::string_view bufferText, int width) {
        // Past the opener token itself when it is the first child: `#{`, `@[`.
        std::size_t delimiterEnd = container.StartByte() + 1;
        if (const grammar::Node opener = container.Child(0); !opener.IsNull() && !opener.IsNamed() &&
                                                              opener.StartByte() == container.StartByte()) {
            delimiterEnd = opener.EndByte();
        }
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

    // "aligned.args": a Lisp call, `(head arg ...`. Continuation lines line
    // up under the first argument when it shares the head's line, else under
    // the head itself -- Emacs's lisp-indent-function default and the
    // Clojure style guide's. nullopt when the head is not on the opener's line.
    std::optional<int> ResolveArgumentColumn(const grammar::Node& container, std::string_view bufferText, int width) {
        const std::size_t openerLineEnd = std::min(bufferText.find('\n', container.StartByte()), bufferText.size());
        // The children between the opener and the closer; the head may be a
        // keyword token of the form's own (Fennel's `(fn ...)`).
        std::vector<grammar::Node> members;
        container.ForEachChild([&](const grammar::Node& child) {
            if (!child.IsExtra()) {
                members.push_back(child);
            }
        });
        const bool hasOpener = !members.empty() && !members.front().IsNamed() && members.front().StartByte() == container.StartByte();
        const bool hasCloser = members.size() > 1 && !members.back().IsNamed() && members.back().EndByte() == container.EndByte();
        const std::size_t first = hasOpener ? 1 : 0;
        const std::size_t last  = members.size() - (hasCloser ? 1 : 0);
        const std::optional<std::size_t> head     = first < last ? std::optional(members[first].StartByte()) : std::nullopt;
        const std::optional<std::size_t> argument = first + 1 < last ? std::optional(members[first + 1].StartByte()) : std::nullopt;
        const std::optional<std::size_t> target = argument && *argument < openerLineEnd ? argument : head;
        if (!target || *target >= openerLineEnd) {
            return std::nullopt;
        }
        return VisualColumnInLine(bufferText, LineStartFor(bufferText, container.StartByte()), *target, width);
    }

    // real-per-form-lisp-indent follow-up: an @indent.body-captured
    // container's own visual column (where its opening "(" itself sits, NOT
    // its line's own leading indentation -- these differ whenever the form
    // isn't the first thing on its line, e.g. "(foo (let [x 1]" -- the let's
    // body indents relative to let's OWN column, matching real Emacs
    // lisp-indent-function behavior for a special form nested mid-line).
    int ContainerOwnColumn(const grammar::Node& container, std::string_view bufferText, int width) {
        return VisualColumnInLine(bufferText, LineStartFor(bufferText, container.StartByte()), container.StartByte(),
                                  width);
    }

    // A special form with `specials` distinguished arguments before its body
    // (Racket's `for/fold` accumulators and clauses, Common Lisp's
    // `multiple-value-bind` variables and values), Emacs's lisp-indent-specform:
    // a line starting one of those lines up under the first argument when it
    // shares the head's line, else sits four columns past the paren; the body
    // two past it.
    int SpecialFormColumn(const grammar::Node& form, std::string_view bufferText, std::size_t position, int specials, int width) {
        std::vector<grammar::Node> members;
        form.ForEachChild([&](const grammar::Node& child) {
            const bool delimiter = !child.IsNamed() && (child.StartByte() == form.StartByte() || child.EndByte() == form.EndByte());
            if (!child.IsExtra() && !delimiter) {
                members.push_back(child);
            }
        });
        const int own = ContainerOwnColumn(form, bufferText, width);
        // members[0] is the head; the line starts argument `index`.
        std::size_t index = 0;
        while (index < members.size() && members[index].EndByte() <= position) {
            ++index;
        }
        if (index == 0 || static_cast<int>(index) > specials) {
            return own + 2;
        }
        const std::size_t openerLineEnd = std::min(bufferText.find('\n', form.StartByte()), bufferText.size());
        if (members.size() > 1 && members[1].StartByte() < openerLineEnd && members[1].StartByte() < position) {
            return VisualColumnInLine(bufferText, LineStartFor(bufferText, form.StartByte()), members[1].StartByte(), width);
        }
        return own + 4;
    }

} // namespace

namespace {

    // indent-cache-by-byte-range follow-up: the actual name-based partition,
    // shared by IndentCapturesFromQuery (fed by a fresh, unwindowed
    // indentQuery.Matches() call) and BuildIndentFunction's closure (fed by
    // MatchCache::Reconcile, whose byte-shifted "kept" entries carry no live
    // node at all -- only QueryMatchCapture's own startByte/endByte/type).
    // Keyed by (startByte, endByte, type) -- IndentCaptures::NodeKey -- NOT a
    // raw Node::Id(), for exactly that reason; see IndentCaptures' own doc
    // comment for the one real collision case (tree-sitter-python's "block")
    // that key still resolves.
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
    bool DedentBefore(const IndentCaptures::Dedent& a, const IndentCaptures::Dedent& b) {
        return a.startByte < b.startByte;
    }

    // Levels a counted container adds: one, or a continuation's own count.
    int ContinuationWeight(const IndentCaptures& captures, const IndentCaptures::NodeKey& key, const IndentStyle& style) {
        if (!captures.continuation.contains(key)) {
            return 1;
        }
        const auto levels = captures.continuationLevels.find(key);
        return levels != captures.continuationLevels.end() ? levels->second : style.continuation;
    }

    IndentCaptures IndentCapturesFromMatches(const std::vector<grammar::QueryMatch>& matches, std::string_view bufferText) {
        IndentCaptures captures;
        for (const grammar::QueryMatch& match : matches) {
            for (const grammar::QueryMatchCapture& capture : match.captures) {
                const IndentCaptures::NodeKey key{capture.startByte, capture.endByte, capture.type};
                if (capture.name == "indent") {
                    captures.indent.emplace(key, capture.startByte);
                }
                else if (capture.name == "indent.headed" || capture.name == "indent.continuation") {
                    // The node's own first line is its header (OCaml's
                    // "f a" before continuation arguments): the interior
                    // starts on the next line. A zero-width one (a match arm
                    // the EOF pass supplied) has no header line yet -- the
                    // next line is where it goes -- so no interior at all.
                    const std::size_t newline = bufferText.find('\n', capture.startByte);
                    const std::size_t interior =
                        capture.startByte == capture.endByte ? std::string_view::npos
                        : newline == std::string_view::npos  ? bufferText.size()
                                                             : newline + 1;
                    captures.indent.emplace(key, interior);
                    if (capture.name == "indent.continuation") {
                        captures.continuation.insert(key);
                        if (const auto levels = match.setDirectives.find("indent.levels"); levels != match.setDirectives.end()) {
                            captures.continuationLevels.insert_or_assign(key, std::max(0, std::atoi(levels->second.c_str())));
                        }
                    }
                }
                else if (capture.name == "aligned") {
                    captures.aligned.insert(key);
                }
                else if (capture.name == "aligned.args") {
                    captures.aligned.insert(key);
                    captures.alignedArgs.insert(key);
                }
                else if (capture.name == "indent.body") {
                    captures.body.insert(key);
                    if (const auto specials = match.setDirectives.find("indent.specials"); specials != match.setDirectives.end()) {
                        captures.bodySpecials.insert_or_assign(key, std::max(0, std::atoi(specials->second.c_str())));
                    }
                }
                else if (capture.name == "align.barrier") {
                    captures.barrier.insert(key);
                }
                else if (capture.name == "indent.suppress") {
                    captures.suppressed.insert(key);
                }
                else if (capture.name == "dedent") {
                    captures.dedents.push_back(IndentCaptures::Dedent{capture.startByte, capture.endByte, capture.type});
                }
            }
            // `(try_expression "with" @indent.end) @indent.headed`: the
            // container's interior stops where that token starts, so the
            // token's line and everything after sit at the container's level.
            // Not a zero-width one: a `with` the EOF pass supplied hasn't
            // been written, so the body before it is still open.
            const auto end = std::find_if(match.captures.begin(), match.captures.end(), [](const grammar::QueryMatchCapture& capture) {
                return capture.name == "indent.end" && capture.startByte != capture.endByte;
            });
            if (end != match.captures.end()) {
                for (const grammar::QueryMatchCapture& capture : match.captures) {
                    if (capture.name == "indent" || capture.name == "indent.headed" || capture.name == "indent.continuation") {
                        captures.interiorEnd.emplace(IndentCaptures::NodeKey{capture.startByte, capture.endByte, capture.type},
                                                     end->startByte);
                    }
                }
            }
            // `(subprogram_body (handled_sequence_of_statements "exception"
            // @indent.begin)) @indent`: the interior starts on the line after
            // that token, which is how a section with no node of its own (Ada's
            // exception handlers) sits one level in.
            const auto begin = std::find_if(match.captures.begin(), match.captures.end(), [](const grammar::QueryMatchCapture& capture) {
                return capture.name == "indent.begin" && capture.startByte != capture.endByte;
            });
            if (begin != match.captures.end()) {
                const std::size_t newline  = bufferText.find('\n', begin->startByte);
                const std::size_t interior = newline == std::string_view::npos ? bufferText.size() : newline + 1;
                for (const grammar::QueryMatchCapture& capture : match.captures) {
                    if (capture.name == "indent" || capture.name == "indent.headed" || capture.name == "indent.continuation") {
                        captures.indent.insert_or_assign(IndentCaptures::NodeKey{capture.startByte, capture.endByte, capture.type},
                                                         interior);
                    }
                }
            }
        }
        // `(parenthesized_expression (binary_expression) @indent.suppress)`:
        // a continuation the query takes back where the parent says so --
        // rooted at the parent, so no ancestor-reading predicate is needed.
        for (const IndentCaptures::NodeKey& key : captures.suppressed) {
            if (captures.continuation.erase(key) > 0) {
                captures.continuationLevels.erase(key);
                captures.indent.erase(key);
            }
        }
        return captures;
    }

} // namespace

IndentCaptures IndentCapturesFromQuery(const grammar::Tree& tree, std::string_view bufferText,
                                       const grammar::QueryMatcher& indentQuery) {
    if (tree.IsNull()) {
        return {};
    }
    IndentCaptures captures       = IndentCapturesFromMatches(indentQuery.Matches(tree.RootNode(), bufferText), bufferText);
    captures.continuationDeclared = indentQuery.DeclaresCapture("indent.continuation");
    std::sort(captures.dedents.begin(), captures.dedents.end(), DedentBefore);
    return captures;
}

void AddImprintCaptures(IndentCaptures& captures, const grammar::Tree& tree, std::string_view languageKey,
                        std::string_view bufferText) {
    if (tree.IsNull()) {
        return;
    }
    const imprint::ImprintIndentCaptures fromImprint =
        imprint::CollectIndentCaptures(tree.RootNode(), languageKey, bufferText);
    for (const imprint::ImprintContainer& container : fromImprint.containers) {
        const IndentCaptures::NodeKey key{container.startByte, container.endByte, container.type};
        if (!captures.suppressed.contains(key)) {
            // emplace, not assignment: a node the query also captured keeps
            // the query's own interior start.
            captures.indent.emplace(key, container.interiorStart);
            if (container.interiorEnd) {
                captures.interiorEnd.emplace(key, *container.interiorEnd);
            }
            if (!container.gaps.empty()) {
                captures.interiorGaps.emplace(key, container.gaps);
            }
            if (container.anchorsAtOwnColumn) {
                captures.columnAnchored.insert(key);
            }
        }
    }
    // A suppressed container's closer still dedents: the `}` of a top-level
    // namespace aligns with the `namespace` line whether or not its body
    // indented, which is what the hand-written query said too.
    for (const imprint::ImprintDedent& dedent : fromImprint.dedents) {
        captures.dedents.push_back(IndentCaptures::Dedent{dedent.startByte, dedent.endByte, dedent.type});
    }
}

std::optional<IndentComputation> IndentLevelForLine(const grammar::Tree& tree, std::string_view bufferText,
                                                    const grammar::QueryMatcher& indentQuery, std::size_t lineStart,
                                                    std::size_t lineEnd, const IndentStyle& style) {
    if (tree.IsNull()) {
        return std::nullopt;
    }
    return IndentLevelForLine(tree, bufferText, IndentCapturesFromQuery(tree, bufferText, indentQuery), lineStart,
                              lineEnd, style);
}

std::optional<IndentComputation> IndentLevelForLine(const grammar::Tree& tree, std::string_view bufferText,
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

    // indent-cache-by-byte-range follow-up: a node's key for every lookup
    // below -- see IndentCaptures' own doc comment for why (startByte,
    // endByte, type) replaced a raw Node::Id().
    const auto keyOf = [](const grammar::Node& node) {
        return IndentCaptures::NodeKey{node.StartByte(), node.EndByte(), node.Type()};
    };

    std::optional<IndentCaptures::NodeKey>           dedentKey; // set only when a dedent capture starts this line
    std::vector<std::pair<std::size_t, std::size_t>> dedentRanges;
    dedentRanges.reserve(captures.dedents.size());
    for (const IndentCaptures::Dedent& dedent : captures.dedents) {
        dedentRanges.emplace_back(dedent.startByte, dedent.endByte);
        if (dedent.startByte == contentStart) {
            dedentKey = IndentCaptures::NodeKey{dedent.startByte, dedent.endByte, dedent.type};
        }
    }

    // The closer starting at `position`, if any. The captures are built once
    // per text and sorted there (SortDedents); a hand-built set may not be.
    std::vector<IndentCaptures::Dedent> sortedCopy;
    if (!std::is_sorted(captures.dedents.begin(), captures.dedents.end(), DedentBefore)) {
        sortedCopy = captures.dedents;
        std::sort(sortedCopy.begin(), sortedCopy.end(), DedentBefore);
    }
    const std::vector<IndentCaptures::Dedent>& sortedDedents    = sortedCopy.empty() ? captures.dedents : sortedCopy;
    const auto                                 dedentStartingAt = [&sortedDedents](std::size_t position) -> std::optional<IndentCaptures::NodeKey> {
        const auto found = std::lower_bound(sortedDedents.begin(), sortedDedents.end(), position,
                                            [](const IndentCaptures::Dedent& dedent, std::size_t at) { return dedent.startByte < at; });
        if (found == sortedDedents.end() || found->startByte != position) {
            return std::nullopt;
        }
        return IndentCaptures::NodeKey{found->startByte, found->endByte, found->type};
    };

    const auto isIndentCaptured = [&captures, &keyOf](const grammar::Node& node) { return captures.indent.contains(keyOf(node)); };
    // Whether `position` sits inside an "indent"-captured node's interior --
    // see IndentCaptures::indent. A node captured only "aligned"/"indent.body"
    // has no entry and its own start is its opener, so the answer is yes.
    // for-loop-header-imprint follow-up: also rejects a position at or past
    // IndentCaptures::interiorEnd's own cap, when one is set -- see that
    // field's own doc comment for why a for-loop's trailing body needs this
    // and almost nothing else does.
    const auto interiorContains = [&captures, &keyOf](const grammar::Node& node, std::size_t position) {
        const IndentCaptures::NodeKey key = keyOf(node);
        const auto                    found = captures.indent.find(key);
        if (found != captures.indent.end() && position < found->second) {
            return false;
        }
        const auto cappedEnd = captures.interiorEnd.find(key);
        if (cappedEnd != captures.interiorEnd.end() && position >= cappedEnd->second) {
            return false;
        }
        if (const auto gaps = captures.interiorGaps.find(key); gaps != captures.interiorGaps.end()) {
            return std::none_of(gaps->second.begin(), gaps->second.end(), [position](const auto& gap) {
                return gap.first <= position && position < gap.second;
            });
        }
        return true;
    };
    const auto isAlignedCaptured    = [&captures, &keyOf](const grammar::Node& node) { return captures.aligned.contains(keyOf(node)); };
    const auto isBodyIndentCaptured = [&captures, &keyOf](const grammar::Node& node) { return captures.body.contains(keyOf(node)); };
    const auto isBarrierCaptured    = [&captures, &keyOf](const grammar::Node& node) { return captures.barrier.contains(keyOf(node)); };
    const auto isColumnAnchored     = [&captures, &keyOf](const grammar::Node& node) { return captures.columnAnchored.contains(keyOf(node)); };

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
        grammar::Node node = tree.RootNode().NamedDescendantForByteRange(position, position);
        // The lookup stops at a zero-width token at `position` (Scala 3's
        // `_indent` opening an indented_block, Fortran's statement terminator
        // before a `case`), answering its parent rather than the code there;
        // keep descending.
        for (bool descended = !node.IsNull(); descended;) {
            descended = false;
            node.ForEachChild([&](const grammar::Node& child) {
                if (!descended && child.IsNamed() && child.StartByte() <= position && position < child.EndByte()) {
                    node      = child;
                    descended = true;
                }
            });
        }
        if (!node.IsNull() && (node.Type() == "start_tag" || node.Type() == "STag")) {
            const grammar::Node parent = node.Parent();
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
    // A closer's line aligns with its opener's: the dedent branch below, and
    // the walk's hand-off for a container opened on such a line.
    std::function<std::optional<IndentComputation>(const IndentCaptures::NodeKey&)> computeForDedent;

    // The container a closer closes: its parent.
    const auto closedBy = [&](const IndentCaptures::NodeKey& key) -> std::optional<grammar::Node> {
        // The dedent-captured node itself may be anonymous (a literal "}")
        // or named (HTML/XML's "end_tag" -- a whole "</div>" node, not a
        // single token) -- DescendantForByteRange (unnamed-inclusive) at
        // the closer's start finds whatever is truly SMALLEST at that position,
        // which for a named capture can be one of ITS OWN anonymous
        // children (e.g. end_tag's own leading "</" token) rather than the
        // captured node itself. Walk up from there by identity (keyOf, not
        // a byte-range/IsNamed() guess alone -- see IndentCaptures' own doc
        // comment) until the node that identity-matches the actual capture
        // is found -- correct regardless of which shape the query captured.
        // By the capture's own range, not the point: a zero-width token just
        // before it (Fortran's statement terminator) would otherwise stop the
        // descent at the parent.
        grammar::Node dedentNode = tree.RootNode().DescendantForByteRange(key.startByte, key.endByte);
        if (!dedentNode.IsNull() && !(keyOf(dedentNode) == key)) {
            std::vector<grammar::Node> chain;
            dedentNode.AncestorChain(chain);
            const auto found = std::find_if(chain.begin(), chain.end(),
                                            [&](const grammar::Node& ancestor) { return keyOf(ancestor) == key; });
            dedentNode       = found != chain.end() ? *found : grammar::Node(parse::NodeNull());
        }
        if (dedentNode.IsNull()) {
            return std::nullopt;
        }
        return dedentNode.Parent();
    };

    // A chain whose root is itself a block written across lines (`VStack {`
    // ... `}` then `.padding()`, `foo(|a| {` ... `})` then `.bar()`) goes on
    // at the root's level: swift-format and rustfmt only indent a link when
    // the line before it doesn't close something the chain's first line
    // opened.
    const auto continuesAfterMultiLineRoot = [&](const grammar::Node& node, std::size_t position) {
        if (!captures.continuation.contains(keyOf(node))) {
            return false;
        }
        const std::size_t rowStart = LineStartFor(bufferText, position);
        if (rowStart == 0) {
            return false;
        }
        const std::size_t previousEnd = bufferText.find_last_not_of(" \t\r\n", rowStart - 1);
        if (previousEnd == std::string_view::npos) {
            return false;
        }
        const std::size_t                            previousStart = LineStartFor(bufferText, previousEnd);
        const std::optional<IndentCaptures::NodeKey> closer        = dedentStartingAt(FirstNonBlankByte(bufferText, previousStart, previousEnd + 1));
        if (!closer) {
            return false;
        }
        const std::optional<grammar::Node> closed = closedBy(*closer);
        return closed && !closed->IsNull() && closed->StartRow() == node.StartRow();
    };

    const auto computeForWalkStart = [&](const grammar::Node& walkStart, std::size_t position) -> IndentComputation {
        // An @indent.headed container's interior starts on a later line, so
        // it never counts for its own first row and needs no seed; seeding
        // it would hide a same-row container that does count (Scala's
        // indented_cases, starting on a headed case_clause's row).
        const auto  found         = captures.indent.find(keyOf(walkStart));
        const bool  headed        = found != captures.indent.end() && bufferText.find('\n', walkStart.StartByte()) < found->second;
        const bool  selfOpensHere = (isIndentCaptured(walkStart) || isAlignedCaptured(walkStart) ||
                                     isBodyIndentCaptured(walkStart)) &&
                                    walkStart.StartByte() == position && !headed;
        std::size_t lastRow       = selfOpensHere ? walkStart.StartRow() : kNoRow;
        // Two containers on one row count once only when both open on it
        // (`foo(bar(`, `{"a": [`). One whose interior already holds the row's
        // content -- the row is its first line rather than its opener's --
        // opened earlier, so it still counts beneath one opening after that
        // content: a body's first statement `x = foo(`, whose body starts at
        // `x`.
        const auto  interiorStartOf = [&](const grammar::Node& node) {
            const auto found = captures.indent.find(keyOf(node));
            return found != captures.indent.end() ? found->second : node.StartByte() + 1;
        };
        const auto rowContentStartOf = [&](const grammar::Node& node) {
            const std::size_t lineStart = LineStartFor(bufferText, node.StartByte());
            return FirstNonBlankByte(bufferText, lineStart, std::min(bufferText.find('\n', lineStart), bufferText.size()));
        };
        std::size_t lastInteriorStart   = selfOpensHere ? interiorStartOf(walkStart) : 0;
        std::size_t lastRowContentStart = selfOpensHere ? rowContentStartOf(walkStart) : 0;
        const auto  opensBeforeLastRow  = [&](const grammar::Node& node) {
            return lastRow != kNoRow && interiorStartOf(node) <= lastRowContentStart && lastInteriorStart > lastRowContentStart &&
                   !(keyOf(resolveWalkStart(lastRowContentStart)) == keyOf(node));
        };
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
        // Set once the walk is inside a statement body whose opener starts
        // its own line (an Allman `{` after `$f = function ()`): that body is
        // a block of its own, not a continuation of the expression above it.
        bool insideOwnLineBody = false;
        // AncestorChain follow-up: one root-to-self descent for the whole
        // climb, instead of walkStart.Parent() re-descending from the root
        // on every step.
        std::vector<grammar::Node> chain;
        walkStart.AncestorChain(chain);
        chain.insert(chain.begin(), walkStart);
        // A container opened on a line that begins with a closer (`} else {`,
        // `}, function () {`) sits one level past that line, whose own
        // indent is its closer's -- the opener's line -- and not the sum of
        // whatever else encloses it (a `let x = if a {` continuation would
        // otherwise count again for the else branch).
        const auto handOffFromCloserRow = [&](const grammar::Node& node) -> std::optional<IndentComputation> {
            if (lastRow == kNoRow || node.StartRow() >= lastRow) {
                return std::nullopt;
            }
            const std::optional<IndentCaptures::NodeKey> closer = dedentStartingAt(lastRowContentStart);
            if (!closer) {
                return std::nullopt;
            }
            const std::optional<IndentComputation> row = computeForDedent(*closer);
            if (!row) {
                return std::nullopt;
            }
            return row->kind == IndentComputation::Kind::Level
                       ? IndentComputation{IndentComputation::Kind::Level, row->value + level}
                       : IndentComputation{IndentComputation::Kind::Column, row->value + IndentColumnForLevel(level, style)};
        };
        for (const grammar::Node& node : chain) {
            const bool opensAtPosition = node.StartByte() == position;
            if ((isIndentCaptured(node) || isAlignedCaptured(node) || isBodyIndentCaptured(node) || isColumnAnchored(node)) &&
                !opensAtPosition && interiorContains(node, position)) {
                if (const std::optional<IndentComputation> handedOff = handOffFromCloserRow(node)) {
                    return *handedOff;
                }
            }
            if (isBodyIndentCaptured(node) && !opensAtPosition) {
                // Unlike @aligned, a special form's body indent never falls
                // back -- it's always 2 columns past the form's own column,
                // regardless of what (if anything) follows the opener on
                // its own line.
                const auto specials = captures.bodySpecials.find(keyOf(node));
                const int  column   = specials != captures.bodySpecials.end()
                                          ? SpecialFormColumn(node, bufferText, position, specials->second, style.width)
                                          : ContainerOwnColumn(node, bufferText, style.width) + 2;
                return IndentComputation{IndentComputation::Kind::Column, column + IndentColumnForLevel(level, style)};
            }
            // A mid-line indentation body (YAML's `- key: v`): its interior
            // sits under its own first member wherever the marker before it
            // put that member, so the answer is a column, not a level. Ahead
            // of @aligned because this body's opener is empty -- aligning
            // "past the opener" would land one column inside its own first
            // member. Barrier-gated for @aligned's own reason.
            if (isColumnAnchored(node) && !opensAtPosition && !crossedBarrier && interiorContains(node, position)) {
                const int column = ContainerOwnColumn(node, bufferText, style.width);
                return IndentComputation{IndentComputation::Kind::Column, column + IndentColumnForLevel(level, style)};
            }
            if (isAlignedCaptured(node) && !opensAtPosition && !crossedBarrier) {
                if (const std::optional<int> column = captures.alignedArgs.contains(keyOf(node))
                                                          ? ResolveArgumentColumn(node, bufferText, style.width)
                                                          : ResolveAlignedColumn(node, bufferText, style.width)) {
                    return IndentComputation{IndentComputation::Kind::Column, *column + IndentColumnForLevel(level, style)};
                }
                // Unresolved (opener alone on its own line) -- falls through
                // to the plain @indent-shaped counting below, same as any
                // other captured container.
            }
            if ((isIndentCaptured(node) || isAlignedCaptured(node) || isBodyIndentCaptured(node)) &&
                (node.StartRow() != lastRow || opensBeforeLastRow(node)) && interiorContains(node, position) &&
                !(insideOwnLineBody && captures.continuation.contains(keyOf(node))) &&
                !continuesAfterMultiLineRoot(node, position)) {
                level += ContinuationWeight(captures, keyOf(node), style);
                lastRow             = node.StartRow();
                lastInteriorStart   = interiorStartOf(node);
                lastRowContentStart = rowContentStartOf(node);
            }
            if (isBarrierCaptured(node)) {
                crossedBarrier = true;
                insideOwnLineBody = insideOwnLineBody || node.StartByte() == rowContentStartOf(node);
            }
        }
        return IndentComputation{IndentComputation::Kind::Level, level};
    };

    computeForDedent = [&](const IndentCaptures::NodeKey& key) -> std::optional<IndentComputation> {
        const std::optional<grammar::Node> closed = closedBy(key);
        if (!closed) {
            return std::nullopt;
        }
        const grammar::Node alignNode = *closed;
        if (alignNode.IsNull()) {
            return IndentComputation{IndentComputation::Kind::Level, 0};
        }
        // Compute indent AS IF for alignNode's own opening line -- this
        // is what makes a closing delimiter align with its opener's own
        // line rather than one level deeper, whether or not alignNode
        // is itself @indent-captured (a bracket/brace container is;
        // Python's if_statement -- elif/else/except/finally's own
        // alignNode -- is not, and still resolves correctly via the
        // same self-exclusion rule rather than assuming captured-ness).
        const grammar::Node walkStart = resolveWalkStart(alignNode.StartByte());
        return walkStart.IsNull() ? IndentComputation{IndentComputation::Kind::Level, 0}
                                  : computeForWalkStart(walkStart, alignNode.StartByte());
    };

    std::optional<IndentComputation> result;
    if (dedentKey.has_value()) {
        result = computeForDedent(*dedentKey);
        if (!result) {
            return std::nullopt;
        }
    }
    else {
        const grammar::Node walkStart = resolveWalkStart(contentStart);
        result                           = walkStart.IsNull() ? std::optional<IndentComputation>(IndentComputation{IndentComputation::Kind::Level, 0})
                                                              : computeForWalkStart(walkStart, contentStart);

        // A header whose body is still empty ("def f():", "proc f() =" and
        // Enter): the parser recovers it with a zero-width body right after
        // the header, so no blank line below can ever fall inside it. The
        // line opens that body: one level past the header. Blank lines only
        // -- real content the parse left outside the body stays outside it.
        const auto openedEmptyBody = [&]() -> std::optional<IndentComputation> {
            std::size_t   searchEnd = lineStart;
            std::size_t   anchor    = 0;
            grammar::Node header(parse::NodeNull());
            do {
                if (searchEnd == 0) {
                    return std::nullopt;
                }
                anchor = bufferText.find_last_not_of(" \t\n\r", searchEnd - 1);
                if (anchor == std::string_view::npos) {
                    return std::nullopt;
                }
                header = resolveWalkStart(anchor);
                if (header.IsNull()) {
                    return std::nullopt;
                }
                searchEnd = header.StartByte(); // a trailing comment: look past it
            }
            while (header.IsExtra());

            bool opens = false;
            header.ForEachChild([&](const grammar::Node& child) {
                opens = opens || (child.StartByte() == child.EndByte() && child.StartByte() > anchor &&
                                  child.StartByte() <= lineStart && isIndentCaptured(child) && interiorContains(child, lineStart));
            });
            // Or the header is the whole container so far (OCaml's `let f x
            // =`, an @indent.headed let_binding): it ends at the anchor, and
            // its interior would begin at this line.
            if (!opens) {
                std::vector<grammar::Node> chain;
                header.AncestorChain(chain);
                chain.insert(chain.begin(), header);
                // A continuation that ends there is finished, not empty.
                opens = std::any_of(chain.begin(), chain.end(), [&](const grammar::Node& node) {
                    return node.EndByte() == anchor + 1 && isIndentCaptured(node) && !captures.continuation.contains(keyOf(node)) &&
                           !interiorContains(node, anchor) && interiorContains(node, lineStart);
                });
            }
            if (!opens) {
                return std::nullopt;
            }
            const IndentComputation own = computeForWalkStart(header, anchor);
            return own.kind == IndentComputation::Kind::Level
                       ? IndentComputation{IndentComputation::Kind::Level, own.value + 1}
                       : IndentComputation{IndentComputation::Kind::Column, own.value + IndentColumnForLevel(1, style)};
        };
        const std::optional<IndentComputation> openedBody =
            contentStart == lineEnd ? openedEmptyBody() : std::nullopt;
        if (openedBody) {
            result = openedBody;
        }

        // Enter after an unfinished statement (`x = a +`, a chain still
        // without its `;`): the parse has no node for it yet -- an ERROR
        // ending at the last token above, or a zero-width token the parser
        // supplied right after it -- so the line continues the construct:
        // a continuation step past the line it begins on. A supplied closer
        // of a body (a `}` the file hasn't got yet) opens that body instead,
        // which the walk already answers.
        const auto continuesUnfinished = [&]() -> std::optional<IndentComputation> {
            if (!captures.continuationDeclared || lineStart == 0) {
                return std::nullopt;
            }
            const std::size_t anchor = bufferText.find_last_not_of(" \t\n\r", lineStart - 1);
            if (anchor == std::string_view::npos) {
                return std::nullopt;
            }
            grammar::Node leaf = tree.RootNode().DescendantForByteRange(anchor, anchor + 1);
            if (leaf.IsNull() || leaf.IsExtra()) {
                return std::nullopt;
            }
            // A zero-width token the parser supplied ends the construct it
            // completes, placed right after the anchor or at the next real
            // token -- so only an ancestor ending in that gap can hold one.
            const std::size_t          next = std::min(bufferText.find_first_not_of(" \t\n\r", anchor + 1), bufferText.size());
            std::vector<grammar::Node> chain;
            leaf.AncestorChain(chain);
            chain.insert(chain.begin(), leaf);
            std::optional<grammar::Node> construct;
            for (const grammar::Node& node : chain) {
                if (node.Type() == "ERROR" && node.EndByte() == anchor + 1) {
                    construct = node;
                    break;
                }
                if (node.EndByte() <= anchor || node.EndByte() > next || node.ChildCount() == 0) {
                    continue;
                }
                const grammar::Node last = node.Child(node.ChildCount() - 1);
                if (last.StartByte() == last.EndByte() && last.StartByte() > anchor) {
                    if (!isIndentCaptured(node) || captures.continuation.contains(keyOf(node))) {
                        construct = node;
                    }
                    break;
                }
            }
            if (!construct) {
                return std::nullopt;
            }
            const std::size_t constructLine = LineStartFor(bufferText, construct->StartByte());
            const std::size_t firstByte =
                FirstNonBlankByte(bufferText, constructLine, std::min(bufferText.find('\n', constructLine), bufferText.size()));
            std::optional<IndentComputation> base;
            if (const std::optional<IndentCaptures::NodeKey> closer = dedentStartingAt(firstByte)) {
                base = computeForDedent(*closer);
            }
            else if (const grammar::Node walkStart = resolveWalkStart(firstByte); !walkStart.IsNull()) {
                base = computeForWalkStart(walkStart, firstByte);
            }
            if (!base) {
                return std::nullopt;
            }
            return base->kind == IndentComputation::Kind::Level
                       ? IndentComputation{IndentComputation::Kind::Level, base->value + style.continuation}
                       : IndentComputation{IndentComputation::Kind::Column,
                                           base->value + IndentColumnForLevel(style.continuation, style)};
        };
        const std::optional<IndentComputation> continued =
            (contentStart == lineEnd && !openedBody) ? continuesUnfinished() : std::nullopt;
        if (continued) {
            result = continued;
        }

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
        if (!openedBody && !continued && lineStart == lineEnd && contentStart == bufferText.size() && contentStart > 0 &&
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
                const grammar::Node rescueWalkStart = resolveWalkStart(rescuePosition);
                if (!rescueWalkStart.IsNull()) {
                    result = computeForWalkStart(rescueWalkStart, rescuePosition);
                }
            }
        }

        // comment-as-first-line-of-a-body follow-up: an @extra node (a
        // comment) isn't really part of a grammar's structural nesting, and
        // a comment as the very first line of a body commonly attaches as a
        // SIBLING directly before the body it precedes rather than inside
        // it -- confirmed via a real parse dump, not assumed: Python's own
        // "comment" sits directly under "if_statement"/"function_definition",
        // one level short of that body's own captured "block", because the
        // block's range (no opening delimiter of its own to capture) starts
        // at its first real statement, one byte past where the comment
        // ends. Walking the comment's own ancestors therefore under-counts
        // by however many containers sit between it and the real content
        // that follows -- not just to 0, so this can't share the rescue
        // above's Level==0 gate; a comment nested two containers deep still
        // resolves one level short, never zero. Re-resolve AS IF for the
        // next non-blank, non-comment line's own position instead -- the
        // comment then takes whatever level the code it precedes takes, the
        // same convention Emacs/vim comment-reindent already follow.
        // Gated on walkStart.StartByte() == contentStart so a multi-line
        // block comment's own CONTINUATION lines (C-style /* ... */; never
        // true for Python's line-only comments) are left exactly as the
        // ordinary walk above already resolves them -- only a comment's own
        // first line is a candidate.
        if (!walkStart.IsNull() && walkStart.IsExtra() && walkStart.StartByte() == contentStart) {
            std::size_t scan = lineEnd;
            while (scan < bufferText.size()) {
                const std::size_t lineEndForScan = bufferText.find('\n', scan);
                const std::size_t candidateEnd   = lineEndForScan == std::string_view::npos ? bufferText.size() : lineEndForScan;
                const std::size_t candidateStart = FirstNonBlankByte(bufferText, scan, candidateEnd);
                if (candidateStart >= candidateEnd) {
                    scan = candidateEnd + 1; // a blank line -- keep scanning forward
                    continue;
                }
                const grammar::Node followingWalkStart = resolveWalkStart(candidateStart);
                if (followingWalkStart.IsNull()) {
                    break;
                }
                if (followingWalkStart.IsExtra() && followingWalkStart.StartByte() == candidateStart) {
                    scan = candidateEnd + 1; // another comment line -- keep looking past it
                    continue;
                }
                result = computeForWalkStart(followingWalkStart, candidateStart);
                break;
            }
        }
    }
    return result;
}

IndentFunction BuildIndentFunction(std::shared_ptr<grammar::Parser> parser, std::shared_ptr<grammar::QueryMatcher> indentQuery,
                                   std::shared_ptr<grammar::IncrementalParseCache> sharedParse, std::string modeName,
                                   std::string languageKey) {
    // indent-cache-by-byte-range follow-up: only built (and only consulted
    // below) when there's a real query to reconcile against -- an
    // imprint-only language (indentQuery null) has nothing for MatchCache to
    // do. Shares sharedParse->LastEdit() the same way Mode.cpp's symbolKind
    // closure does, so the diff is computed once per generation regardless
    // of how many capabilities ask for it.
    const auto indentMatchCache = indentQuery ? std::make_shared<grammar::MatchCache>() : nullptr;
    // indent-region-batch-perf follow-up: caches the fully merged (query +
    // imprint) captures set, reused wholesale whenever bufferText is
    // byte-identical to the text this SAME closure last computed captures
    // for. Without this, a caller invoking this closure many times over the
    // SAME frozen text -- IndentRegion's own batch loop below, once it
    // stopped re-materializing buffer.Text() every iteration -- paid a full
    // AddImprintCaptures tree walk (O(document size), no incremental
    // reconciliation at all, unlike the query side's MatchCache) on EVERY
    // call, turning an O(n) batch reindent into O(n * linesInRange):
    // measured at 104s on this project's own ~3000-line main.cpp via
    // format-buffer's new Native fallback.
    //
    // Deliberately compares bufferText directly rather than trusting
    // sharedParse->LastEdit() (nullopt there does NOT mean "unchanged since
    // this closure's own last call" -- it means "unchanged since the last
    // call to sharedParse->Update by ANY caller", and sharedParse is shared
    // across a Mode's other capabilities too, e.g. mode.highlight, which
    // IndentColumnForLine's own VerbatimRanges call invokes with the exact
    // same bufferText just before calling into this closure. That interleaving
    // made LastEdit() falsely report "no edit" here even when the document
    // genuinely changed since this closure's own previous invocation --
    // caught by ImprintIndentTest.cpp's "indented relative to nothing is a
    // root" case, which calls this closure three times over three different
    // documents on one shared Mode).
    struct CapturesCache {
        bool           hasResult = false;
        std::string    lastText;
        IndentCaptures result;
    };
    const auto capturesCache = std::make_shared<CapturesCache>();
    return [parser, indentQuery, sharedParse, indentMatchCache, capturesCache, modeName,
            languageKey](std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd,
                         const IndentOverride& bufferIndent) -> std::optional<int> {
        const grammar::Tree& tree  = sharedParse->Update(*parser, bufferText);
        const IndentStyle    style = bufferIndent.AppliedTo(EffectiveIndentStyle(modeName));
        if (!capturesCache->hasResult || capturesCache->lastText != bufferText) {
            // Tree::RootNode()'s own precondition is !IsNull() -- guard here
            // rather than rely on Reconcile/Matches tolerating a null root,
            // matching what IndentCapturesFromQuery already checked internally
            // before this closure started calling MatchCache::Reconcile directly.
            IndentCaptures captures =
                (indentQuery && !tree.IsNull())
                    ? IndentCapturesFromMatches(indentMatchCache->Reconcile(*indentQuery, tree, bufferText,
                                                                            sharedParse->LastEdit()),
                                                bufferText)
                    : IndentCaptures{};
            AddImprintCaptures(captures, tree, languageKey, bufferText);
            captures.continuationDeclared = indentQuery && indentQuery->DeclaresCapture("indent.continuation");
            std::sort(captures.dedents.begin(), captures.dedents.end(), DedentBefore);
            capturesCache->result    = std::move(captures);
            capturesCache->lastText.assign(bufferText);
            capturesCache->hasResult = true;
        }
        const std::optional<IndentComputation> result =
            IndentLevelForLine(tree, bufferText, capturesCache->result, lineStart, lineEnd, style);
        if (!result) {
            return std::nullopt;
        }
        const int column =
            (result->kind == IndentComputation::Kind::Column) ? result->value : IndentColumnForLevel(result->value, style);

        // ned/set-indent-rule follow-up: an additive final step, applied
        // fresh on every call (never cached alongside capturesCache above,
        // so a live config change takes effect immediately) -- see
        // IndentRuleOverride.h's own header comment for why this resolves
        // the line's own smallest named node rather than threading anything
        // into IndentLevelForLine's existing walk. !tree.IsNull() already
        // guaranteed above (IndentLevelForLine returns nullopt otherwise, an
        // early return already taken).
        const std::size_t     contentStart = FirstNonBlankByte(bufferText, lineStart, lineEnd);
        const grammar::Node   leadingNode  = tree.RootNode().NamedDescendantForByteRange(contentStart, contentStart);
        if (!leadingNode.IsNull()) {
            const IndentRuleValue rule = IndentRuleFor(leadingNode.Type(), languageKey);
            if (rule.policy == IndentRulePolicy::Absolute) {
                return rule.value;
            }
            if (rule.policy == IndentRulePolicy::Offset) {
                return column + rule.value;
            }
        }
        return column;
    };
}

std::vector<std::pair<std::size_t, std::size_t>> VerbatimRanges(const Mode& mode, std::string_view bufferText,
                                                                 HighlightWindow window) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (!mode.highlight) {
        return ranges;
    }
    for (const HighlightSpan& span : mode.highlight(bufferText, window)) {
        if ((span.syntaxClass != SyntaxClass::String && span.syntaxClass != SyntaxClass::Comment) ||
            span.startByte >= span.endByte) {
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
    return RangeContainingLine(ranges, lineStart) != nullptr;
}

const std::pair<std::size_t, std::size_t>* RangeContainingLine(
    const std::vector<std::pair<std::size_t, std::size_t>>& ranges, std::size_t lineStart) {
    const auto it = std::ranges::find_if(ranges, [lineStart](const std::pair<std::size_t, std::size_t>& range) {
        return range.first < lineStart && lineStart < range.second;
    });
    return it == ranges.end() ? nullptr : &*it;
}

namespace {

    // A block comment's continuation line: the opener's own column plus one,
    // so a leading `*` lands under the `*` of `/*`. nullopt for anything
    // else, which IndentColumnForLine reads as "leave this line alone" --
    // see Indent.h's own verbatim-regions comment for why that is the answer
    // for an unornamented comment interior rather than a fallback.
    std::optional<int> BlockCommentContinuationColumn(std::string_view                           bufferText,
                                                      const std::pair<std::size_t, std::size_t>& range,
                                                      std::size_t lineStart, std::size_t lineEnd) {
        if (bufferText.substr(range.first, 2) != "/*") {
            return std::nullopt; // a string, or a line-comment run
        }
        const std::size_t contentStart = FirstNonBlankByte(bufferText, lineStart, lineEnd);
        if (contentStart >= bufferText.size() || bufferText[contentStart] != '*') {
            return std::nullopt;
        }
        // The opener's column, counted in bytes from its own line start --
        // the same plain-byte measure every other column in this engine uses.
        const std::size_t openerLineStart = bufferText.rfind('\n', range.first);
        const std::size_t column          = range.first - (openerLineStart == std::string_view::npos ? 0 : openerLineStart + 1);
        return static_cast<int>(column) + 1;
    }

    // Whether the line before `lineStart` ends in `marker` that isn't itself
    // escaped: an odd run of it, since a doubled one is a literal.
    bool ContinuedFromPreviousLine(std::string_view bufferText, std::size_t lineStart, std::string_view marker) {
        if (marker.empty() || lineStart == 0) {
            return false;
        }
        std::size_t end = lineStart - 1;
        if (end > 0 && bufferText[end - 1] == '\r') {
            --end;
        }
        std::size_t run = 0;
        while (end >= marker.size() && bufferText.substr(end - marker.size(), marker.size()) == marker) {
            ++run;
            end -= marker.size();
        }
        return run % 2 == 1;
    }

} // namespace

std::optional<int> IndentColumnForLine(const Mode& mode, std::string_view bufferText, std::size_t lineStart,
                                       std::size_t lineEnd, const IndentOverride& bufferIndent,
                                       const std::vector<std::pair<std::size_t, std::size_t>>* ranges) {
    if (!mode.indentColumn) {
        return std::nullopt;
    }
    if (ranges != nullptr) {
        if (const std::pair<std::size_t, std::size_t>* range = RangeContainingLine(*ranges, lineStart)) {
            return BlockCommentContinuationColumn(bufferText, *range, lineStart, lineEnd);
        }
    }
    // No precomputed ranges (the single-interactive-line path -- newline,
    // indent-for-tab-command): bound the highlight pass to just the single
    // point LineIsVerbatim actually tests (lineStart -- lineEnd plays no
    // part in that check) instead of paying for a whole-document query
    // pass. CapturesInRange's overlap+tree-pruned semantics still find a
    // multi-line string that opened far above lineStart, so this is not an
    // approximation -- see VerbatimRanges' own doc comment. NOT
    // {lineStart, lineEnd}: newline's own call passes lineEnd == lineStart
    // (its "blank line, no bound" convention -- mode.indentColumn's own
    // reading of that empty range, unrelated to what a highlight window
    // needs here), which would make the window zero-width and silently
    // report "nothing is verbatim" every time. Confirmed live: this call
    // alone cost ~60ms on this project's own 168KB main.cpp, unbounded, on
    // every single keystroke.
    else {
        const std::vector<std::pair<std::size_t, std::size_t>> single =
            VerbatimRanges(mode, bufferText, HighlightWindow{lineStart, std::min(lineStart + 1, bufferText.size())});
        if (const std::pair<std::size_t, std::size_t>* range = RangeContainingLine(single, lineStart)) {
            return BlockCommentContinuationColumn(bufferText, *range, lineStart, lineEnd);
        }
    }
    // A continued line is laid out by its author, often under something the
    // grammar doesn't see (a shell command's arguments, a macro's body), so a
    // reindent leaves it. A line not yet typed on -- the cursor's, after
    // Enter -- goes a level past the line that began the statement, or level
    // with an earlier continuation.
    if (ContinuedFromPreviousLine(bufferText, lineStart, mode.lineContinuation)) {
        if (FirstNonBlankByte(bufferText, lineStart, lineEnd) < lineEnd) {
            return std::nullopt;
        }
        const std::size_t previousStart = LineStartFor(bufferText, lineStart - 1);
        const IndentStyle style         = bufferIndent.AppliedTo(EffectiveIndentStyle(mode.name));
        const int         previous      = VisualColumnInLine(
            bufferText, previousStart, FirstNonBlankByte(bufferText, previousStart, lineStart - 1), std::max(1, style.width));
        return ContinuedFromPreviousLine(bufferText, previousStart, mode.lineContinuation) ? previous
                                                                                           : previous + style.width;
    }
    return mode.indentColumn(bufferText, lineStart, lineEnd, bufferIndent);
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

std::size_t CopyPreviousLineIndent(text::Buffer& buffer, std::size_t lineStart) {
    const text::ITextStorage& content = buffer.Content();
    std::string               indent;
    for (std::size_t line = content.ByteOffsetToLine(lineStart); line-- > 0;) {
        const std::size_t start     = content.LineToByteOffset(line);
        const std::size_t end       = content.LineToByteOffset(line + 1) - 1; // excludes the '\n'
        const std::size_t indentEnd = LineIndentEnd(content, start);
        if (indentEnd < end) {
            indent = content.Substring(start, indentEnd - start);
            break;
        }
    }
    const std::size_t oldLength = LineIndentEnd(content, lineStart) - lineStart;
    if (indent.size() != oldLength || content.Substring(lineStart, oldLength) != indent) {
        buffer.BeginUndoGroup();
        buffer.DeleteRange(lineStart, oldLength);
        buffer.InsertAt(lineStart, indent);
        buffer.EndUndoGroup();
    }
    return lineStart + indent.size();
}

namespace {

    // Enough for any real nesting of column anchors; a plan still moving
    // after this many passes is applied as it stands.
    constexpr int kMaxReindentPasses = 64;

    // (line, column) for every line in [startLine, endLineExclusive) of
    // `text` that has an opinion, measured against `text` as it stands.
    std::vector<std::pair<std::size_t, int>> PlanReindent(const Mode& mode, std::string_view text, std::size_t startLine,
                                                          std::size_t endLineExclusive, const IndentOverride& bufferIndent) {
        const std::vector<std::pair<std::size_t, std::size_t>> verbatim = VerbatimRanges(mode, text);
        // Where the parse failed, the author's own indentation is the only
        // structure there is -- see Mode.h's UnreliableIndentFunction.
        const std::vector<std::pair<std::size_t, std::size_t>> unreliable =
            mode.unreliableIndentRanges ? mode.unreliableIndentRanges(text) : std::vector<std::pair<std::size_t, std::size_t>>{};

        std::vector<std::size_t> lineStarts{0};
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                lineStarts.push_back(i + 1);
            }
        }

        std::vector<std::pair<std::size_t, int>> plan;
        for (std::size_t line = startLine; line < std::min(endLineExclusive, lineStarts.size()); ++line) {
            const std::size_t lineStart = lineStarts[line];
            const std::size_t lineEnd   = line + 1 < lineStarts.size() ? lineStarts[line + 1] - 1 : text.size();
            if (RangeContainingLine(unreliable, lineStart) != nullptr) {
                continue;
            }
            // A line with no content of its own is left alone: indenting it
            // writes pure trailing whitespace, and a batch reformat has no
            // cursor waiting there (the idempotence property test found
            // Python's final empty line gaining four spaces). The live path
            // indents an empty line on purpose -- it is the cursor's.
            std::size_t indentEnd = lineStart;
            while (indentEnd < lineEnd && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
                ++indentEnd;
            }
            if (indentEnd >= lineEnd) {
                continue;
            }
            if (const std::optional<int> column = IndentColumnForLine(mode, text, lineStart, lineEnd, bufferIndent, &verbatim)) {
                plan.emplace_back(line, *column);
            }
        }
        return plan;
    }

    // `text` with each planned line's leading whitespace replaced.
    std::string ApplyReindentPlan(std::string_view text, const std::vector<std::pair<std::size_t, int>>& plan, const IndentStyle& style) {
        std::string out;
        out.reserve(text.size());
        std::size_t line   = 0;
        std::size_t cursor = 0;
        auto        next   = plan.begin();
        while (cursor <= text.size()) {
            const std::size_t newline = text.find('\n', cursor);
            const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
            if (next != plan.end() && next->first == line) {
                std::size_t indentEnd = cursor;
                while (indentEnd < lineEnd && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
                    ++indentEnd;
                }
                out += IndentString(next->second, style);
                out.append(text, indentEnd, lineEnd - indentEnd);
                ++next;
            }
            else {
                out.append(text, cursor, lineEnd - cursor);
            }
            if (newline == std::string_view::npos) {
                break;
            }
            out += '\n';
            cursor = newline + 1;
            ++line;
        }
        return out;
    }

} // namespace

std::size_t IndentRegion(text::Buffer& buffer, const Mode& mode, std::size_t startLine, std::size_t endLineExclusive,
                         bool* refused) {
    if (refused != nullptr) {
        *refused = false;
    }
    if (!mode.indentColumn) {
        return 0;
    }

    const IndentStyle style = EffectiveIndentStyle(buffer, mode.name);

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

    // An ordinary buffer is planned against its text first and checked
    // against Mode::sameStructure before any of it touches the buffer. The
    // plan is re-derived from its own result until it stops moving: a column
    // result (`@aligned`, `@indent.body`, a mid-line YAML body) is measured
    // from its container's line, and when that line moves too -- any flattened
    // Lisp -- the first plan measured it where it used to be. Each pass settles
    // one more level of such anchors; an already-indented file takes one.
    if (!huge) {
        const std::string original = buffer.Text();
        std::string       working  = original;
        std::map<std::size_t, int> columns; // line -> planned column
        for (int pass = 0; pass < kMaxReindentPasses; ++pass) {
            const std::vector<std::pair<std::size_t, int>> plan = PlanReindent(mode, working, startLine, endLineExclusive, buffer.LocalIndent());
            std::string planned = ApplyReindentPlan(working, plan, style);
            for (const auto& [line, column] : plan) {
                columns.insert_or_assign(line, column);
            }
            if (planned == working) {
                break;
            }
            working = std::move(planned);
        }

        const bool refuse = working != original && mode.sameStructure && !mode.sameStructure(original, working);
        if (refused != nullptr) {
            *refused = refuse;
        }
        buffer.BeginUndoGroup();
        std::size_t changed = 0;
        if (!refuse) {
            // Bottom-to-top, so an edit never shifts a line still to come.
            for (auto it = columns.rbegin(); it != columns.rend(); ++it) {
                if (SetLineIndent(buffer, buffer.Content().LineToByteOffset(it->first), it->second, style) != 0) {
                    ++changed;
                }
            }
        }
        buffer.EndUndoGroup();
        return changed;
    }

    // A huge buffer is reindented in place, window by window, and goes
    // unchecked: a whole-document parse is what its windowing exists to avoid.
    const std::vector<std::pair<std::size_t, std::size_t>> verbatim = VerbatimRanges(
        mode, initialContent.Substring(initialContent.LineToByteOffset(windowStartLine),
                                       (windowEndLineExclusive < initialContent.LineCount() ? initialContent.LineToByteOffset(windowEndLineExclusive)
                                                                                           : initialContent.ByteLength()) -
                                           initialContent.LineToByteOffset(windowStartLine)));
    const std::vector<std::pair<std::size_t, std::size_t>> unreliable =
        !mode.unreliableIndentRanges
            ? std::vector<std::pair<std::size_t, std::size_t>>{}
            : mode.unreliableIndentRanges(initialContent.Substring(
                  initialContent.LineToByteOffset(windowStartLine),
                  (windowEndLineExclusive < initialContent.LineCount() ? initialContent.LineToByteOffset(windowEndLineExclusive)
                                                                      : initialContent.ByteLength()) -
                      initialContent.LineToByteOffset(windowStartLine)));

    buffer.BeginUndoGroup();
    std::size_t changed = 0;
    // Bottom-to-top: reindenting a line's own leading whitespace never
    // shifts the byte offsets of any earlier, still-to-process line.
    for (std::size_t line = endLineExclusive; line-- > startLine;) {
        const text::ITextStorage& content = buffer.Content();
        if (line >= content.LineCount()) {
            continue;
        }
        const std::size_t lineStart = content.LineToByteOffset(line);
        std::size_t       lineEnd   = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
        if (line + 1 < content.LineCount() && lineEnd > lineStart) {
            --lineEnd; // exclude the line's own trailing '\n'
        }
        const std::size_t windowStartByte = content.LineToByteOffset(windowStartLine);
        if (RangeContainingLine(unreliable, lineStart - windowStartByte) != nullptr) {
            continue;
        }
        // Re-fetched every line: an earlier iteration's edit can shift
        // windowEnd's byte offset, never windowStart's.
        const std::size_t windowEndByte = (windowEndLineExclusive < content.LineCount()) ? content.LineToByteOffset(windowEndLineExclusive)
                                                                                          : content.ByteLength();
        const std::string windowText    = content.Substring(windowStartByte, windowEndByte - windowStartByte);
        const std::optional<int> column = IndentColumnForLine(mode, windowText, lineStart - windowStartByte, lineEnd - windowStartByte,
                                                              buffer.LocalIndent(), &verbatim);
        if (!column || LineIndentEnd(buffer.Content(), lineStart) >= lineEnd) {
            continue;
        }
        if (SetLineIndent(buffer, lineStart, *column, style) != 0) {
            ++changed;
        }
    }
    buffer.EndUndoGroup();
    return changed;
}

std::size_t IndentBuffer(text::Buffer& buffer, const Mode& mode, bool* refused) {
    return IndentRegion(buffer, mode, 0, buffer.Content().LineCount(), refused);
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

std::size_t ConvertIndentation(text::Buffer& buffer, const IndentStyle& style, std::size_t startLine,
                               std::size_t endLineExclusive) {
    const int width = std::max(1, style.width);

    buffer.BeginUndoGroup();
    std::size_t changed = 0;
    for (std::size_t line = endLineExclusive; line-- > startLine;) {
        const text::ITextStorage& content = buffer.Content();
        if (line >= content.LineCount()) {
            continue;
        }
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t indentEnd = LineIndentEnd(content, lineStart);
        if (indentEnd == lineStart || indentEnd >= content.ByteLength() || content.CodepointAt(indentEnd).codepoint == U'\n') {
            continue; // unindented or whitespace-only
        }
        int column = 0;
        for (std::size_t offset = lineStart; offset < indentEnd; ++offset) { // spaces and tabs are one byte each
            column = content.CodepointAt(offset).codepoint == U'\t' ? (column / width + 1) * width : column + 1;
        }
        if (SetLineIndent(buffer, lineStart, column, style) != 0) {
            ++changed;
        }
    }
    buffer.EndUndoGroup();
    return changed;
}

} // namespace ned::editor
