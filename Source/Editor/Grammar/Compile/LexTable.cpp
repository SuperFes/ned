#include "LexTable.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace ned::editor::grammar::compile {

bool LexState::Less(const LexState& other) const {
    if (acceptAction != other.acceptAction)
        return acceptAction < other.acceptAction;
    if (eofAction != other.eofAction)
        return eofAction < other.eofAction;
    for (std::size_t i = 0; i < advanceActions.size() && i < other.advanceActions.size(); ++i) {
        const auto& a = advanceActions[i];
        const auto& b = other.advanceActions[i];
        if (a.first != b.first)
            return a.first < b.first;
        if (a.second != b.second)
            return a.second < b.second;
    }
    return advanceActions.size() < other.advanceActions.size();
}

// --- TokenConflictMap ------------------------------------------------------------

namespace {

    std::vector<CharacterSet> StartingChars(NfaCursor& cursor, const LexicalGrammar& grammar) {
        std::vector<CharacterSet> result;
        for (const LexicalVariable& variable : grammar.variables) {
            cursor.Reset({variable.startState});
            CharacterSet all;
            for (const auto& [chars, _] : cursor.TransitionChars())
                all = all.Add(*chars);
            result.push_back(std::move(all));
        }
        return result;
    }

    std::vector<CharacterSet> FollowingChars(const std::vector<CharacterSet>& startingChars, const std::vector<TokenSet>& followingTokens) {
        std::vector<CharacterSet> result;
        for (const TokenSet& tokens : followingTokens) {
            CharacterSet chars;
            for (const Symbol token : tokens.Symbols())
                if (token.IsTerminal())
                    chars = chars.Add(startingChars[token.index]);
            result.push_back(std::move(chars));
        }
        return result;
    }

} // namespace

TokenConflictMap::TokenConflictMap(const LexicalGrammar& grammar, std::vector<TokenSet> followingTokens) : n_(grammar.variables.size()), followingTokens_(std::move(followingTokens)) {
    NfaCursor cursor(grammar.nfa, {});
    startingChars_  = StartingChars(cursor, grammar);
    followingChars_ = FollowingChars(startingChars_, followingTokens_);
    statusMatrix_.resize(n_ * n_);

    for (std::size_t i = 0; i < n_; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            Status                                  resultI, resultJ;
            std::set<std::vector<std::uint32_t>>    visited;
            std::vector<std::vector<std::uint32_t>> queue = {{grammar.variables[i].startState, grammar.variables[j].startState}};
            while (!queue.empty()) {
                std::vector<std::uint32_t> stateSet = std::move(queue.back());
                queue.pop_back();

                const std::vector<std::size_t> live = grammar.VariableIndicesForNfaStates(stateSet);
                if (live.size() == 1) {
                    if (live.front() == i)
                        resultI.matchesDifferentString = true;
                    else
                        resultJ.matchesDifferentString = true;
                    continue;
                }

                cursor.Reset(stateSet);
                bool withinSeparator = false;
                for (const auto& [_, sep] : cursor.TransitionChars())
                    withinSeparator |= sep;

                std::optional<std::pair<std::size_t, int>> completion;
                for (const auto& [id, precedence] : cursor.Completions()) {
                    if (withinSeparator) {
                        if (id == i)
                            resultI.doesMatchSeparators = true;
                        else
                            resultJ.doesMatchSeparators = true;
                    }
                    if (completion) {
                        if (id == completion->first)
                            continue;
                        std::size_t preferredId;
                        if (PreferToken(grammar, {completion->second, completion->first}, {precedence, id})) {
                            preferredId = completion->first;
                        }
                        else {
                            preferredId = id;
                            completion  = {id, precedence};
                        }
                        if (preferredId == i)
                            resultI.matchesSameString = true;
                        else
                            resultJ.matchesSameString = true;
                    }
                    else {
                        completion = {id, precedence};
                    }
                }

                for (NfaTransition& transition : cursor.Transitions()) {
                    bool canAdvance = true;
                    if (completion) {
                        std::optional<std::size_t> advancedId;
                        bool                       successorContainsCompletedId = false;
                        for (const std::size_t variableId : grammar.VariableIndicesForNfaStates(transition.states)) {
                            if (variableId == completion->first) {
                                successorContainsCompletedId = true;
                                break;
                            }
                            advancedId = variableId;
                        }
                        if (advancedId && !successorContainsCompletedId) {
                            if (PreferTransition(grammar, transition, completion->first, completion->second, withinSeparator)) {
                                canAdvance = true;
                                if (*advancedId == i) {
                                    resultI.doesMatchContinuation = true;
                                    if (transition.characters.DoesIntersect(followingChars_[j]))
                                        resultI.doesMatchValidContinuation = true;
                                }
                                else {
                                    resultJ.doesMatchContinuation = true;
                                    if (transition.characters.DoesIntersect(followingChars_[i]))
                                        resultJ.doesMatchValidContinuation = true;
                                }
                            }
                            else if (completion->first == i) {
                                resultI.matchesPrefix = true;
                            }
                            else {
                                resultJ.matchesPrefix = true;
                            }
                        }
                    }
                    if (canAdvance && visited.insert(transition.states).second)
                        queue.push_back(std::move(transition.states));
                }
            }
            statusMatrix_[n_ * i + j] = resultI;
            statusMatrix_[n_ * j + i] = resultJ;
        }
    }
}

