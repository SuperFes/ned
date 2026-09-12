#include "ImprintBracket.h"

#include <optional>
#include <string>
#include <string_view>

#include "Editor/ImprintTables.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"

namespace ned::editor::imprint {

namespace {

    // The four bracket kinds the imprint recognises as delimiters -- the same set
    // TreeSitter/GrammarImprint.cpp infers from, angle brackets included (template
    // and type-parameter lists, JSX opening elements).
    constexpr std::string_view kOpeners = "([{<";
    constexpr std::string_view kClosers = ")]}>";

    std::optional<char> OpenerFor(std::string_view closer) {
        if (closer.size() != 1) {
            return std::nullopt;
        }
        const std::size_t at = kClosers.find(closer[0]);
        return at == std::string_view::npos ? std::nullopt : std::optional<char>(kOpeners[at]);
    }

std::optional<DelimiterPair> PairFor(const treesitter::Node& node,
                                     const std::map<std::string, DelimitedBody>& table) {
    const auto entry = table.find(std::string(node.Type()));
    if (entry == table.end()) {
        return std::nullopt;
    }
    return DelimitersOf(node, entry->second);
}

// Deepest-first, so an inner pair wins over the outer one that contains it.
void Search(const treesitter::Node& node, const std::map<std::string, DelimitedBody>& table, std::size_t point,
            std::optional<DelimiterPair>& onDelimiter, std::optional<DelimiterPair>& adjacent) {
    if (node.IsNull() || point < node.StartByte() || point > node.EndByte()) {
        return;
    }
    for (std::size_t i = 0; i < node.ChildCount(); ++i) {
        Search(node.Child(i), table, point, onDelimiter, adjacent);
    }
    if (onDelimiter.has_value()) {
        return; // an inner match already won
    }

    const std::optional<DelimiterPair> pair = PairFor(node, table);
    if (!pair.has_value()) {
        return;
    }
    const bool onOpen  = point >= pair->openStart && point < pair->openEnd;
    const bool onClose = point >= pair->closeStart && point < pair->closeEnd;
    if (onOpen || onClose) {
        onDelimiter = pair;
        return;
    }
    // Immediately after either delimiter -- the caret-just-past-a-brace case.
    if (!adjacent.has_value() && (point == pair->openEnd || point == pair->closeEnd)) {
        adjacent = pair;
    }
}

} // namespace

std::optional<DelimiterPair> DelimitersOf(const treesitter::Node& node) {
    if (node.IsNull() || node.ChildCount() < 2) {
        return std::nullopt;
    }
    for (std::size_t i = node.ChildCount(); i-- > 0;) {
        const treesitter::Node close = node.Child(i);
        if (close.IsNull() || close.IsNamed()) {
            continue;
        }
        const std::optional<char> opener = OpenerFor(close.Type());
        if (!opener.has_value()) {
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const treesitter::Node open = node.Child(j);
            if (open.IsNull() || open.IsNamed() || !OpensWithBracket(open.Type(), *opener)) {
                continue;
            }
            return DelimiterPair{open.StartByte(), open.EndByte(), close.StartByte(), close.EndByte()};
        }
        return std::nullopt; // a closer with no opener before it is not a pair
    }
    return std::nullopt;
}

std::optional<DelimiterPair> DelimitersOf(const treesitter::Node& node, const DelimitedBody& body) {
    switch (body.kind) {
        case DelimiterKind::Bracket:
            return DelimitersOf(node);
        case DelimiterKind::Indent:
            return std::nullopt; // a dedent is not a token
        case DelimiterKind::Keyword:
            break;
    }
    if (node.IsNull() || node.ChildCount() < 2) {
        return std::nullopt;
    }
    // Same shape as the bracket walk: the LAST anonymous child spelling the
    // closer, then the first anonymous child before it spelling the opener --
    // so a body whose closer is followed by optional members still pairs.
    for (std::size_t i = node.ChildCount(); i-- > 0;) {
        const treesitter::Node close = node.Child(i);
        if (close.IsNull() || close.IsNamed() || close.Type() != body.closer) {
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const treesitter::Node open = node.Child(j);
            if (!open.IsNull() && !open.IsNamed() && open.Type() == body.opener) {
                return DelimiterPair{open.StartByte(), open.EndByte(), close.StartByte(), close.EndByte()};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<DelimiterPair> MatchingDelimitersAt(const treesitter::Node& root, std::string_view language,
                                                  std::size_t point) {
    const auto& table = TableFor(language);
    if (table.empty() || root.IsNull()) {
        return std::nullopt;
    }
    std::optional<DelimiterPair> onDelimiter;
    std::optional<DelimiterPair> adjacent;
    Search(root, table, point, onDelimiter, adjacent);
    return onDelimiter.has_value() ? onDelimiter : adjacent;
}

std::optional<std::size_t> MatchingDelimiterOffset(const treesitter::Node& root, std::string_view language,
                                                   std::size_t point) {
    const std::optional<DelimiterPair> pair = MatchingDelimitersAt(root, language, point);
    if (!pair.has_value()) {
        return std::nullopt;
    }
    // On (or just past) the opener -> go to the closer, and vice versa.
    const bool nearOpen = point <= pair->openEnd;
    return nearOpen ? pair->closeStart : pair->openStart;
}

std::string LanguageKeyForMode(std::string_view modeName) {
    constexpr std::string_view kSuffix = "-mode";
    std::string                key(modeName);
    if (key.size() > kSuffix.size() && key.ends_with(kSuffix)) {
        key.resize(key.size() - kSuffix.size());
    }
    return key;
}

} // namespace ned::editor::imprint
