#include "ParseTable.h"

#include <algorithm>
#include <deque>
#include <set>

#include "Editor/Grammar/Compile/LexTable.h"

namespace ned::editor::grammar::compile {

// --- ParseState --------------------------------------------------------------

bool ParseState::IsEndOfNonTerminalExtra() const {
    return terminalEntries.count(Symbol::EndOfNonTerminalExtra()) > 0;
}

std::vector<ParseStateId> ParseState::ReferencedStates() const {
    std::vector<ParseStateId> out;
    for (const auto& [_, entry] : terminalEntries)
        for (const ParseAction& action : entry.actions)
            if (action.kind == ParseAction::Kind::Shift)
                out.push_back(action.state);
    for (const auto& [_, action] : nonterminalEntries)
        if (!action.shiftExtra)
            out.push_back(action.state);
    return out;
}

void ParseState::UpdateReferencedStates(const std::function<ParseStateId(ParseStateId, const ParseState&)>& f) {
    struct Update {
        Symbol       symbol;
        std::size_t  actionIndex;
        ParseStateId newState;
    };
    std::vector<Update> updates;
    for (const auto& [symbol, entry] : terminalEntries) {
        for (std::size_t i = 0; i < entry.actions.size(); ++i) {
            const ParseAction& action = entry.actions[i];
            if (action.kind == ParseAction::Kind::Shift) {
                const ParseStateId result = f(action.state, *this);
                if (result != action.state)
                    updates.push_back({symbol, i, result});
            }
        }
    }
    for (const auto& [symbol, action] : nonterminalEntries) {
        if (!action.shiftExtra) {
            const ParseStateId result = f(action.state, *this);
            if (result != action.state)
                updates.push_back({symbol, 0, result});
        }
    }
    for (const Update& update : updates) {
        if (update.symbol.IsNonTerminal()) {
            nonterminalEntries[update.symbol] = GotoAction{.shiftExtra = false, .state = update.newState};
        }
        else {
            ParseAction& action = terminalEntries.at(update.symbol).actions[update.actionIndex];
            if (action.kind == ParseAction::Kind::Shift)
                action.state = update.newState;
        }
    }
}

namespace {

    // --- items ----------------------------------------------------------------

    const Production& StartProduction() {
        static const Production kStart = {
            .steps             = {ProductionStep{.symbol = Symbol::NonTerminal(0), .reservedWordSetId = kNoReservedWords}},
            .dynamicPrecedence = 0,
        };
        return kStart;
    }

    constexpr std::uint32_t kAugmentedVariable = static_cast<std::uint32_t>(-1);

    struct ParseItem {
        std::uint32_t     variableIndex = kAugmentedVariable;
        std::uint32_t     stepIndex     = 0;
        const Production* production    = &StartProduction();
        // Whether an already-matched child was hidden and had fields: then
        // the preceding children's symbols matter for equality.
        bool hasPrecedingInheritedFields = false;

        static ParseItem Start() {
            return {};
        }
        [[nodiscard]] const ProductionStep* Step() const {
            return stepIndex < production->steps.size() ? &production->steps[stepIndex] : nullptr;
        }
        [[nodiscard]] std::optional<Symbol> NextSymbol() const {
            const ProductionStep* step = Step();
            return step != nullptr ? std::optional(step->symbol) : std::nullopt;
        }
        [[nodiscard]] const ProductionStep* PrevStep() const {
            return stepIndex > 0 ? &production->steps[stepIndex - 1] : nullptr;
        }
        [[nodiscard]] const Precedence& ItemPrecedence() const {
            static const Precedence kNone;
            const ProductionStep*   prev = PrevStep();
            return prev != nullptr ? prev->precedence : kNone;
        }
        [[nodiscard]] std::optional<Associativity> ItemAssociativity() const {
            const ProductionStep* prev = PrevStep();
            return prev != nullptr ? prev->associativity : std::nullopt;
        }
        [[nodiscard]] bool IsDone() const {
            return stepIndex == production->steps.size();
        }
        [[nodiscard]] bool IsAugmented() const {
            return variableIndex == kAugmentedVariable;
        }
        [[nodiscard]] ParseItem Successor() const {
            ParseItem item = *this;
            item.stepIndex++;
            return item;
        }
        [[nodiscard]] ParseItem WithProduction(const Production* other) const {
            ParseItem item  = *this;
            item.production = other;
            return item;
        }

        // The reference's Ord: already-matched steps count only by alias and
        // field. Used for the position of an item within a set.
        [[nodiscard]] std::strong_ordering Order(const ParseItem& other) const {
            if (const auto c = stepIndex <=> other.stepIndex; c != 0)
                return c;
            if (const auto c = variableIndex <=> other.variableIndex; c != 0)
                return c;
            if (const auto c = production->dynamicPrecedence <=> other.production->dynamicPrecedence; c != 0)
                return c;
            if (const auto c = production->steps.size() <=> other.production->steps.size(); c != 0)
                return c;
            if (const auto c = ItemPrecedence() <=> other.ItemPrecedence(); c != 0)
                return c;
            if (const auto c = ItemAssociativity() <=> other.ItemAssociativity(); c != 0)
                return c;
            for (std::size_t i = 0; i < production->steps.size(); ++i) {
                const ProductionStep& a = production->steps[i];
                const ProductionStep& b = other.production->steps[i];
                if (i < stepIndex) {
                    if (const auto c = a.alias <=> b.alias; c != 0)
                        return c;
                    if (const auto c = a.fieldName <=> b.fieldName; c != 0)
                        return c;
                }
                else if (const auto c = a <=> b; c != 0) {
                    return c;
                }
            }
            return std::strong_ordering::equal;
        }

        // The reference's Eq: Order-equal, plus the inherited-fields flag and,
        // when set, the already-matched symbols.
        [[nodiscard]] bool Equals(const ParseItem& other) const {
            if (Order(other) != 0 || hasPrecedingInheritedFields != other.hasPrecedingInheritedFields)
                return false;
            if (hasPrecedingInheritedFields)
                for (std::size_t i = 0; i < stepIndex; ++i)
                    if (production->steps[i].symbol != other.production->steps[i].symbol)
                        return false;
            return true;
        }

        // A total order consistent with Equals, for map keys.
        [[nodiscard]] std::strong_ordering KeyOrder(const ParseItem& other) const {
            if (const auto c = Order(other); c != 0)
                return c;
            if (const auto c = hasPrecedingInheritedFields <=> other.hasPrecedingInheritedFields; c != 0)
                return c;
            if (hasPrecedingInheritedFields)
                for (std::size_t i = 0; i < stepIndex; ++i)
                    if (const auto c = production->steps[i].symbol <=> other.production->steps[i].symbol; c != 0)
                        return c;
            return std::strong_ordering::equal;
        }
    };

    struct ParseItemSetEntry {
        ParseItem         item;
        TokenSet          lookaheads;
        ReservedWordSetId followingReservedWordSet = 0;
    };

    struct ParseItemSet {
        std::vector<ParseItemSetEntry> entries;