bool TokenConflictMap::HasSameConflictStatus(std::size_t a, std::size_t b, std::size_t other) const {
    return At(a, other) == At(b, other);
}

bool TokenConflictMap::DoesMatchDifferentString(std::size_t i, std::size_t j) const {
    return At(i, j).matchesDifferentString;
}

bool TokenConflictMap::DoesMatchSameString(std::size_t i, std::size_t j) const {
    return At(i, j).matchesSameString;
}

bool TokenConflictMap::DoesConflict(std::size_t i, std::size_t j) const {
    const Status& s = At(i, j);
    return s.doesMatchValidContinuation || s.doesMatchSeparators || s.matchesSameString;
}

bool TokenConflictMap::DoesMatchPrefix(std::size_t i, std::size_t j) const {
    return At(i, j).matchesPrefix;
}

bool TokenConflictMap::DoesMatchShorterOrLonger(std::size_t i, std::size_t j) const {
    const Status& s = At(i, j);
    const Status& r = At(j, i);
    return (s.doesMatchValidContinuation || s.doesMatchSeparators) && !r.doesMatchSeparators;
}

bool TokenConflictMap::DoesOverlap(std::size_t i, std::size_t j) const {
    const Status& s = At(i, j);
    return s.doesMatchSeparators || s.matchesPrefix || s.matchesSameString || s.doesMatchContinuation;
}

bool TokenConflictMap::PreferToken(const LexicalGrammar& grammar, std::pair<int, std::size_t> left, std::pair<int, std::size_t> right) {
    if (left.first != right.first)
        return left.first > right.first;
    const int leftImplicit  = grammar.variables[left.second].implicitPrecedence;
    const int rightImplicit = grammar.variables[right.second].implicitPrecedence;
    if (leftImplicit != rightImplicit)
        return leftImplicit > rightImplicit;
    return left.second < right.second;
}

bool TokenConflictMap::PreferTransition(const LexicalGrammar& grammar, const NfaTransition& t, std::size_t completedId, int completedPrecedence, bool hasSeparatorTransitions) {
    if (t.precedence < completedPrecedence)
        return false;
    if (t.precedence == completedPrecedence) {
        if (t.isSeparator)
            return false;
        if (hasSeparatorTransitions) {
            const std::vector<std::size_t> variables = grammar.VariableIndicesForNfaStates(t.states);
            if (std::find(variables.begin(), variables.end(), completedId) == variables.end())
                return false;
        }
    }
    return true;
}

// --- CoincidentTokenIndex -----------------------------------------------------------

CoincidentTokenIndex::CoincidentTokenIndex(const ParseTable& table, const LexicalGrammar& grammar) : n_(grammar.variables.size()), entries_(n_ * n_) {
    std::vector<std::uint32_t> terminals;
    for (std::size_t i = 0; i < table.states.size(); ++i) {
        terminals.clear();
        for (const auto& [symbol, _] : table.states[i].terminalEntries)
            if (symbol.IsTerminal())
                terminals.push_back(symbol.index);
        for (const std::uint32_t a : terminals) {
            for (const std::uint32_t b : terminals) {
                std::vector<ParseStateId>& entry = entries_[Index(a, b)];
                if (entry.empty() || entry.back() != i)
                    entry.push_back(i);
            }
        }
    }
}

