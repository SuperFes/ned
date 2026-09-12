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
    bool HasHeader(const DelimitedBody& body, std::size_t startByte, std::string_view text) {
        if (startByte == 0 || startByte > text.size()) {
            return false;
        }
        for (std::size_t i = startByte; i > 0 && text[i - 1] != '\n'; --i) {
            if (text[i - 1] != ' ' && text[i - 1] != '\t') {
                return true;
            }
        }
        return FoldAnchorStart(body, startByte, text) != startByte;
    }

    // The anonymous child carrying the closer DelimitersOf reported -- the walk
    // in Indent.cpp resolves a dedent to its container by climbing from the
    // token to the captured identity, so the token is what gets recorded.
    std::optional<ImprintDedent> CloserOf(const treesitter::Node& node, const DelimiterPair& pair) {
        for (std::size_t i = node.ChildCount(); i-- > 0;) {
            const treesitter::Node child = node.Child(i);
            if (!child.IsNull() && !child.IsNamed() && child.StartByte() == pair.closeStart &&
                child.EndByte() == pair.closeEnd) {
                return ImprintDedent{child.StartByte(), child.EndByte(), child.Id()};
            }
        }
        return std::nullopt;
    }

    void Collect(const treesitter::Node& node, const std::map<std::string, DelimitedBody>& table,
                 std::string_view text, ImprintIndentCaptures& out) {
        if (node.IsNull()) {
            return;
        }
        if (const auto entry = table.find(std::string(node.Type())); entry != table.end()) {
            const DelimitedBody& body = entry->second;
            if (body.kind != DelimiterKind::Indent) {
                if (const std::optional<DelimiterPair> pair = DelimitersOf(node, body)) {
                    out.containers.push_back(ImprintContainer{node.Id(), pair->openEnd});
                    if (const std::optional<ImprintDedent> closer = CloserOf(node, *pair)) {
                        out.dedents.push_back(*closer);
                    }
                }
            }
            else if (!body.openerIsFirst && HasHeader(body, node.StartByte(), text)) {
                out.containers.push_back(ImprintContainer{node.Id(), node.StartByte()});
            }
        }
        const std::size_t childCount = node.ChildCount();
        for (std::size_t i = 0; i < childCount; ++i) {
            Collect(node.Child(i), table, text, out);
        }
    }

} // namespace

ImprintIndentCaptures CollectIndentCaptures(const treesitter::Node& root, std::string_view language,
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