        ParseItemSetEntry& Insert(const ParseItem& item) {
            const auto pos = std::lower_bound(entries.begin(), entries.end(), item,
                                              [](const ParseItemSetEntry& e, const ParseItem& i) { return e.item.Order(i) == std::strong_ordering::less; });
            if (pos != entries.end() && pos->item.Order(item) == 0)
                return *pos;
            return *entries.insert(pos, ParseItemSetEntry{.item = item});
        }

        [[nodiscard]] std::strong_ordering KeyOrder(const ParseItemSet& other) const {
            for (std::size_t i = 0; i < entries.size() && i < other.entries.size(); ++i) {
                const ParseItemSetEntry& a = entries[i];
                const ParseItemSetEntry& b = other.entries[i];
                if (const auto c = a.item.KeyOrder(b.item); c != 0)
                    return c;
                if (a.lookaheads < b.lookaheads)
                    return std::strong_ordering::less;
                if (b.lookaheads < a.lookaheads)
                    return std::strong_ordering::greater;
                if (const auto c = a.followingReservedWordSet <=> b.followingReservedWordSet; c != 0)
                    return c;
            }
            return entries.size() <=> other.entries.size();
        }
    };

    struct ItemSetKeyLess {
        bool operator()(const ParseItemSet& a, const ParseItemSet& b) const {
            return a.KeyOrder(b) == std::strong_ordering::less;
        }
    };

    struct CoreKeyLess {
        bool operator()(const std::vector<ParseItem>& a, const std::vector<ParseItem>& b) const {
            for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (const auto c = a[i].KeyOrder(b[i]); c != 0)
                    return c == std::strong_ordering::less;
            return a.size() < b.size();
        }
    };

    struct ItemOrderLess {
        bool operator()(const ParseItem& a, const ParseItem& b) const {
            return a.Order(b) == std::strong_ordering::less;
        }
    };

    // --- item set builder -------------------------------------------------------

    struct FollowSetInfo {
        TokenSet          lookaheads;
        ReservedWordSetId reservedLookaheads   = 0;
        bool              propagatesLookaheads = false;

        bool operator==(const FollowSetInfo&) const = default;
    };

    struct TransitiveClosureAddition {
        ParseItem     item;
        FollowSetInfo info;

        [[nodiscard]] bool Equals(const TransitiveClosureAddition& other) const {
            return item.Equals(other.item) && info == other.info;
        }
    };

    class ParseItemSetBuilder {
      public:
        ParseItemSetBuilder(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const InlinedProductionMap& inlines) : syntax_(syntax), inlines_(inlines) {
            for (std::size_t i = 0; i < lexical.variables.size(); ++i) {
                const Symbol symbol = Symbol::Terminal(static_cast<std::uint32_t>(i));
                TokenSet     set;
                set.Insert(symbol);
                firstSets_[symbol]         = set;
                lastSets_[symbol]          = set;
                reservedFirstSets_[symbol] = 0;
            }
            for (std::size_t i = 0; i < syntax.externalTokens.size(); ++i) {
                const Symbol symbol = Symbol::External(static_cast<std::uint32_t>(i));
                TokenSet     set;
                set.Insert(symbol);
                firstSets_[symbol]         = set;
                lastSets_[symbol]          = set;
                reservedFirstSets_[symbol] = 0;
            }

            std::vector<Symbol> toProcess;
            std::set<Symbol>    processed;
            for (std::size_t i = 0; i < syntax.variables.size(); ++i) {
                const Symbol       symbol           = Symbol::NonTerminal(static_cast<std::uint32_t>(i));
                TokenSet&          firstSet         = firstSets_[symbol];
                ReservedWordSetId& reservedFirstSet = reservedFirstSets_[symbol];

                processed.clear();
                toProcess = {symbol};
                while (!toProcess.empty()) {
                    const Symbol sym = toProcess.back();
                    toProcess.pop_back();
                    for (const Production& production : syntax.variables[sym.index].productions) {
                        if (production.steps.empty())
                            continue;
                        const ProductionStep& step = production.steps.front();
                        if (step.symbol.IsTerminal() || step.symbol.IsExternal())
                            firstSet.Insert(step.symbol);
                        else if (processed.insert(step.symbol).second)
                            toProcess.push_back(step.symbol);
                        reservedFirstSet = std::max(reservedFirstSet, step.reservedWordSetId);
                    }
                }

                TokenSet& lastSet = lastSets_[symbol];
                processed.clear();
                toProcess = {symbol};
                while (!toProcess.empty()) {
                    const Symbol sym = toProcess.back();
                    toProcess.pop_back();
                    for (const Production& production : syntax.variables[sym.index].productions) {
                        if (production.steps.empty())
                            continue;
                        const ProductionStep& step = production.steps.back();
                        if (step.symbol.IsTerminal() || step.symbol.IsExternal())
                            lastSet.Insert(step.symbol);
                        else if (processed.insert(step.symbol).second)
                            toProcess.push_back(step.symbol);
                    }
                }
            }

            // For each non-terminal, the items its expansion always adds to a
            // set, with the lookaheads that always follow them.
            transitiveClosureAdditions_.resize(syntax.variables.size());
            const TokenSet emptyLookaheads;
            struct StackEntry {
                std::size_t       symbolIndex;
                const TokenSet*   lookaheads;
                ReservedWordSetId reservedWordSetId;
                bool              propagatesLookaheads;
            };
            std::vector<StackEntry>              stack;
            std::map<std::size_t, FollowSetInfo> followSetInfoByNonTerminal;
            for (std::size_t i = 0; i < syntax.variables.size(); ++i) {
                stack.clear();
                stack.push_back({i, &emptyLookaheads, 0, true});
                followSetInfoByNonTerminal.clear();
                while (!stack.empty()) {
                    const StackEntry entry = stack.back();
                    stack.pop_back();
                    FollowSetInfo& info   = followSetInfoByNonTerminal[entry.symbolIndex];
                    bool           didAdd = info.lookaheads.InsertAll(*entry.lookaheads);
                    if (entry.reservedWordSetId > info.reservedLookaheads) {
                        info.reservedLookaheads = entry.reservedWordSetId;
                        didAdd                  = true;
                    }
                    didAdd |= entry.propagatesLookaheads && !info.propagatesLookaheads;
                    info.propagatesLookaheads |= entry.propagatesLookaheads;
                    if (!didAdd)
                        continue;

                    for (const Production& production : syntax.variables[entry.symbolIndex].productions) {
                        const std::optional<Symbol> first = production.FirstSymbol();
                        if (!first || !first->IsNonTerminal())
                            continue;
                        if (production.steps.size() > 1) {
                            const Symbol next = production.steps[1].symbol;
                            stack.push_back({first->index, &firstSets_.at(next), reservedFirstSets_.at(next), false});
                        }
                        else {
                            stack.push_back({first->index, entry.lookaheads, entry.reservedWordSetId, entry.propagatesLookaheads});
                        }
                    }
                }

                std::vector<TransitiveClosureAddition>& additions = transitiveClosureAdditions_[i];
                for (const auto& [variableIndex, followSetInfo] : followSetInfoByNonTerminal) {
                    const SyntaxVariable& variable    = syntax.variables[variableIndex];
                    const Symbol          nonTerminal = Symbol::NonTerminal(static_cast<std::uint32_t>(variableIndex));
                    if (syntax.IsInlined(nonTerminal))
                        continue;
                    for (const Production& production : variable.productions) {
                        const ParseItem item{.variableIndex = static_cast<std::uint32_t>(variableIndex), .stepIndex = 0, .production = &production};
                        if (const std::vector<std::size_t>* inlined = inlines.InlinedProductions(&production, 0)) {
                            for (const std::size_t index : *inlined)
                                FindOrPush(additions, {item.WithProduction(&inlines.productions[index]), followSetInfo});
                        }
                        else {
                            FindOrPush(additions, {item, followSetInfo});
                        }
                    }
                }
            }
        }