const std::vector<ParseStateId>& CoincidentTokenIndex::StatesWith(Symbol a, Symbol b) const {
    return entries_[Index(a.index, b.index)];
}

bool CoincidentTokenIndex::Contains(Symbol a, Symbol b) const {
    return !entries_[Index(a.index, b.index)].empty();
}

// --- BuildLexTable ----------------------------------------------------------------

namespace {

    class LexTableBuilder {
      public:
        LexTable table;

        explicit LexTableBuilder(const LexicalGrammar& grammar) : grammar_(grammar), cursor_(grammar.nfa, {}) {
        }

        void Reset() {
            table = {};
            queue_.clear();
            stateIdsByNfaStateSet_.clear();
        }

        LexStateId AddStateForTokens(const TokenSet& tokens) {
            bool                       eofValid = false;
            std::vector<std::uint32_t> nfaStates;
            for (const Symbol token : tokens.Symbols()) {
                if (token.IsTerminal())
                    nfaStates.push_back(grammar_.variables[token.index].startState);
                else
                    eofValid = true;
            }
            const LexStateId stateId = AddState(std::move(nfaStates), eofValid).first;
            while (!queue_.empty()) {
                QueueEntry entry = std::move(queue_.front());
                queue_.pop_front();
                PopulateState(entry.stateId, std::move(entry.nfaStates), entry.eofValid);
            }
            return stateId;
        }

      private:
        struct QueueEntry {
            LexStateId                 stateId;
            std::vector<std::uint32_t> nfaStates;
            bool                       eofValid;
        };

        const LexicalGrammar&                                             grammar_;
        NfaCursor                                                         cursor_;
        std::deque<QueueEntry>                                            queue_;
        std::map<std::pair<std::vector<std::uint32_t>, bool>, LexStateId> stateIdsByNfaStateSet_;

        std::pair<LexStateId, bool> AddState(std::vector<std::uint32_t> nfaStates, bool eofValid) {
            cursor_.Reset(std::move(nfaStates));
            const std::pair<std::vector<std::uint32_t>, bool> key{cursor_.StateIds(), eofValid};
            if (const auto it = stateIdsByNfaStateSet_.find(key); it != stateIdsByNfaStateSet_.end())
                return {it->second, false};
            const LexStateId stateId = table.states.size();
            table.states.emplace_back();
            queue_.push_back({stateId, key.first, eofValid});
            stateIdsByNfaStateSet_.emplace(key, stateId);
            return {stateId, true};
        }

        void PopulateState(LexStateId stateId, std::vector<std::uint32_t> nfaStates, bool eofValid) {
            cursor_.ForceReset(std::move(nfaStates));

            std::optional<std::pair<std::size_t, int>> completion;
            for (const auto& [id, precedence] : cursor_.Completions()) {
                if (completion && TokenConflictMap::PreferToken(grammar_, {completion->second, completion->first}, {precedence, id}))
                    continue;
                completion = {id, precedence};
            }

            const std::vector<NfaTransition> transitions = cursor_.Transitions();
            bool                             hasSep      = false;
            for (const auto& [_, sep] : cursor_.TransitionChars())
                hasSep |= sep;

            if (eofValid) {
                const LexStateId next           = AddState({}, false).first;
                table.states[stateId].eofAction = AdvanceAction{.state = next, .inMainToken = true};
            }

            for (const NfaTransition& transition : transitions) {
                if (completion && !TokenConflictMap::PreferTransition(grammar_, transition, completion->first, completion->second, hasSep))
                    continue;
                const LexStateId next = AddState(transition.states, eofValid && transition.isSeparator).first;
                table.states[stateId].advanceActions.emplace_back(transition.characters, AdvanceAction{.state = next, .inMainToken = !transition.isSeparator});
            }

            if (completion)
                table.states[stateId].acceptAction = Symbol::Terminal(static_cast<std::uint32_t>(completion->first));
            else if (cursor_.StateIds().empty())
                table.states[stateId].acceptAction = Symbol::End();
        }
    };

