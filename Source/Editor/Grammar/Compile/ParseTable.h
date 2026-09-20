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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/NodeTypes.h"

namespace ned::editor::grammar::compile {

// 32-bit, not size_t: a ParseAction is stored per entry and a large grammar
// holds tens of millions of them before minimization, so the padding costs
// real memory. The pre-minimized table runs to hundreds of thousands of
// states, well past a uint16 -- the narrower widths the runtime ABI uses
// (Editor/Parse/Abi.h) apply to the minimized table, and Tables.cpp already
// casts at that boundary.
using ParseStateId     = std::uint32_t;
using ProductionInfoId = std::uint32_t;
using LexStateId       = std::uint32_t;

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
    std::uint32_t    childCount        = 0; // Reduce
    int              dynamicPrecedence = 0; // Reduce
    ProductionInfoId productionId      = 0; // Reduce

    auto operator<=>(const ParseAction&) const = default;

    static ParseAction Shift(ParseStateId state, bool isRepetition = false) {
        return {.kind = Kind::Shift, .state = state, .isRepetition = isRepetition};
    }
    static ParseAction Reduce(Symbol symbol, std::uint32_t childCount, int dynamicPrecedence, ProductionInfoId productionId) {
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

// A map keyed on Symbol, kept as one sorted buffer rather than a node per
// entry. Every parse state holds two, and a large grammar builds hundreds of
// thousands of states before minimization, so std::map's per-entry node --
// a red-black header plus its own malloc chunk, around 48 bytes over the
// entry itself -- was the single biggest thing in the generator's memory.
// Iteration is ascending by Symbol, the order the rest of the generator and
// the serialized tables both depend on.
//
// References into one are invalidated by any insertion, unlike std::map's;
// no caller holds one across an insert.
template <typename T>
class SymbolMap {
  public:
    using value_type     = std::pair<Symbol, T>;
    using iterator       = typename std::vector<value_type>::iterator;
    using const_iterator = typename std::vector<value_type>::const_iterator;

    iterator begin() {
        return entries_.begin();
    }
    iterator end() {
        return entries_.end();
    }
    [[nodiscard]] const_iterator begin() const {
        return entries_.begin();
    }
    [[nodiscard]] const_iterator end() const {
        return entries_.end();
    }
    [[nodiscard]] std::size_t size() const {
        return entries_.size();
    }
    [[nodiscard]] bool empty() const {
        return entries_.empty();
    }

    [[nodiscard]] iterator find(Symbol symbol) {
        return Found(LowerBound(symbol), symbol, entries_.end());
    }
    [[nodiscard]] const_iterator find(Symbol symbol) const {
        return Found(LowerBound(symbol), symbol, entries_.end());
    }
    [[nodiscard]] std::size_t count(Symbol symbol) const {
        return find(symbol) == entries_.end() ? 0 : 1;
    }

    T& at(Symbol symbol) {
        const iterator found = find(symbol);
        if (found == entries_.end())
            throw std::out_of_range("SymbolMap::at");
        return found->second;
    }
    [[nodiscard]] const T& at(Symbol symbol) const {
        const const_iterator found = find(symbol);
        if (found == entries_.end())
            throw std::out_of_range("SymbolMap::at");
        return found->second;
    }

    T& operator[](Symbol symbol) {
        return try_emplace(symbol).first->second;
    }

    template <typename... Args>
    std::pair<iterator, bool> try_emplace(Symbol symbol, Args&&... args) {
        const iterator pos = LowerBound(symbol);
        if (pos != entries_.end() && pos->first == symbol)
            return {pos, false};
        return {entries_.insert(pos, value_type{symbol, T(std::forward<Args>(args)...)}), true};
    }

  private:
    template <typename It>
    static It Found(It pos, Symbol symbol, It last) {
        return pos != last && pos->first == symbol ? pos : last;
    }
    iterator LowerBound(Symbol symbol) {
        return std::lower_bound(entries_.begin(), entries_.end(), symbol, Less);
    }
    [[nodiscard]] const_iterator LowerBound(Symbol symbol) const {
        return std::lower_bound(entries_.begin(), entries_.end(), symbol, Less);
    }
    static bool Less(const value_type& entry, Symbol symbol) {
        return entry.first < symbol;
    }

    std::vector<value_type> entries_;
};

struct ParseState {
    ParseStateId                      id = 0;
    SymbolMap<ParseTableEntry>        terminalEntries;
    SymbolMap<GotoAction>             nonterminalEntries;
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