        [[nodiscard]] ParseItemSet TransitiveClosure(const ParseItemSet& itemSet) const {
            ParseItemSet result;
            for (const ParseItemSetEntry& entry : itemSet.entries) {
                if (const std::vector<std::size_t>* inlined = inlines_.InlinedProductions(entry.item.production, entry.item.stepIndex)) {
                    for (const std::size_t index : *inlined) {
                        ParseItemSetEntry substituted = entry;
                        substituted.item              = entry.item.WithProduction(&inlines_.productions[index]);
                        AddItem(result, substituted);
                    }
                }
                else {
                    AddItem(result, entry);
                }
            }
            return result;
        }

        [[nodiscard]] const TokenSet& FirstSet(Symbol symbol) const {
            return firstSets_.at(symbol);
        }
        [[nodiscard]] const TokenSet* ReservedFirstSet(Symbol symbol) const {
            const auto it = reservedFirstSets_.find(symbol);
            return it == reservedFirstSets_.end() ? nullptr : &syntax_.reservedWordSets[it->second];
        }
        [[nodiscard]] const TokenSet& LastSet(Symbol symbol) const {
            return lastSets_.at(symbol);
        }

      private:
        const SyntaxGrammar&                                syntax_;
        const InlinedProductionMap&                         inlines_;
        std::map<Symbol, TokenSet>                          firstSets_;
        std::map<Symbol, ReservedWordSetId>                 reservedFirstSets_;
        std::map<Symbol, TokenSet>                          lastSets_;
        std::vector<std::vector<TransitiveClosureAddition>> transitiveClosureAdditions_;

        static void FindOrPush(std::vector<TransitiveClosureAddition>& list, TransitiveClosureAddition addition) {
            for (const TransitiveClosureAddition& existing : list)
                if (existing.Equals(addition))
                    return;
            list.push_back(std::move(addition));
        }

        void AddItem(ParseItemSet& set, const ParseItemSetEntry& entry) const {
            if (const ProductionStep* step = entry.item.Step(); step != nullptr && step->symbol.IsNonTerminal()) {
                const ProductionStep* nextStep = entry.item.Successor().Step();
                const TokenSet*       followingTokens;
                ReservedWordSetId     followingReserved;
                if (nextStep != nullptr) {
                    followingTokens   = &firstSets_.at(nextStep->symbol);
                    followingReserved = reservedFirstSets_.at(nextStep->symbol);
                }
                else {
                    followingTokens   = &entry.lookaheads;
                    followingReserved = entry.followingReservedWordSet;
                }

                for (const TransitiveClosureAddition& addition : transitiveClosureAdditions_[step->symbol.index]) {
                    ParseItemSetEntry& added = set.Insert(addition.item);
                    added.lookaheads.InsertAll(addition.info.lookaheads);
                    if (syntax_.wordToken && addition.info.lookaheads.Contains(*syntax_.wordToken))
                        added.followingReservedWordSet = std::max(added.followingReservedWordSet, addition.info.reservedLookaheads);
                    if (addition.info.propagatesLookaheads) {
                        added.lookaheads.InsertAll(*followingTokens);
                        if (syntax_.wordToken && followingTokens->Contains(*syntax_.wordToken))
                            added.followingReservedWordSet = std::max(added.followingReservedWordSet, followingReserved);
                    }
                }
            }
            ParseItemSetEntry& e = set.Insert(entry.item);
            e.lookaheads.InsertAll(entry.lookaheads);
            e.followingReservedWordSet = std::max(e.followingReservedWordSet, entry.followingReservedWordSet);
        }
    };

    // --- parse table builder --------------------------------------------------------

    struct AuxiliarySymbolInfo {
        Symbol              auxiliarySymbol;
        std::vector<Symbol> parentSymbols;

        bool operator==(const AuxiliarySymbolInfo&) const = default;
    };

    struct ReductionInfo {
        Precedence          precedence;
        std::vector<Symbol> symbols; // sorted
        bool                hasLeftAssoc  = false;
        bool                hasRightAssoc = false;
        bool                hasNonAssoc   = false;
    };

    class ParseTableBuilder {
      public:
        ParseTableBuilder(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, ParseItemSetBuilder itemSetBuilder, const std::vector<VariableInfo>& variableInfo) : syntax_(syntax), lexical_(lexical), itemSetBuilder_(std::move(itemSetBuilder)), variableInfo_(variableInfo) {
            for (const std::vector<Symbol>& conflict : syntax.expectedConflicts)
                actualConflicts_.insert(conflict);
        }

        ParseTable Build() {
            table_.productionInfos.emplace_back(); // the empty alias sequence is id 0

            AddParseState({}, {}, ParseItemSet{}); // error state, id 0

            ParseItemSet start;
            start.entries.push_back({.item = ParseItem::Start(), .lookaheads = TokenSet::Of({Symbol::End()}), .followingReservedWordSet = 0});
            AddParseState({}, {}, std::move(start)); // start state, id 1

            std::map<Symbol, ParseItemSet> extraItemSetsByFirstTerminal;
            for (const Symbol extra : syntax_.extraSymbols) {
                if (!extra.IsNonTerminal())
                    continue;
                for (const Production& production : syntax_.variables[extra.index].productions) {
                    const ParseItem item{.variableIndex = extra.index, .stepIndex = 1, .production = &production};
                    extraItemSetsByFirstTerminal[*production.FirstSymbol()].Insert(item).lookaheads.Insert(Symbol::EndOfNonTerminalExtra());
                }
            }
            for (auto& [terminal, itemSet] : extraItemSetsByFirstTerminal) {
                if (terminal.IsNonTerminal())
                    throw CompileError("The non-terminal rule `" + SymbolName(terminal) + "` is used in a non-terminal `extra` rule, which is not allowed.");
                const ParseStateId stateId = AddParseState({}, {}, std::move(itemSet));
                nonTerminalExtraStates_.emplace_back(terminal, stateId);
            }

            while (!queue_.empty()) {
                const QueueEntry entry = std::move(queue_.front());
                queue_.pop_front();
                const ParseItemSet itemSet = itemSetBuilder_.TransitiveClosure(stateInfoById_[entry.stateId].itemSet);
                AddActions(stateInfoById_[entry.stateId].precedingSymbols, entry.precedingAuxiliarySymbols, entry.stateId, itemSet);
            }
            return std::move(table_);
        }

      private:
        struct StateInfo {
            std::vector<Symbol> precedingSymbols;
            ParseItemSet        itemSet;
        };
        struct QueueEntry {
            ParseStateId                     stateId;
            std::vector<AuxiliarySymbolInfo> precedingAuxiliarySymbols;
        };