    bool MergeTokenSet(TokenSet& tokens, const TokenSet& other, const LexicalGrammar& grammar, const TokenConflictMap& conflicts, const CoincidentTokenIndex& coincident) {
        for (std::size_t i = 0; i < grammar.variables.size(); ++i) {
            const Symbol    symbol   = Symbol::Terminal(static_cast<std::uint32_t>(i));
            const bool      inTokens = tokens.ContainsTerminal(static_cast<std::uint32_t>(i));
            const bool      inOther  = other.ContainsTerminal(static_cast<std::uint32_t>(i));
            const TokenSet* without;
            if (inTokens && !inOther)
                without = &other;
            else if (!inTokens && inOther)
                without = &tokens;
            else
                continue;
            bool mergeable = true;
            without->ForEachTerminal([&](Symbol existing) {
                if (!mergeable)
                    return;
                if (conflicts.DoesConflict(i, existing.index) || conflicts.DoesMatchPrefix(i, existing.index))
                    mergeable = false;
                else if (!coincident.Contains(symbol, existing) && (conflicts.DoesOverlap(existing.index, i) || conflicts.DoesOverlap(i, existing.index)))
                    mergeable = false;
            });
            if (!mergeable)
                return false;
        }
        tokens.InsertAll(other);
        return true;
    }

    struct LexSignature {
        bool                                       isError;
        std::optional<Symbol>                      accept;
        bool                                       hasEof;
        std::vector<std::pair<CharacterSet, bool>> actions;

        bool operator<(const LexSignature& other) const {
            if (isError != other.isError)
                return isError < other.isError;
            if (accept != other.accept)
                return accept < other.accept;
            if (hasEof != other.hasEof)
                return hasEof < other.hasEof;
            for (std::size_t i = 0; i < actions.size() && i < other.actions.size(); ++i) {
                if (actions[i].first != other.actions[i].first)
                    return actions[i].first < other.actions[i].first;
                if (actions[i].second != other.actions[i].second)
                    return actions[i].second < other.actions[i].second;
            }
            return actions.size() < other.actions.size();
        }
    };

    void MinimizeLexTable(LexTable& table, ParseTable& parseTable) {
        std::map<LexSignature, std::vector<std::size_t>> byLexSignature;
        for (std::size_t i = 0; i < table.states.size(); ++i) {
            const LexState& state = table.states[i];
            LexSignature    signature{.isError = i == 0, .accept = state.acceptAction, .hasEof = state.eofAction.has_value()};
            for (const auto& [chars, action] : state.advanceActions)
                signature.actions.emplace_back(chars, action.inMainToken);
            byLexSignature[std::move(signature)].push_back(i);
        }
        std::vector<std::vector<std::size_t>> stateIdsByGroupId;
        for (auto& [_, ids] : byLexSignature)
            stateIdsByGroupId.push_back(std::move(ids));
        std::sort(stateIdsByGroupId.begin(), stateIdsByGroupId.end());
        const auto errorGroup = std::find_if(stateIdsByGroupId.begin(), stateIdsByGroupId.end(),
                                             [](const std::vector<std::size_t>& g) { return std::find(g.begin(), g.end(), std::size_t{0}) != g.end(); });
        std::iter_swap(errorGroup, stateIdsByGroupId.begin());

        std::vector<std::size_t> groupIdsByStateId(table.states.size(), 0);
        for (std::size_t g = 0; g < stateIdsByGroupId.size(); ++g)
            for (const std::size_t id : stateIdsByGroupId[g])
                groupIdsByStateId[id] = g;

        while (SplitStateIdGroups(table.states, stateIdsByGroupId, groupIdsByStateId, 1, [](const LexState& left, const LexState& right, const std::vector<std::size_t>& groups) {
            for (std::size_t i = 0; i < left.advanceActions.size() && i < right.advanceActions.size(); ++i)
                if (groups[left.advanceActions[i].second.state] != groups[right.advanceActions[i].second.state])
                    return true;
            return false;
        })) {
        }

        std::vector<LexState> newStates;
        for (const std::vector<std::size_t>& stateIds : stateIdsByGroupId) {
            LexState state = std::move(table.states[stateIds[0]]);
            for (auto& [_, action] : state.advanceActions)
                action.state = groupIdsByStateId[action.state];
            if (state.eofAction)
                state.eofAction->state = groupIdsByStateId[state.eofAction->state];
            newStates.push_back(std::move(state));
        }
        for (ParseState& state : parseTable.states)
            state.lexStateId = groupIdsByStateId[state.lexStateId];
        table.states = std::move(newStates);
    }

