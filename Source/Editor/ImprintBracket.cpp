#include "ImprintBracket.h"

#include <string>

#include "Editor/ImprintTables.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"

namespace ned::editor::imprint {

namespace {

// The node's own first and last children, when it is a bracket-delimited body
// -- which by construction is where its delimiters are.
std::optional<DelimiterPair> PairFor(const treesitter::Node& node,
                                     const std::map<std::string, DelimitedBody>& table) {
    const auto entry = table.find(std::string(node.Type()));
    if (entry == table.end() || entry->second.kind != DelimiterKind::Bracket) {
        return std::nullopt;
    }
    if (node.ChildCount() < 2) {
        return std::nullopt;
    }
    const treesitter::Node open  = node.Child(0);
    const treesitter::Node close = node.Child(node.ChildCount() - 1);
    if (open.IsNull() || close.IsNull() || open.StartByte() >= close.StartByte()) {
        return std::nullopt;
    }
    return DelimiterPair{open.StartByte(), open.EndByte(), close.StartByte(), close.EndByte()};
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