        const SyntaxGrammar&                                       syntax_;
        const LexicalGrammar&                                      lexical_;
        ParseItemSetBuilder                                        itemSetBuilder_;
        const std::vector<VariableInfo>&                           variableInfo_;
        std::map<std::vector<ParseItem>, std::size_t, CoreKeyLess> coreIdsByCore_;
        std::map<ParseItemSet, ParseStateId, ItemSetKeyLess>       stateIdsByItemSet_;
        std::vector<StateInfo>                                     stateInfoById_;
        std::deque<QueueEntry>                                     queue_;
        std::vector<std::pair<Symbol, ParseStateId>>               nonTerminalExtraStates_;
        std::set<std::vector<Symbol>>                              actualConflicts_;
        ParseTable                                                 table_;

        ParseStateId AddParseState(const std::vector<Symbol>& precedingSymbols, const std::vector<AuxiliarySymbolInfo>& precedingAuxiliarySymbols, ParseItemSet itemSet) {
            if (const auto existing = stateIdsByItemSet_.find(itemSet); existing != stateIdsByItemSet_.end())
                return existing->second;

            std::vector<ParseItem> core;
            for (const ParseItemSetEntry& entry : itemSet.entries)
                core.push_back(entry.item);
            const std::size_t coreCount = coreIdsByCore_.size();
            const std::size_t coreId    = coreIdsByCore_.try_emplace(std::move(core), coreCount).first->second;

            const ParseStateId stateId = table_.states.size();
            stateInfoById_.push_back({precedingSymbols, itemSet});
            table_.states.push_back(ParseState{.id = stateId, .coreId = coreId});
            queue_.push_back({stateId, precedingAuxiliarySymbols});
            stateIdsByItemSet_.emplace(std::move(itemSet), stateId);
            return stateId;
        }

        void AddActions(std::vector<Symbol> precedingSymbols, std::vector<AuxiliarySymbolInfo> precedingAuxiliarySymbols, ParseStateId stateId, const ParseItemSet& itemSet) {
            std::map<Symbol, ParseItemSet>  terminalSuccessors;
            std::map<Symbol, ParseItemSet>  nonTerminalSuccessors;
            TokenSet                        lookaheadsWithConflicts;
            std::map<Symbol, ReductionInfo> reductionInfos;

            for (const ParseItemSetEntry& entry : itemSet.entries) {
                const ParseItem& item = entry.item;
                if (const std::optional<Symbol> nextSymbol = item.NextSymbol()) {
                    ParseItem     successor = item.Successor();
                    ParseItemSet* successorSet;
                    if (nextSymbol->IsNonTerminal()) {
                        const SyntaxVariable& variable = syntax_.variables[nextSymbol->index];
                        if (variable.IsAuxiliary())
                            precedingAuxiliarySymbols.push_back(AuxiliaryNodeInfo(itemSet, *nextSymbol));
                        if (variable.IsHidden() && !variableInfo_[nextSymbol->index].fields.empty())
                            successor.hasPrecedingInheritedFields = true;
                        successorSet = &nonTerminalSuccessors[*nextSymbol];
                    }
                    else {
                        successorSet = &terminalSuccessors[*nextSymbol];
                    }
                    ParseItemSetEntry& successorEntry = successorSet->Insert(successor);
                    successorEntry.lookaheads.InsertAll(entry.lookaheads);
                    successorEntry.followingReservedWordSet = std::max(successorEntry.followingReservedWordSet, entry.followingReservedWordSet);
                }
                else {
                    const Symbol                       symbol        = Symbol::NonTerminal(item.variableIndex);
                    const ParseAction                  action        = item.IsAugmented() ? ParseAction::Accept()
                                                                                          : ParseAction::Reduce(symbol, item.stepIndex, item.production->dynamicPrecedence, GetProductionId(item));
                    const Precedence&                  precedence    = item.ItemPrecedence();
                    const std::optional<Associativity> associativity = item.ItemAssociativity();
                    for (const Symbol lookahead : entry.lookaheads.Symbols()) {
                        ParseTableEntry& tableEntry    = table_.states[stateId].terminalEntries[lookahead];
                        ReductionInfo&   reductionInfo = reductionInfos[lookahead];
                        if (tableEntry.actions.empty()) {
                            tableEntry.actions.push_back(action);
                        }
                        else {
                            const std::strong_ordering c = ComparePrecedence(precedence, {symbol}, reductionInfo.precedence, reductionInfo.symbols);
                            if (c == std::strong_ordering::greater) {
                                tableEntry.actions.clear();
                                tableEntry.actions.push_back(action);
                                lookaheadsWithConflicts.Remove(lookahead);
                                reductionInfo = ReductionInfo{};
                            }
                            else if (c == std::strong_ordering::equal) {
                                tableEntry.actions.push_back(action);
                                lookaheadsWithConflicts.Insert(lookahead);
                            }
                            else {
                                continue;
                            }
                        }
                        reductionInfo.precedence = precedence;
                        const auto pos           = std::lower_bound(reductionInfo.symbols.begin(), reductionInfo.symbols.end(), symbol);
                        if (pos == reductionInfo.symbols.end() || *pos != symbol)
                            reductionInfo.symbols.insert(pos, symbol);
                        if (associativity == Associativity::Left)
                            reductionInfo.hasLeftAssoc = true;
                        else if (associativity == Associativity::Right)
                            reductionInfo.hasRightAssoc = true;
                        else
                            reductionInfo.hasNonAssoc = true;
                    }
                }
            }

            precedingAuxiliarySymbols.erase(std::unique(precedingAuxiliarySymbols.begin(), precedingAuxiliarySymbols.end()), precedingAuxiliarySymbols.end());

            for (auto& [symbol, nextItemSet] : terminalSuccessors) {
                precedingSymbols.push_back(symbol);
                const ParseStateId nextStateId = AddParseState(precedingSymbols, precedingAuxiliarySymbols, std::move(nextItemSet));
                precedingSymbols.pop_back();

                ParseTableEntry& entry = table_.states[stateId].terminalEntries[symbol];
                if (!entry.actions.empty())
                    lookaheadsWithConflicts.Insert(symbol);
                entry.actions.push_back(ParseAction::Shift(nextStateId));
            }
            for (auto& [symbol, nextItemSet] : nonTerminalSuccessors) {
                precedingSymbols.push_back(symbol);
                const ParseStateId nextStateId = AddParseState(precedingSymbols, precedingAuxiliarySymbols, std::move(nextItemSet));
                precedingSymbols.pop_back();
                table_.states[stateId].nonterminalEntries[symbol] = GotoAction{.shiftExtra = false, .state = nextStateId};
            }

            for (const Symbol symbol : lookaheadsWithConflicts.Symbols())
                HandleConflict(itemSet, stateId, precedingSymbols, precedingAuxiliarySymbols, symbol, reductionInfos.at(symbol));

            ParseState& state = table_.states[stateId];
            if (state.IsEndOfNonTerminalExtra()) {
                if (state.terminalEntries.size() > 1) {
                    std::set<std::uint32_t> parents;
                    for (const ParseItemSetEntry& entry : itemSet.entries)
                        if (!entry.item.IsAugmented() && entry.item.stepIndex > 0)
                            parents.insert(entry.item.variableIndex);
                    std::string names;
                    for (const std::uint32_t index : parents)
                        names += (names.empty() ? "" : ", ") + syntax_.variables[index].name;
                    throw CompileError("Extra rules must have unambiguous endings. Conflicting rules: " + names);
                }
            }
            else {
                for (const auto& [terminal, extraStateId] : nonTerminalExtraStates_)
                    state.terminalEntries.try_emplace(terminal, ParseTableEntry{.actions = {ParseAction::Shift(extraStateId)}, .reusable = true});
                for (const Symbol extra : syntax_.extraSymbols) {
                    if (extra.IsNonTerminal())
                        state.nonterminalEntries[extra] = GotoAction{.shiftExtra = true};
                    else
                        state.terminalEntries.try_emplace(extra, ParseTableEntry{.actions = {ParseAction::ShiftExtra()}, .reusable = true});
                }
            }

            if (syntax_.wordToken) {
                std::optional<ReservedWordSetId> reservedWordSetId;
                for (const ParseItemSetEntry& entry : itemSet.entries) {
                    std::optional<ReservedWordSetId> candidate;
                    if (const ProductionStep* next = entry.item.Step()) {
                        if (next->symbol == *syntax_.wordToken)
                            candidate = next->reservedWordSetId;
                    }
                    else if (entry.lookaheads.Contains(*syntax_.wordToken)) {
                        candidate = entry.followingReservedWordSet;
                    }
                    if (candidate && (!reservedWordSetId || *candidate > *reservedWordSetId))
                        reservedWordSetId = candidate;
                }
                if (reservedWordSetId)
                    state.reservedWords = syntax_.reservedWordSets[*reservedWordSetId];
            }
        }

