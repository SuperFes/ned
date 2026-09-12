#include "Imprint.h"

#include <algorithm>
#include <cctype>

namespace ned::editor::imprint {

std::string DelimiterKindName(DelimiterKind kind) {
    switch (kind) {
        case DelimiterKind::Bracket: return "Bracket";
        case DelimiterKind::Indent:  return "Indent";
        case DelimiterKind::Keyword:
            return "Keyword";
    }
    return "?";
}

bool OpensWithBracket(std::string_view token, char bracket) {
    if (token.empty() || token.back() != bracket) {
        return false;
    }
    const std::string_view prefix = token.substr(0, token.size() - 1);
    return std::all_of(prefix.begin(), prefix.end(), [](unsigned char c) { return std::ispunct(c) != 0; });
}

bool ShouldFold(const DelimitedBody& body, const FoldPolicy& policy) {
    // A body holding exactly one subexpression has nothing to collapse:
    // `parenthesized_expression`, `decltype(x)`, `index_expression`.
    if (!body.listLikeInterior) {
        return false;
    }
    // An argument/parameter list is the list-like bracketed body that does
    // not open its own production -- the callee or declarator precedes it.
    // That is precisely the separable case the policy exists for; every
    // statement/member block opens with its own brace.
    //
    // Bracket-only, deliberately: an indentation body reports the same
    // `openerIsFirst == false` for an unrelated reason -- it has no opener at
    // all -- and an indented suite is never an argument list. Without this
    // guard, turning argument lists off would stop Python folding.
    if (body.kind == DelimiterKind::Bracket && !body.openerIsFirst && !policy.foldArgumentLists) {
        return false;
    }
    return true;
}

bool SupersededByChildBody(const DelimitedBody& node, std::size_t nodeStartByte, std::size_t nodeEndByte,
                           const std::vector<ChildBody>& children, std::string_view text) {
    return std::any_of(children.begin(), children.end(), [&](const ChildBody& child) {
        if (!child.foldable || child.endByte != nodeEndByte)
            return false;
        if (node.openerIsFirst)
            return true; // this node's row is its own to give away
        if (child.startByte <= nodeStartByte)
            return true; // hides everything this node would
        const std::size_t from = std::min(nodeStartByte, text.size());
        const std::size_t to   = std::min(child.startByte, text.size());
        return text.substr(from, to - from).find('\n') == std::string_view::npos; // same row
    });
}

std::size_t FoldAnchorStart(const DelimitedBody& body, std::size_t startByte, std::string_view text) {
    if (body.kind != DelimiterKind::Indent || body.openerIsFirst || startByte == 0 || startByte > text.size()) {
        return startByte;
    }

    const auto lineStartOf = [&text](std::size_t offset) -> std::size_t {
        if (offset == 0)
            return 0;
        const std::size_t newline = text.rfind('\n', offset - 1);
        return newline == std::string_view::npos ? 0 : newline + 1;
    };
    const auto indentWidth = [&text](std::size_t lineStart) -> std::size_t {
        std::size_t width = lineStart;
        while (width < text.size() && (text[width] == ' ' || text[width] == '\t'))
            ++width;
        return width - lineStart;
    };

    const std::size_t bodyLineStart = lineStartOf(startByte);
    if (bodyLineStart + indentWidth(bodyLineStart) != startByte) {
        return startByte; // the body begins mid-line; that line is its own
    }

    // The header is the nearest line above that is indented LESS than the
    // body. Blank lines are stepped over, so a body separated from its `def`
    // by an empty line still folds from the `def`; so are lines at the body's
    // own indentation or deeper, which between a header and its body can only
    // be comments -- a `# comment` as the first line under `def f():` is not
    // the header, and treating it as one left the fold unanchored and the
    // indent driver (Editor/ImprintIndent.h) counting no container at all.
    const std::size_t bodyIndent = indentWidth(bodyLineStart);
    std::size_t       candidate  = bodyLineStart;
    while (candidate > 0) {
        const std::size_t previous = lineStartOf(candidate - 1);
        const std::size_t indent   = indentWidth(previous);
        if (indent + previous < candidate - 1 && indent < bodyIndent) {
            return previous;
        }
        candidate = previous;
    }
    return startByte; // nothing shallower above it -- a root body owns its own line
}

} // namespace ned::editor::imprint
