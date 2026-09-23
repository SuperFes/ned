#include "ImprintIndent.h"

#include <map>
#include <optional>
#include <string>

#include "Editor/Imprint.h"
#include "Editor/ImprintBracket.h"
#include "Editor/ImprintTables.h"

namespace ned::editor::imprint {

namespace {

    // Whether an indentation body is indented relative to something -- see
    // the header. Mid-line, the row's earlier content is the header; on its
    // own line, FoldAnchorStart's search for a shallower line above answers.
    bool BeginsMidLine(std::size_t startByte, std::string_view text) {
        if (startByte == 0 || startByte > text.size()) {
            return false;
        }
        for (std::size_t i = startByte; i > 0 && text[i - 1] != '\n'; --i) {
            if (text[i - 1] != ' ' && text[i - 1] != '\t') {
                return true;
            }
        }
        return false;
    }

    bool HasHeader(const DelimitedBody& body, std::size_t startByte, std::string_view text) {
        if (startByte == 0 || startByte > text.size()) {
            return false;
        }
        if (BeginsMidLine(startByte, text)) {
            return true;
        }
        return FoldAnchorStart(body, startByte, text) != startByte;
    }

    // The anonymous child carrying the closer DelimitersOf reported -- the walk
    // in Indent.cpp resolves a dedent to its container by climbing from the
    // token to the captured identity, so the token is what gets recorded.
    std::optional<ImprintDedent> CloserOf(const grammar::Node& node, const DelimiterPair& pair) {
        for (std::size_t i = node.ChildCount(); i-- > 0;) {
            const grammar::Node child = node.Child(i);
            if (!child.IsNull() && !child.IsNamed() && child.StartByte() == pair.closeStart &&
                child.EndByte() == pair.closeEnd) {
                return ImprintDedent{child.StartByte(), child.EndByte(), child.Type()};
            }
        }
        return std::nullopt;
    }

    void CollectOne(const grammar::Node& node, const ImprintTable& table, std::string_view text,
                    ImprintIndentCaptures& out) {
        if (const auto entry = table.find(node.Type()); entry != table.end()) {
            const DelimitedBody& body = entry->second;
            if (body.kind != DelimiterKind::Indent) {
                if (const std::optional<DelimiterPair> pair = DelimitersOf(node, body)) {
                    // for-loop-header-imprint follow-up: endByte stays
                    // node.EndByte() -- the live tree node's own true span,
                    // which every lookup (Indent.cpp's keyOf()) recomputes
                    // fresh and must match exactly, so narrowing it here
                    // would make the container unfindable, not narrower (an
                    // earlier version of this fix did exactly that). A node
                    // whose grammar production trails its closer with a
                    // required FIELD the classifier now recognizes (a
                    // for-loop's own `body` statement, GrammarImprint.cpp's
                    // MatchBracketed) instead gets an interiorEnd CAP,
                    // consulted only by the walk's own containment check
                    // (Indent.cpp's interiorContains) -- without it, a line
                    // genuinely inside the for-loop's own {...} body
                    // (already its own, separate compound_statement
                    // container) got double-counted: once for its real
                    // enclosing block, once more for this container wrongly
                    // reaching past its own closing ')'. Reported live: a
                    // for-loop's body gained one extra, unwanted indent
                    // level end to end, including its own closing brace.
                    ImprintContainer container{node.StartByte(), node.EndByte(), node.Type(), pair->openEnd};
                    if (pair->closeEnd != node.EndByte()) {
                        container.interiorEnd = pair->closeEnd;
                    }
                    out.containers.push_back(container);
                    if (const std::optional<ImprintDedent> closer = CloserOf(node, *pair)) {
                        out.dedents.push_back(*closer);
                    }
                }
            }
            else if (!body.openerIsFirst && HasHeader(body, node.StartByte(), text)) {
                out.containers.push_back(ImprintContainer{node.StartByte(), node.EndByte(), node.Type(),
                                                          node.StartByte(),
                                                          BeginsMidLine(node.StartByte(), text)});
            }
        }
    }

    // One cursor for the whole subtree, in the same pre-order the recursion
    // it replaced produced -- see Node::WalkSubtree for why the recursion
    // itself was the cost.
    void Collect(const grammar::Node& root, const ImprintTable& table, std::string_view text,
                 ImprintIndentCaptures& out) {
        root.WalkSubtree([&](const grammar::Node& node, std::size_t) { CollectOne(node, table, text, out); },
                         [](const grammar::Node&, std::size_t) {});
    }

} // namespace

ImprintIndentCaptures CollectIndentCaptures(const grammar::Node& root, std::string_view language,
                                            std::string_view text) {
    ImprintIndentCaptures captures;
    const auto&           table = TableFor(language);
    if (table.empty() || root.IsNull()) {
        return captures;
    }
    Collect(root, table, text, captures);
    return captures;
}

} // namespace ned::editor::imprint