        void HandleConflict(const ParseItemSet& itemSet, ParseStateId stateId, const std::vector<Symbol>& precedingSymbols,
                            const std::vector<AuxiliarySymbolInfo>& precedingAuxiliarySymbols, Symbol conflictingLookahead, const ReductionInfo& reductionInfo) {
            ParseTableEntry* entry = &table_.states[stateId].terminalEntries.at(conflictingLookahead);

            bool                                        consideredAssociativity = false;
            std::vector<std::pair<Precedence, Symbol>>  shiftPrecedence;
            std::set<const ParseItem*, ItemPointerLess> conflictingItems;
            for (const ParseItemSetEntry& setEntry : itemSet.entries) {
                const ParseItem& item = setEntry.item;
                if (const ProductionStep* step = item.Step()) {
                    if (item.stepIndex > 0 && itemSetBuilder_.FirstSet(step->symbol).Contains(conflictingLookahead)) {
                        if (item.variableIndex != kAugmentedVariable)
                            conflictingItems.insert(&item);
                        const std::pair<Precedence, Symbol> p{item.ItemPrecedence(), Symbol::NonTerminal(item.variableIndex)};
                        const auto                          pos = std::lower_bound(shiftPrecedence.begin(), shiftPrecedence.end(), p);
                        if (pos == shiftPrecedence.end() || *pos != p)
                            shiftPrecedence.insert(pos, p);
                    }
                }
                else if (setEntry.lookaheads.Contains(conflictingLookahead) && item.variableIndex != kAugmentedVariable) {
                    conflictingItems.insert(&item);
                }
            }

            if (entry->actions.back().kind == ParseAction::Kind::Shift) {
                // All items sharing one auxiliary parent: the intended
                // ambiguity of a repeat, kept and flagged.
                const std::uint32_t conflictingVariable = (*conflictingItems.begin())->variableIndex;
                if (syntax_.variables[conflictingVariable].IsAuxiliary() &&
                    std::all_of(conflictingItems.begin(), conflictingItems.end(), [&](const ParseItem* i) { return i->variableIndex == conflictingVariable; })) {
                    entry->actions.back().isRepetition = true;
                    return;
                }

                bool shiftIsLess = false, shiftIsMore = false;
                for (const auto& [precedence, symbol] : shiftPrecedence) {
                    const std::strong_ordering c = ComparePrecedence(precedence, {symbol}, reductionInfo.precedence, reductionInfo.symbols);
                    if (c == std::strong_ordering::greater)
                        shiftIsMore = true;
                    else if (c == std::strong_ordering::less)
                        shiftIsLess = true;
                }

                if (shiftIsMore && !shiftIsLess) {
                    entry->actions.erase(entry->actions.begin(), entry->actions.end() - 1);
                }
                else if (shiftIsLess && !shiftIsMore) {
                    entry->actions.pop_back();
                    RetainDone(conflictingItems);
                }
                else if (!shiftIsLess && !shiftIsMore) {
                    consideredAssociativity = true;
                    if (reductionInfo.hasLeftAssoc && !reductionInfo.hasNonAssoc && !reductionInfo.hasRightAssoc) {
                        entry->actions.pop_back();
                        RetainDone(conflictingItems);
                    }
                    else if (!reductionInfo.hasLeftAssoc && !reductionInfo.hasNonAssoc && reductionInfo.hasRightAssoc) {
                        entry->actions.erase(entry->actions.begin(), entry->actions.end() - 1);
                    }
                }
            }

            entry = &table_.states[stateId].terminalEntries.at(conflictingLookahead);
            if (entry->actions.size() == 1)
                return;

            std::vector<Symbol> actualConflict;
            for (const ParseItem* item : conflictingItems) {
                const Symbol symbol = Symbol::NonTerminal(item->variableIndex);
                if (syntax_.variables[symbol.index].IsAuxiliary()) {
                    const std::vector<Symbol>* parents = nullptr;
                    for (auto it = precedingAuxiliarySymbols.rbegin(); it != precedingAuxiliarySymbols.rend(); ++it) {
                        if (it->auxiliarySymbol == symbol) {
                            parents = &it->parentSymbols;
                            break;
                        }
                    }
                    if (parents == nullptr)
                        throw CompileError("internal: auxiliary symbol " + SymbolName(symbol) + " has no recorded parents");
                    actualConflict.insert(actualConflict.end(), parents->begin(), parents->end());
                }
                else {
                    actualConflict.push_back(symbol);
                }
            }
            std::sort(actualConflict.begin(), actualConflict.end());
            actualConflict.erase(std::unique(actualConflict.begin(), actualConflict.end()), actualConflict.end());

            if (std::find(syntax_.expectedConflicts.begin(), syntax_.expectedConflicts.end(), actualConflict) != syntax_.expectedConflicts.end()) {
                actualConflicts_.erase(actualConflict);
                return;
            }

            throw CompileError(DescribeConflict(precedingSymbols, conflictingLookahead, conflictingItems, actualConflict, consideredAssociativity));
        }

        struct ItemPointerLess {
            bool operator()(const ParseItem* a, const ParseItem* b) const {
                return a->Order(*b) == std::strong_ordering::less;
            }
        };

        static void RetainDone(std::set<const ParseItem*, ItemPointerLess>& items) {
            for (auto it = items.begin(); it != items.end();)
                it = (*it)->IsDone() ? std::next(it) : items.erase(it);
        }