    void SortStates(LexTable& table, ParseTable& parseTable) {
        std::vector<std::size_t> oldIdsByNewId(table.states.size());
        for (std::size_t i = 0; i < oldIdsByNewId.size(); ++i)
            oldIdsByNewId[i] = i;
        if (oldIdsByNewId.size() > 1)
            std::stable_sort(oldIdsByNewId.begin() + 1, oldIdsByNewId.end(), [&](std::size_t a, std::size_t b) { return table.states[a].Less(table.states[b]); });
        std::vector<std::size_t> newIdsByOldId(oldIdsByNewId.size());
        for (std::size_t id = 0; id < oldIdsByNewId.size(); ++id)
            newIdsByOldId[oldIdsByNewId[id]] = id;

        std::vector<LexState> reordered;
        for (const std::size_t oldId : oldIdsByNewId) {
            LexState state = std::move(table.states[oldId]);
            for (auto& [_, action] : state.advanceActions)
                action.state = newIdsByOldId[action.state];
            if (state.eofAction)
                state.eofAction->state = newIdsByOldId[state.eofAction->state];
            reordered.push_back(std::move(state));
        }
        for (ParseState& state : parseTable.states)
            state.lexStateId = newIdsByOldId[state.lexStateId];
        table.states = std::move(reordered);
    }

} // namespace

LexTables BuildLexTable(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const TokenSet& keywords, const CoincidentTokenIndex& coincident,
                        const TokenConflictMap& conflicts) {
    LexTables result;
    if (syntax.wordToken) {
        LexTableBuilder builder(lexical);
        builder.AddStateForTokens(keywords);
        result.keyword = std::move(builder.table);
    }

    std::vector<std::pair<TokenSet, std::vector<ParseStateId>>> parseStateIdsByTokenSet;
    for (std::size_t i = 0; i < table.states.size(); ++i) {
        const ParseState& state = table.states[i];
        TokenSet          tokens;
        const auto        consider = [&](Symbol token) {
            if (token.IsTerminal())
                tokens.Insert(keywords.Contains(token) ? *syntax.wordToken : token);
            else if (token.IsEof())
                tokens.Insert(token);
        };
        for (const auto& [token, _] : state.terminalEntries)
            consider(token);
        for (const Symbol token : state.reservedWords.Symbols())
            consider(token);

        bool merged = false;
        for (auto& [existing, ids] : parseStateIdsByTokenSet) {
            if (MergeTokenSet(existing, tokens, lexical, conflicts, coincident)) {
                merged = true;
                ids.push_back(i);
                break;
            }
        }
        if (!merged)
            parseStateIdsByTokenSet.emplace_back(std::move(tokens), std::vector<ParseStateId>{i});
    }

    LexTableBuilder builder(lexical);
    for (const auto& [tokens, ids] : parseStateIdsByTokenSet) {
        const LexStateId lexStateId = builder.AddStateForTokens(tokens);
        for (const ParseStateId id : ids)
            table.states[id].lexStateId = lexStateId;
    }
    result.main = std::move(builder.table);
    MinimizeLexTable(result.main, table);
    SortStates(result.main, table);

    const auto known = [&](const CharacterSet& set) {
        for (const auto& [_, existing] : result.largeCharacterSets)
            if (existing == set)
                return true;
        return false;
    };
    for (std::size_t i = 0; i < lexical.variables.size(); ++i) {
        const Symbol symbol = Symbol::Terminal(static_cast<std::uint32_t>(i));
        builder.Reset();
        builder.AddStateForTokens(TokenSet::Of({symbol}));
        for (const LexState& state : builder.table.states) {
            CharacterSet characters;
            for (const auto& [chars, action] : state.advanceActions) {
                if (action.inMainToken) {
                    characters = characters.Add(chars);
                    continue;
                }
                if (chars.RangeCount() > kLargeCharacterRangeCount && !known(chars))
                    result.largeCharacterSets.emplace_back(std::nullopt, chars);
            }
            if (characters.RangeCount() > kLargeCharacterRangeCount && !known(characters))
                result.largeCharacterSets.emplace_back(symbol, std::move(characters));
        }
    }
    return result;
}

} // namespace ned::editor::grammar::compile
