//
// The parse table and the LR(1) construction that fills it -- tree-sitter's
// generator `build_tables/{item, item_set_builder, build_parse_table,
// minimize_parse_table}.rs` and `tables.rs`. Conflicts resolve by
// precedence and associativity exactly as there; what survives must be a
// declared conflict, and an undeclared one is an error naming the
// interpretations, so a hand-written grammar gets the same guidance the
// reference gives.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_PARSETABLE_H
#define NED_EDITOR_GRAMMAR_COMPILE_PARSETABLE_H

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/NodeTypes.h"

namespace ned::editor::grammar::compile {

using ParseStateId     = std::size_t;
using ProductionInfoId = std::size_t;
using LexStateId       = std::size_t;

struct ParseAction {
    enum class Kind : std::uint8_t { Accept,
                                     Shift,
                                     ShiftExtra,
                                     Recover,
                                     Reduce };
    Kind             kind         = Kind::Accept;
    ParseStateId     state        = 0; // Shift
    bool             isRepetition = false;
    Symbol           symbol;                // Reduce
    std::size_t      childCount        = 0; // Reduce
    int              dynamicPrecedence = 0; // Reduce
    ProductionInfoId productionId      = 0; // Reduce

    auto operator<=>(const ParseAction&) const = default;

    static ParseAction Shift(ParseStateId state, bool isRepetition = false) {
        return {.kind = Kind::Shift, .state = state, .isRepetition = isRepetition};
    }
    static ParseAction Reduce(Symbol symbol, std::size_t childCount, int dynamicPrecedence, ProductionInfoId productionId) {
        return {.kind = Kind::Reduce, .symbol = symbol, .childCount = childCount, .dynamicPrecedence = dynamicPrecedence, .productionId = productionId};
    }
    static ParseAction Accept() {
        return {.kind = Kind::Accept};
    }
    static ParseAction ShiftExtra() {
        return {.kind = Kind::ShiftExtra};
    }
    static ParseAction Recover() {
        return {.kind = Kind::Recover};
    }
};

struct GotoAction {
    bool         shiftExtra = false;
    ParseStateId state      = 0;

    bool operator==(const GotoAction&) const = default;
};

struct ParseTableEntry {
    std::vector<ParseAction> actions;
    bool                     reusable = true;

    auto operator<=>(const ParseTableEntry&) const = default;
};

struct ParseState {
    ParseStateId                      id = 0;
    std::map<Symbol, ParseTableEntry> terminalEntries;
    std::map<Symbol, GotoAction>      nonterminalEntries;
    TokenSet                          reservedWords;
    LexStateId                        lexStateId         = 0;
    std::size_t                       externalLexStateId = 0;
    std::size_t                       coreId             = 0;

    [[nodiscard]] bool                      IsEndOfNonTerminalExtra() const;
    [[nodiscard]] std::vector<ParseStateId> ReferencedStates() const;
    // Rewrites every shift/goto target through f(target, *this).
    void UpdateReferencedStates(const std::function<ParseStateId(ParseStateId, const ParseState&)>& f);
};

struct FieldLocation {
    std::size_t index     = 0;
    bool        inherited = false;

    auto operator<=>(const FieldLocation&) const = default;
};

struct ProductionInfo {
    std::vector<std::optional<Alias>>                 aliasSequence;
    std::map<std::string, std::vector<FieldLocation>> fieldMap;

    bool operator==(const ProductionInfo&) const = default;
};

struct ParseTable {
    std::vector<ParseState>     states;
    std::vector<Symbol>         symbols;
    std::vector<ProductionInfo> productionInfos;
    std::size_t                 maxAliasedProductionLength = 1;
    std::vector<TokenSet>       externalLexStates;
};

// A state the table already holds, with the symbol sequence that reaches
// it, for conflict reports.
struct ParseTableResult {
    ParseTable table;
};

// Builds the parse table for a prepared grammar (before the lex table, the
// error state and minimization -- see Compiler.cpp for the full order).
// Throws CompileError on an unresolved conflict or a malformed extra.
[[nodiscard]] ParseTable BuildParseTable(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const InlinedProductionMap& inlines,
                                         const std::vector<VariableInfo>& variableInfo);

// FOLLOW information the lex-table builder needs: for each terminal, the
// tokens that can directly follow it.
[[nodiscard]] std::vector<TokenSet> GetFollowingTokens(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const InlinedProductionMap& inlines);

class TokenConflictMap;

void MinimizeParseTable(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const AliasMap& defaultAliases,
                        const TokenConflictMap& conflicts, const TokenSet& keywords);

// Groups states (state ids grouped by group id, and the inverse) and splits
// any group whose members `shouldSplit`; true when something split. Each
// group compares every pair, so membership in the split-off set is a flag
// per state rather than a search.
template <typename State, typename ShouldSplit>
bool SplitStateIdGroups(const std::vector<State>& states, std::vector<std::vector<std::size_t>>& stateIdsByGroupId,
                        std::vector<std::size_t>& groupIdsByStateId, std::size_t startGroupId, ShouldSplit&& shouldSplit) {
    bool                     result = false;
    std::vector<char>        split(states.size(), 0);
    std::vector<std::size_t> splitStateIds;
    for (std::size_t groupId = startGroupId; groupId < stateIdsByGroupId.size(); ++groupId) {
        splitStateIds.clear();
        const std::vector<std::size_t>& stateIds = stateIdsByGroupId[groupId];
        for (std::size_t i = 0; i < stateIds.size(); ++i) {
            const std::size_t left = stateIds[i];
            if (split[left] != 0)
                continue;
            for (std::size_t j = i + 1; j < stateIds.size(); ++j) {
                const std::size_t right = stateIds[j];
                if (split[right] != 0)
                    continue;
                if (shouldSplit(states[left], states[right], groupIdsByStateId)) {
                    split[right] = 1;
                    splitStateIds.push_back(right);
                }
            }
        }
        if (!splitStateIds.empty()) {
            result                          = true;
            std::vector<std::size_t>& group = stateIdsByGroupId[groupId];
            group.erase(std::remove_if(group.begin(), group.end(), [&](std::size_t id) { return split[id] != 0; }), group.end());
            const std::size_t newGroupId = stateIdsByGroupId.size();
            for (const std::size_t id : splitStateIds) {
                groupIdsByStateId[id] = newGroupId;
                split[id]             = 0;
            }
            stateIdsByGroupId.push_back(splitStateIds);
        }
    }
    return result;
}

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_PARSETABLE_H