        std::string DescribeConflict(const std::vector<Symbol>& precedingSymbols, Symbol conflictingLookahead, const std::set<const ParseItem*, ItemPointerLess>& conflictingItems,
                                     const std::vector<Symbol>& actualConflict, bool consideredAssociativity) const {
            std::string out = "Unresolved conflict for symbol sequence:\n\n";
            for (const Symbol symbol : precedingSymbols)
                out += "  " + SymbolName(symbol);
            out += "  •  " + SymbolName(conflictingLookahead) + "  …\n\nPossible interpretations:\n\n";

            std::vector<std::string> interpretations;
            for (const ParseItem* item : conflictingItems) {
                std::string line;
                for (std::size_t i = 0; i + item->stepIndex < precedingSymbols.size(); ++i)
                    line += "  " + SymbolName(precedingSymbols[i]);
                line += "  (" + syntax_.variables[item->variableIndex].name;
                for (std::size_t i = 0; i < item->production->steps.size(); ++i) {
                    if (i == item->stepIndex)
                        line += "  •";
                    line += "  " + SymbolName(item->production->steps[i].symbol);
                }
                line += ")";
                if (item->IsDone())
                    line += "  •  " + SymbolName(conflictingLookahead) + "  …";
                const Precedence& precedence = item->ItemPrecedence();
                if (!precedence.IsNone()) {
                    line += "  (precedence: " + precedence.ToString();
                    if (const auto assoc = item->ItemAssociativity())
                        line += std::string(", associativity: ") + (*assoc == Associativity::Left ? "Left" : "Right");
                    line += ")";
                }
                interpretations.push_back(std::move(line));
            }
            std::sort(interpretations.begin(), interpretations.end());
            for (std::size_t i = 0; i < interpretations.size(); ++i)
                out += "  " + std::to_string(i + 1) + ":" + interpretations[i] + "\n";

            out += "\nPossible resolutions:\n\n";
            std::size_t              n = 1;
            std::vector<std::string> shiftNames, reduceNames;
            for (const ParseItem* item : conflictingItems) {
                const std::string         name = SymbolName(Symbol::NonTerminal(item->variableIndex));
                std::vector<std::string>& into = item->IsDone() ? reduceNames : shiftNames;
                if (std::find(into.begin(), into.end(), name) == into.end())
                    into.push_back(name);
            }
            if (actualConflict.size() > 1) {
                if (!shiftNames.empty()) {
                    out += "  " + std::to_string(n++) + ":  Specify a higher precedence in ";
                    for (std::size_t i = 0; i < shiftNames.size(); ++i)
                        out += (i > 0 ? " and " : "") + ("`" + shiftNames[i] + "`");
                    out += " than in the other rules.\n";
                }
                for (const std::string& name : reduceNames)
                    out += "  " + std::to_string(n++) + ":  Specify a higher precedence in `" + name + "` than in the other rules.\n";
            }
            if (consideredAssociativity) {
                out += "  " + std::to_string(n++) + ":  Specify a left or right associativity in ";
                for (std::size_t i = 0; i < reduceNames.size(); ++i)
                    out += (i > 0 ? ", " : "") + ("`" + reduceNames[i] + "`");
                out += "\n";
            }
            out += "  " + std::to_string(n) + ":  Add a conflict for these rules: ";
            for (std::size_t i = 0; i < actualConflict.size(); ++i)
                out += (i > 0 ? ", " : "") + ("`" + SymbolName(actualConflict[i]) + "`");
            return out + "\n";
        }

        std::strong_ordering ComparePrecedence(const Precedence& left, const std::vector<Symbol>& leftSymbols, const Precedence& right, const std::vector<Symbol>& rightSymbols) const {
            const auto entryMatches = [&](const PrecedenceEntry& entry, const Precedence& precedence, const std::vector<Symbol>& symbols) {
                if (entry.kind == PrecedenceEntry::Kind::Name)
                    return precedence.kind == Precedence::Kind::Name && precedence.name == entry.text;
                return std::any_of(symbols.begin(), symbols.end(), [&](Symbol s) { return syntax_.variables[s.index].name == entry.text; });
            };

            const bool leftInt  = left.kind == Precedence::Kind::Integer;
            const bool rightInt = right.kind == Precedence::Kind::Integer;
            if (leftInt && rightInt && (left.value != 0 || right.value != 0))
                return left.value <=> right.value;
            if (leftInt && right.IsNone() && left.value != 0)
                return left.value <=> 0;
            if (left.IsNone() && rightInt && right.value != 0)
                return 0 <=> right.value;

            for (const std::vector<PrecedenceEntry>& list : syntax_.precedenceOrderings) {
                bool sawLeft = false, sawRight = false;
                for (const PrecedenceEntry& entry : list) {
                    const bool matchesLeft  = entryMatches(entry, left, leftSymbols);
                    const bool matchesRight = entryMatches(entry, right, rightSymbols);
                    if (matchesLeft) {
                        sawLeft = true;
                        if (sawRight)
                            return std::strong_ordering::less;
                    }
                    else if (matchesRight) {
                        sawRight = true;
                        if (sawLeft)
                            return std::strong_ordering::greater;
                    }
                }
            }
            return std::strong_ordering::equal;
        }

        AuxiliarySymbolInfo AuxiliaryNodeInfo(const ParseItemSet& itemSet, Symbol symbol) const {
            AuxiliarySymbolInfo info{.auxiliarySymbol = symbol};
            for (const ParseItemSetEntry& entry : itemSet.entries) {
                const std::uint32_t variableIndex = entry.item.variableIndex;
                if (entry.item.NextSymbol() == symbol && variableIndex != kAugmentedVariable && !syntax_.variables[variableIndex].IsAuxiliary())
                    info.parentSymbols.push_back(Symbol::NonTerminal(variableIndex));
            }
            return info;
        }

        ProductionInfoId GetProductionId(const ParseItem& item) {
            ProductionInfo info;
            for (std::size_t i = 0; i < item.production->steps.size(); ++i) {
                const ProductionStep& step = item.production->steps[i];
                info.aliasSequence.push_back(step.alias);
                if (step.fieldName)
                    info.fieldMap[*step.fieldName].push_back({.index = i, .inherited = false});
                if (step.symbol.IsNonTerminal() && !IsVisible(syntax_.variables[step.symbol.index].kind))
                    for (const auto& [fieldName, _] : variableInfo_[step.symbol.index].fields)
                        info.fieldMap[fieldName].push_back({.index = i, .inherited = true});
            }
            while (!info.aliasSequence.empty() && !info.aliasSequence.back())
                info.aliasSequence.pop_back();
            if (item.production->steps.size() > table_.maxAliasedProductionLength)
                table_.maxAliasedProductionLength = item.production->steps.size();
            const auto found = std::find(table_.productionInfos.begin(), table_.productionInfos.end(), info);
            if (found != table_.productionInfos.end())
                return static_cast<ProductionInfoId>(found - table_.productionInfos.begin());
            table_.productionInfos.push_back(std::move(info));
            return table_.productionInfos.size() - 1;
        }

        std::string SymbolName(Symbol symbol) const {
            switch (symbol.kind) {
                case SymbolType::End:
                case SymbolType::EndOfNonTerminalExtra:
                    return "EOF";
                case SymbolType::External:
                    return syntax_.externalTokens[symbol.index].name;
                case SymbolType::NonTerminal:
                    return syntax_.variables[symbol.index].name;
                case SymbolType::Terminal: {
                    const LexicalVariable& variable = lexical_.variables[symbol.index];
                    return variable.kind == VariableType::Named ? variable.name : "'" + variable.name + "'";
                }
            }
            return "?";
        }
    };

    // --- minimization ---------------------------------------------------------------

    class Minimizer {
      public:
        Minimizer(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const AliasMap& defaultAliases, const TokenConflictMap& conflicts, const TokenSet& keywords) : table_(table), syntax_(syntax), lexical_(lexical), defaultAliases_(defaultAliases), conflicts_(conflicts), keywords_(keywords) {
        }

        void Run() {
            MergeCompatibleStates();
            RemoveUnitReductions();
            RemoveUnusedStates();
            ReorderStatesByDescendingSize();
        }

      private:
        ParseTable&             table_;
        const SyntaxGrammar&    syntax_;
        const LexicalGrammar&   lexical_;
        const AliasMap&         defaultAliases_;
        const TokenConflictMap& conflicts_;
        const TokenSet&         keywords_;

        void RemoveUnitReductions() {
            std::set<Symbol> aliasedSymbols;
            for (const SyntaxVariable& variable : syntax_.variables)
                for (const Production& production : variable.productions)
                    for (const ProductionStep& step : production.steps)
                        if (step.alias)
                            aliasedSymbols.insert(step.symbol);

            std::map<ParseStateId, Symbol> unitReductionSymbolsByState;
            for (std::size_t i = 0; i < table_.states.size(); ++i) {
                const ParseState&     state              = table_.states[i];
                bool                  onlyUnitReductions = true;
                std::optional<Symbol> unitReductionSymbol;
                for (const auto& [_, entry] : state.terminalEntries) {
                    for (const ParseAction& action : entry.actions) {
                        if (action.kind == ParseAction::Kind::ShiftExtra)
                            continue;
                        if (action.kind == ParseAction::Kind::Reduce && action.childCount == 1 && action.productionId == 0 && defaultAliases_.count(action.symbol) == 0 &&
                            std::find(syntax_.supertypeSymbols.begin(), syntax_.supertypeSymbols.end(), action.symbol) == syntax_.supertypeSymbols.end() &&
                            std::find(syntax_.extraSymbols.begin(), syntax_.extraSymbols.end(), action.symbol) == syntax_.extraSymbols.end() &&
                            aliasedSymbols.count(action.symbol) == 0 && syntax_.variables[action.symbol.index].kind != VariableType::Named &&
                            (!unitReductionSymbol || *unitReductionSymbol == action.symbol)) {
                            unitReductionSymbol = action.symbol;
                            continue;
                        }
                        onlyUnitReductions = false;
                        break;
                    }
                    if (!onlyUnitReductions)
                        break;
                }
                if (unitReductionSymbol && onlyUnitReductions)
                    unitReductionSymbolsByState.emplace(i, *unitReductionSymbol);
            }

            for (ParseState& state : table_.states) {
                bool done = false;
                while (!done) {
                    done = true;
                    state.UpdateReferencedStates([&](ParseStateId otherStateId, const ParseState& current) {
                        const auto found = unitReductionSymbolsByState.find(otherStateId);
                        if (found == unitReductionSymbolsByState.end())
                            return otherStateId;
                        done             = false;
                        const auto entry = current.nonterminalEntries.find(found->second);
                        if (entry != current.nonterminalEntries.end() && !entry->second.shiftExtra)
                            return entry->second.state;
                        return otherStateId;
                    });
                }
            }
        }

        void MergeCompatibleStates() {
            std::size_t coreCount = 0;
            for (const ParseState& state : table_.states)
                coreCount = std::max(coreCount, state.coreId + 1);

            std::vector<std::size_t>              groupIdsByStateId;
            std::vector<std::vector<std::size_t>> stateIdsByGroupId(coreCount);
            for (std::size_t i = 0; i < table_.states.size(); ++i) {
                stateIdsByGroupId[table_.states[i].coreId].push_back(i);
                groupIdsByStateId.push_back(table_.states[i].coreId);
            }

            SplitStateIdGroups(table_.states, stateIdsByGroupId, groupIdsByStateId, 0,
                               [&](const ParseState& l, const ParseState& r, const std::vector<std::size_t>& groups) { return StatesConflict(l, r, groups); });
            while (SplitStateIdGroups(table_.states, stateIdsByGroupId, groupIdsByStateId, 0,
                                      [&](const ParseState& l, const ParseState& r, const std::vector<std::size_t>& groups) { return StateSuccessorsDiffer(l, r, groups); })) {
            }

            const auto groupContaining = [&](std::size_t stateId) {
                for (std::size_t g = 0; g < stateIdsByGroupId.size(); ++g)
                    if (std::find(stateIdsByGroupId[g].begin(), stateIdsByGroupId[g].end(), stateId) != stateIdsByGroupId[g].end())
                        return g;
                throw CompileError("internal: state group missing");
            };
            std::swap(stateIdsByGroupId[groupContaining(0)], stateIdsByGroupId[0]);
            std::swap(stateIdsByGroupId[groupContaining(1)], stateIdsByGroupId[1]);
            for (std::size_t g = 0; g < stateIdsByGroupId.size(); ++g)
                for (const std::size_t id : stateIdsByGroupId[g])
                    groupIdsByStateId[id] = g;

            std::vector<ParseState> newStates;
            for (const std::vector<std::size_t>& stateIds : stateIdsByGroupId) {
                ParseState parseState = std::move(table_.states[stateIds[0]]);
                for (std::size_t k = 1; k < stateIds.size(); ++k) {
                    ParseState other = std::move(table_.states[stateIds[k]]);
                    for (auto& [symbol, entry] : other.terminalEntries)
                        parseState.terminalEntries[symbol] = std::move(entry);
                    for (auto& [symbol, action] : other.nonterminalEntries)
                        parseState.nonterminalEntries[symbol] = action;
                    parseState.reservedWords.InsertAll(other.reservedWords);
                    for (const auto& [symbol, _] : parseState.terminalEntries)
                        parseState.reservedWords.Remove(symbol);
                }
                parseState.UpdateReferencedStates([&](ParseStateId stateId, const ParseState&) { return groupIdsByStateId[stateId]; });
                newStates.push_back(std::move(parseState));
            }
            table_.states = std::move(newStates);
        }

        bool StatesConflict(const ParseState& left, const ParseState& right, const std::vector<std::size_t>& groups) const {
            for (const auto& [token, leftEntry] : left.terminalEntries) {
                if (const auto it = right.terminalEntries.find(token); it != right.terminalEntries.end()) {
                    if (EntriesConflict(token, leftEntry, it->second, groups))
                        return true;
                }
                else if (TokenConflicts(right, token)) {
                    return true;
                }
            }
            for (const auto& [token, _] : right.terminalEntries)
                if (left.terminalEntries.count(token) == 0 && TokenConflicts(left, token))
                    return true;
            return false;
        }

        bool StateSuccessorsDiffer(const ParseState& state1, const ParseState& state2, const std::vector<std::size_t>& groups) const {
            for (const auto& [token, entry1] : state1.terminalEntries) {
                const ParseAction& last1 = entry1.actions.back();
                if (last1.kind != ParseAction::Kind::Shift)
                    continue;
                if (const auto it = state2.terminalEntries.find(token); it != state2.terminalEntries.end()) {
                    const ParseAction& last2 = it->second.actions.back();
                    if (last2.kind == ParseAction::Kind::Shift && groups[last1.state] != groups[last2.state])
                        return true;
                }
            }
            for (const auto& [symbol, s1] : state1.nonterminalEntries) {
                if (const auto it = state2.nonterminalEntries.find(symbol); it != state2.nonterminalEntries.end()) {
                    const GotoAction& s2 = it->second;
                    if (s1.shiftExtra && s2.shiftExtra)
                        continue;
                    if (!s1.shiftExtra && !s2.shiftExtra) {
                        if (groups[s1.state] != groups[s2.state])
                            return true;
                        continue;
                    }
                    return true;
                }
            }
            return false;
        }

        static bool EntriesConflict(Symbol, const ParseTableEntry& entry1, const ParseTableEntry& entry2, const std::vector<std::size_t>& groups) {
            if (entry1.actions.size() != entry2.actions.size())
                return true;
            for (std::size_t i = 0; i < entry1.actions.size(); ++i) {
                const ParseAction& a1 = entry1.actions[i];
                const ParseAction& a2 = entry2.actions[i];
                if (a1.kind == ParseAction::Kind::Shift && a2.kind == ParseAction::Kind::Shift) {
                    if (groups[a1.state] == groups[a2.state] && a1.isRepetition == a2.isRepetition)
                        continue;
                    return true;
                }
                if (a1 != a2)
                    return true;
            }
            return false;
        }

        bool TokenConflicts(const ParseState& rightState, Symbol newToken) const {
            if (newToken == Symbol::EndOfNonTerminalExtra())
                return true;
            if (newToken.IsExternal())
                return true;
            if (rightState.reservedWords.Contains(newToken))
                return false;
            for (const ExternalToken& external : syntax_.externalTokens)
                if (external.correspondingInternalToken == newToken)
                    return true;
            for (const auto& [token, _] : rightState.terminalEntries) {
                if (!token.IsTerminal())
                    continue;
                if (syntax_.wordToken == token && keywords_.Contains(newToken))
                    continue;
                if (syntax_.wordToken == newToken && keywords_.Contains(token))
                    continue;
                if (conflicts_.DoesConflict(newToken.index, token.index) || conflicts_.DoesMatchSameString(newToken.index, token.index))
                    return true;
            }
            return false;
        }

        void RemoveUnusedStates() {
            std::vector<bool> used(table_.states.size(), false);
            used[0] = true;
            used[1] = true;
            for (const ParseState& state : table_.states)
                for (const ParseStateId referenced : state.ReferencedStates())
                    used[referenced] = true;

            std::vector<ParseStateId> replacement(table_.states.size());
            std::size_t               removed = 0;
            for (std::size_t i = 0; i < table_.states.size(); ++i) {
                replacement[i] = i - removed;
                if (!used[i])
                    removed++;
            }
            std::vector<ParseState> kept;
            for (std::size_t i = 0; i < table_.states.size(); ++i) {
                if (!used[i])
                    continue;
                ParseState state = std::move(table_.states[i]);
                state.UpdateReferencedStates([&](ParseStateId other, const ParseState&) { return replacement[other]; });
                kept.push_back(std::move(state));
            }
            table_.states = std::move(kept);
        }

        void ReorderStatesByDescendingSize() {
            std::vector<std::size_t> oldIdsByNewId(table_.states.size());
            for (std::size_t i = 0; i < oldIdsByNewId.size(); ++i)
                oldIdsByNewId[i] = i;
            std::stable_sort(oldIdsByNewId.begin(), oldIdsByNewId.end(), [&](std::size_t a, std::size_t b) {
                const auto key = [&](std::size_t i) -> std::int64_t {
                    if (i <= 1)
                        return static_cast<std::int64_t>(i) - 1'000'000;
                    const ParseState& state = table_.states[i];
                    return -static_cast<std::int64_t>(state.terminalEntries.size() + state.nonterminalEntries.size());
                };
                return key(a) < key(b);
            });
            std::vector<std::size_t> newIdsByOldId(oldIdsByNewId.size());
            for (std::size_t id = 0; id < oldIdsByNewId.size(); ++id)
                newIdsByOldId[oldIdsByNewId[id]] = id;

            std::vector<ParseState> reordered;
            for (const std::size_t oldId : oldIdsByNewId) {
                ParseState state = std::move(table_.states[oldId]);
                state.UpdateReferencedStates([&](ParseStateId id, const ParseState&) { return newIdsByOldId[id]; });
                reordered.push_back(std::move(state));
            }
            table_.states = std::move(reordered);
        }
    };

} // namespace

ParseTable BuildParseTable(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const InlinedProductionMap& inlines, const std::vector<VariableInfo>& variableInfo) {
    return ParseTableBuilder(syntax, lexical, ParseItemSetBuilder(syntax, lexical, inlines), variableInfo).Build();
}

std::vector<TokenSet> GetFollowingTokens(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const InlinedProductionMap& inlines) {
    const ParseItemSetBuilder builder(syntax, lexical, inlines);
    std::vector<TokenSet>     result(lexical.variables.size());
    TokenSet                  allTokens;
    for (std::size_t i = 0; i < result.size(); ++i)
        allTokens.Insert(Symbol::Terminal(static_cast<std::uint32_t>(i)));

    const auto visit = [&](const Production& production) {
        for (std::size_t i = 1; i < production.steps.size(); ++i) {
            const TokenSet& leftTokens    = builder.LastSet(production.steps[i - 1].symbol);
            const TokenSet& rightTokens   = builder.FirstSet(production.steps[i].symbol);
            const TokenSet* rightReserved = builder.ReservedFirstSet(production.steps[i].symbol);
            for (const Symbol left : leftTokens.Symbols()) {
                if (!left.IsTerminal())
                    continue;
                result[left.index].InsertAllTerminals(rightTokens);
                if (rightReserved != nullptr)
                    result[left.index].InsertAllTerminals(*rightReserved);
            }
        }
    };
    for (const SyntaxVariable& variable : syntax.variables)
        for (const Production& production : variable.productions)
            visit(production);
    for (const Production& production : inlines.productions)
        visit(production);

    for (const Symbol extra : syntax.extraSymbols) {
        if (!extra.IsTerminal())
            continue;
        for (TokenSet& entry : result)
            entry.Insert(extra);
        result[extra.index] = allTokens;
    }
    return result;
}

void MinimizeParseTable(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const AliasMap& defaultAliases, const TokenConflictMap& conflicts, const TokenSet& keywords) {
    Minimizer(table, syntax, lexical, defaultAliases, conflicts, keywords).Run();
}

} // namespace ned::editor::grammar::compile
