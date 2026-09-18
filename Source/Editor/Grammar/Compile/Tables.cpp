#include "Tables.h"

#include <algorithm>
#include <set>
#include <tuple>

namespace ned::editor::grammar::compile {

namespace {

    constexpr std::size_t kSmallStateThreshold = 64;

    using LanguageData = parse::abi::LanguageData;

    // --- The lexer as the generator would have emitted it -----------------------

    void AddRangeConditions(std::vector<parse::LexClause>& out, const CharacterSet& set, bool included) {
        using Kind = parse::LexClause::Kind;
        for (const CodepointRange& range : set.Ranges()) {
            const auto start = static_cast<std::int32_t>(range.start);
            const auto end   = static_cast<std::int32_t>(range.end - 1);
            if (included) {
                if (start == 0)
                    out.push_back({.kind = end == 0 ? Kind::NulUnlessEof : Kind::AtMostUnlessEof, .start = 0, .end = end});
                else if (end == start)
                    out.push_back({.kind = Kind::Equal, .start = start, .end = end});
                else if (end == start + 1) {
                    out.push_back({.kind = Kind::Equal, .start = start, .end = start});
                    out.push_back({.kind = Kind::Equal, .start = end, .end = end});
                }
                else
                    out.push_back({.kind = Kind::Between, .start = start, .end = end});
            }
            else {
                if (end == start)
                    out.push_back({.kind = Kind::NotEqual, .start = start, .end = end});
                else if (end == start + 1) {
                    out.push_back({.kind = Kind::NotEqual, .start = start, .end = start});
                    out.push_back({.kind = Kind::NotEqual, .start = end, .end = end});
                }
                else if (start != 0)
                    out.push_back({.kind = Kind::Outside, .start = start, .end = end});
                else
                    out.push_back({.kind = Kind::Greater, .start = start, .end = end});
            }
        }
    }

    parse::LexDfa RenderLexDfa(const LexTable& table, const std::vector<std::pair<std::optional<Symbol>, CharacterSet>>& largeSets, const std::map<Symbol, std::uint16_t>& ids) {
        parse::LexDfa dfa;
        for (const LexState& state : table.states) {
            parse::LexDfaState s;
            if (state.acceptAction) {
                s.hasAccept    = true;
                s.acceptSymbol = ids.at(*state.acceptAction);
            }
            if (state.eofAction) {
                s.hasEof   = true;
                s.eofState = static_cast<std::uint16_t>(state.eofAction->state);
            }

            CharacterSet ruledOut;

            // Leading single-character transitions become an ADVANCE_MAP
            // (a plain `== lookahead` per character, no eof guard) once
            // there are enough of them.
            std::size_t leadingCount      = 0;
            std::size_t leadingRangeCount = 0;
            for (const auto& [chars, action] : state.advanceActions) {
                const bool simple = action.inMainToken && std::all_of(chars.Ranges().begin(), chars.Ranges().end(), [](const CodepointRange& r) {
                                        return r.end - 1 <= r.start + 1 && r.end - 1 <= 0xFFFF;
                                    });
                if (!simple)
                    break;
                leadingCount++;
                leadingRangeCount += chars.RangeCount();
            }
            if (leadingRangeCount >= 8) {
                for (std::size_t i = 0; i < leadingCount; ++i) {
                    const auto& [chars, action] = state.advanceActions[i];
                    parse::LexDfaTransition t{.nextState = static_cast<std::uint16_t>(action.state), .inMainToken = true, .conditional = true, .assertedAnyOf = true};
                    for (const CodepointRange& range : chars.Ranges()) {
                        t.asserted.push_back({.kind = parse::LexClause::Kind::Equal, .start = static_cast<std::int32_t>(range.start), .end = static_cast<std::int32_t>(range.start)});
                        if (range.end - 1 > range.start)
                            t.asserted.push_back({.kind = parse::LexClause::Kind::Equal, .start = static_cast<std::int32_t>(range.end - 1), .end = static_cast<std::int32_t>(range.end - 1)});
                    }
                    ruledOut = ruledOut.Add(chars);
                    s.transitions.push_back(std::move(t));
                }
            }
            else {
                leadingCount = 0;
            }

            for (std::size_t i = leadingCount; i < state.advanceActions.size(); ++i) {
                const auto& [chars, action]   = state.advanceActions[i];
                const CharacterSet simplified = chars.SimplifyIgnoring(ruledOut);

                std::optional<std::size_t> bestIndex;
                CharacterSet               bestAdditions, bestRemovals;
                if (simplified.RangeCount() >= kLargeCharacterRangeCount) {
                    for (std::size_t ix = 0; ix < largeSets.size(); ++ix) {
                        CharacterSet charsCopy = simplified;
                        CharacterSet largeSet  = largeSets[ix].second;
                        if (charsCopy.RemoveIntersection(largeSet).IsEmpty())
                            continue;
                        const CharacterSet additions = charsCopy.SimplifyIgnoring(ruledOut);
                        const CharacterSet removals  = largeSet.SimplifyIgnoring(ruledOut);
                        const std::size_t  total     = additions.RangeCount() + removals.RangeCount();
                        if (total >= simplified.RangeCount())
                            continue;
                        if (bestIndex && bestAdditions.RangeCount() + bestRemovals.RangeCount() < total)
                            continue;
                        bestIndex     = ix;
                        bestAdditions = additions;
                        bestRemovals  = removals;
                    }
                }
                ruledOut = ruledOut.Add(chars);

                CharacterSet            asserted = simplified;
                CharacterSet            negated;
                parse::LexDfaTransition t{.nextState = static_cast<std::uint16_t>(action.state), .inMainToken = action.inMainToken};
                if (bestIndex) {
                    asserted           = bestAdditions;
                    negated            = bestRemovals;
                    t.largeSet         = static_cast<std::uint32_t>(*bestIndex);
                    t.largeSetCheckEof = largeSets[*bestIndex].second.Contains(0);
                }
                t.conditional = bestIndex.has_value() || !asserted.IsEmpty() || !negated.IsEmpty();
                if (!asserted.IsEmpty()) {
                    // A set holding the maximum codepoint came from a negated
                    // class; it is checked as exclusions, with NUL excluded
                    // too.
                    const bool included = !asserted.Contains(kCodepointEnd - 1);
                    if (!included)
                        asserted = asserted.Negate().AddChar(0);
                    t.assertedAnyOf = included;
                    AddRangeConditions(t.asserted, asserted, included);
                }
                if (!negated.IsEmpty())
                    AddRangeConditions(t.negated, negated, false);
                s.transitions.push_back(std::move(t));
            }
            dfa.states.push_back(std::move(s));
        }
        return dfa;
    }

    // --- Everything else: render.rs's tables as data -------------------------------

    struct Assembler {
        AssemblyInput                     in;
        std::unique_ptr<CompiledLanguage> out = std::make_unique<CompiledLanguage>();

        std::map<Symbol, std::uint16_t> symbolIds; // parse-table symbol -> numbered id
        std::vector<Alias>              uniqueAliases;
        std::map<Alias, std::uint16_t>  aliasIds;
        std::map<Symbol, Symbol>        symbolMap;
        std::vector<std::string>        fieldNames;
        std::vector<TokenSet>           reservedWordSets;
        std::vector<std::size_t>        reservedWordSetIdsByParseState;
        std::size_t                     largeStateCount = 0;

        [[nodiscard]] std::pair<const std::string*, VariableType> MetadataForSymbol(Symbol symbol) const {
            static const std::string kEnd = "end";
            switch (symbol.kind) {
                case SymbolType::End:
                case SymbolType::EndOfNonTerminalExtra:
                    return {&kEnd, VariableType::Hidden};
                case SymbolType::NonTerminal:
                    return {&in.syntax.variables[symbol.index].name, in.syntax.variables[symbol.index].kind};
                case SymbolType::Terminal:
                    return {&in.lexical.variables[symbol.index].name, in.lexical.variables[symbol.index].kind};
                case SymbolType::External:
                    return {&in.syntax.externalTokens[symbol.index].name, in.syntax.externalTokens[symbol.index].kind};
            }
            return {&kEnd, VariableType::Hidden};
        }

        [[nodiscard]] std::vector<Symbol> SymbolsForAlias(const Alias& alias) const {
            std::vector<Symbol> result;
            for (const Symbol symbol : in.parseTable.symbols) {
                if (const auto it = in.defaultAliases.find(symbol); it != in.defaultAliases.end()) {
                    if (it->second == alias)
                        result.push_back(symbol);
                }
                else {
                    const auto [name, kind] = MetadataForSymbol(symbol);
                    if (*name == alias.value && kind == (alias.named ? VariableType::Named : VariableType::Anonymous))
                        result.push_back(symbol);
                }
            }
            return result;
        }

        // A state's terminal entries in symbol-id order (the generator's
        // `symbol_order`), which fixes action-list numbering and the order
        // of a small state's groups.
        [[nodiscard]] std::vector<std::pair<Symbol, const ParseTableEntry*>> TerminalEntriesById(const ParseState& state) const {
            std::vector<std::pair<Symbol, const ParseTableEntry*>> entries;
            for (const auto& [symbol, entry] : state.terminalEntries)
                entries.emplace_back(symbol, &entry);
            std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) { return symbolIds.at(a.first) < symbolIds.at(b.first); });
            return entries;
        }

        void Init() {
            std::uint16_t next = 0;
            for (const Symbol symbol : in.parseTable.symbols)
                symbolIds.emplace(symbol, next++);
            symbolIds.emplace(Symbol::EndOfNonTerminalExtra(), symbolIds.at(Symbol::End()));

            // Symbols sharing a default alias, or anonymous tokens sharing a
            // string, map to one public symbol: an unaliased one if there
            // is one, else the lowest.
            for (const Symbol symbol : in.parseTable.symbols) {
                Symbol mapping = symbol;
                if (const auto alias = in.defaultAliases.find(symbol); alias != in.defaultAliases.end()) {
                    const VariableType kind = alias->second.named ? VariableType::Named : VariableType::Anonymous;
                    for (const Symbol other : in.parseTable.symbols) {
                        if (const auto otherAlias = in.defaultAliases.find(other); otherAlias != in.defaultAliases.end()) {
                            if (other < mapping && otherAlias->second == alias->second)
                                mapping = other;
                        }
                        else {
                            const auto [name, otherKind] = MetadataForSymbol(other);
                            if (*name == alias->second.value && otherKind == kind) {
                                mapping = other;
                                break;
                            }
                        }
                    }
                }
                else if (symbol.IsTerminal()) {
                    const auto [name, kind] = MetadataForSymbol(symbol);
                    for (const Symbol other : in.parseTable.symbols) {
                        const auto [otherName, otherKind] = MetadataForSymbol(other);
                        if (*otherName == *name && otherKind == kind) {
                            if (const auto mapped = symbolMap.find(other); mapped != symbolMap.end() && mapped->second == symbol)
                                break;
                            mapping = other;
                            break;
                        }
                    }
                }
                symbolMap[symbol] = mapping;
            }

            for (const ProductionInfo& info : in.parseTable.productionInfos) {
                for (const auto& [fieldName, _] : info.fieldMap) {
                    const auto pos = std::lower_bound(fieldNames.begin(), fieldNames.end(), fieldName);
                    if (pos == fieldNames.end() || *pos != fieldName)
                        fieldNames.insert(pos, fieldName);
                }
                for (const std::optional<Alias>& alias : info.aliasSequence) {
                    if (!alias || aliasIds.count(*alias) > 0)
                        continue;
                    const std::vector<Symbol> existing = SymbolsForAlias(*alias);
                    if (!existing.empty()) {
                        aliasIds.emplace(*alias, symbolIds.at(symbolMap.at(existing.front())));
                    }
                    else {
                        const auto pos = std::lower_bound(uniqueAliases.begin(), uniqueAliases.end(), *alias);
                        if (pos == uniqueAliases.end() || *pos != *alias)
                            uniqueAliases.insert(pos, *alias);
                        aliasIds.emplace(*alias, 0); // numbered below, once the set is complete
                    }
                }
            }
            const auto symbolCount = static_cast<std::uint16_t>(in.parseTable.symbols.size());
            for (std::size_t i = 0; i < uniqueAliases.size(); ++i)
                aliasIds[uniqueAliases[i]] = static_cast<std::uint16_t>(symbolCount + i);

            reservedWordSets.emplace_back();
            for (const ParseState& state : in.parseTable.states) {
                const auto found = std::find(reservedWordSets.begin(), reservedWordSets.end(), state.reservedWords);
                if (found != reservedWordSets.end()) {
                    reservedWordSetIdsByParseState.push_back(static_cast<std::size_t>(found - reservedWordSets.begin()));
                }
                else {
                    reservedWordSets.push_back(state.reservedWords);
                    reservedWordSetIdsByParseState.push_back(reservedWordSets.size() - 1);
                }
            }

            const std::size_t threshold = std::min(kSmallStateThreshold, in.parseTable.symbols.size() / 2);
            largeStateCount             = 0;
            for (std::size_t i = 0; i < in.parseTable.states.size(); ++i) {
                const ParseState& s = in.parseTable.states[i];
                if (i <= 1 || s.terminalEntries.size() + s.nonterminalEntries.size() > threshold)
                    largeStateCount++;
                else
                    break;
            }
        }

        void AddSymbolNamesAndMetadata() {
            for (const Symbol symbol : in.parseTable.symbols) {
                const auto found = in.defaultAliases.find(symbol);
                out->symbolNameStorage.push_back(found != in.defaultAliases.end() ? found->second.value : *MetadataForSymbol(symbol).first);
                parse::abi::SymbolMetadata metadata{.visible = false, .named = false, .supertype = false};
                if (found != in.defaultAliases.end()) {
                    metadata.visible = true;
                    metadata.named   = found->second.named;
                }
                else {
                    switch (MetadataForSymbol(symbol).second) {
                        case VariableType::Named:
                            metadata = {.visible = true, .named = true, .supertype = false};
                            break;
                        case VariableType::Anonymous:
                            metadata = {.visible = true, .named = false, .supertype = false};
                            break;
                        case VariableType::Hidden:
                            metadata           = {.visible = false, .named = true, .supertype = false};
                            metadata.supertype = std::find(in.syntax.supertypeSymbols.begin(), in.syntax.supertypeSymbols.end(), symbol) != in.syntax.supertypeSymbols.end();
                            break;
                        case VariableType::Auxiliary:
                            metadata = {.visible = false, .named = false, .supertype = false};
                            break;
                    }
                }
                out->symbolMetadata.push_back(metadata);
                out->publicSymbolMap.push_back(symbolIds.at(symbolMap.at(symbol)));
            }
            for (const Alias& alias : uniqueAliases) {
                out->symbolNameStorage.push_back(alias.value);
                out->symbolMetadata.push_back({.visible = true, .named = alias.named, .supertype = false});
                out->publicSymbolMap.push_back(aliasIds.at(alias));
            }
            out->fieldNameStorage.emplace_back(); // [0] = NULL
            for (const std::string& name : fieldNames)
                out->fieldNameStorage.push_back(name);
        }

        [[nodiscard]] std::uint16_t FieldId(const std::string& name) const {
            return static_cast<std::uint16_t>(std::lower_bound(fieldNames.begin(), fieldNames.end(), name) - fieldNames.begin() + 1);
        }

        void AddAliasSequences() {
            const std::size_t width = in.parseTable.maxAliasedProductionLength;
            out->aliasSequences.assign(in.parseTable.productionInfos.size() * width, 0);
            for (std::size_t i = 0; i < in.parseTable.productionInfos.size(); ++i) {
                const ProductionInfo& info = in.parseTable.productionInfos[i];
                for (std::size_t j = 0; j < info.aliasSequence.size(); ++j)
                    if (info.aliasSequence[j])
                        out->aliasSequences[i * width + j] = aliasIds.at(*info.aliasSequence[j]);
            }
        }

        void AddNonTerminalAliasMap() {
            std::map<Symbol, std::vector<std::uint16_t>> aliasIdsBySymbol;
            for (const SyntaxVariable& variable : in.syntax.variables) {
                for (const Production& production : variable.productions) {
                    for (const ProductionStep& step : production.steps) {
                        if (!step.alias || !step.symbol.IsNonTerminal() || symbolIds.count(step.symbol) == 0)
                            continue;
                        const auto defaultAlias = in.defaultAliases.find(step.symbol);
                        if (defaultAlias != in.defaultAliases.end() && defaultAlias->second == *step.alias)
                            continue;
                        const auto aliasId = aliasIds.find(*step.alias);
                        if (aliasId == aliasIds.end())
                            continue;
                        std::vector<std::uint16_t>& ids = aliasIdsBySymbol[step.symbol];
                        const auto                  pos = std::lower_bound(ids.begin(), ids.end(), aliasId->second);
                        if (pos == ids.end() || *pos != aliasId->second)
                            ids.insert(pos, aliasId->second);
                    }
                }
            }
            for (const auto& [symbol, ids] : aliasIdsBySymbol) {
                out->aliasMap.push_back(symbolIds.at(symbol));
                out->aliasMap.push_back(static_cast<std::uint16_t>(1 + ids.size()));
                out->aliasMap.push_back(symbolIds.at(symbolMap.at(symbol)));
                for (const std::uint16_t id : ids)
                    out->aliasMap.push_back(id);
            }
            out->aliasMap.push_back(0);
        }

        void AddPrimaryStateIds() {
            std::map<std::size_t, std::size_t> firstStateForCore;
            for (std::size_t i = 0; i < in.parseTable.states.size(); ++i)
                out->primaryStateIds.push_back(static_cast<parse::abi::StateId>(firstStateForCore.try_emplace(in.parseTable.states[i].coreId, i).first->second));
        }

        void AddFieldSequences() {
            using FlatMap = std::vector<std::pair<std::string, FieldLocation>>;
            std::vector<std::pair<std::size_t, FlatMap>> flatMaps;
            std::size_t                                  nextIndex = 0;
            const auto                                   idFor     = [&](FlatMap map) {
                for (const auto& [index, existing] : flatMaps)
                    if (existing == map)
                        return index;
                const std::size_t result = nextIndex;
                nextIndex += map.size();
                flatMaps.emplace_back(result, std::move(map));
                return result;
            };
            idFor({});
            out->fieldMapSlices.assign(in.parseTable.productionInfos.size(), {.index = 0, .length = 0});
            for (std::size_t i = 0; i < in.parseTable.productionInfos.size(); ++i) {
                const ProductionInfo& info = in.parseTable.productionInfos[i];
                if (info.fieldMap.empty())
                    continue;
                FlatMap flat;
                for (const auto& [name, locations] : info.fieldMap)
                    for (const FieldLocation& location : locations)
                        flat.emplace_back(name, location);
                const std::size_t length = flat.size();
                out->fieldMapSlices[i]   = {.index = static_cast<std::uint16_t>(idFor(std::move(flat))), .length = static_cast<std::uint16_t>(length)};
            }
            out->fieldMapEntries.assign(nextIndex, {});
            for (const auto& [index, map] : flatMaps) {
                std::size_t k = index;
                for (const auto& [name, location] : map)
                    out->fieldMapEntries[k++] = {.fieldId = FieldId(name), .childIndex = static_cast<std::uint8_t>(location.index), .inherited = location.inherited};
            }
        }

        void AddSupertypeMap() {
            std::map<std::uint16_t, std::set<std::uint16_t>> subtypesById;
            for (const auto& [supertype, subtypes] : in.supertypeSymbolMap) {
                const auto id = symbolIds.find(supertype);
                if (id == symbolIds.end())
                    continue;
                std::set<std::uint16_t>& ids = subtypesById[id->second];
                for (const ChildType& subtype : subtypes) {
                    if (subtype.kind == ChildType::Kind::Normal) {
                        if (const auto s = symbolIds.find(subtype.symbol); s != symbolIds.end())
                            ids.insert(s->second);
                    }
                    else if (const auto a = aliasIds.find(subtype.alias); a != aliasIds.end()) {
                        ids.insert(a->second);
                    }
                    else {
                        for (const Symbol s : SymbolsForAlias(subtype.alias))
                            ids.insert(symbolIds.at(s));
                    }
                }
            }
            out->supertypeMapSlices.assign(in.parseTable.symbols.size() + uniqueAliases.size(), {.index = 0, .length = 0});
            for (const auto& [id, ids] : subtypesById) {
                out->supertypeSymbols.push_back(id);
                out->supertypeMapSlices[id] = {.index = static_cast<std::uint16_t>(out->supertypeMapEntries.size()), .length = static_cast<std::uint16_t>(ids.size())};
                for (const std::uint16_t sub : ids)
                    out->supertypeMapEntries.push_back(sub);
            }
        }

        void AddLexModes() {
            for (std::size_t i = 0; i < in.parseTable.states.size(); ++i) {
                const ParseState& state = in.parseTable.states[i];
                if (state.IsEndOfNonTerminalExtra()) {
                    out->lexModes.push_back({.lexState = static_cast<std::uint16_t>(-1), .externalLexState = 0, .reservedWordSetId = 0});
                }
                else {
                    out->lexModes.push_back({.lexState          = static_cast<std::uint16_t>(state.lexStateId),
                                             .externalLexState  = static_cast<std::uint16_t>(state.externalLexStateId),
                                             .reservedWordSetId = static_cast<std::uint16_t>(reservedWordSetIdsByParseState[i])});
                }
            }
        }

        [[nodiscard]] std::size_t MaxReservedWordSetSize() const {
            std::size_t max = 0;
            for (const TokenSet& set : reservedWordSets)
                max = std::max(max, set.Len());
            return max;
        }

        void AddReservedWordSets() {
            const std::size_t width = MaxReservedWordSetSize();
            out->reservedWords.assign(reservedWordSets.size() * width, 0);
            for (std::size_t id = 1; id < reservedWordSets.size(); ++id) {
                std::size_t k = 0;
                for (const Symbol token : reservedWordSets[id].Symbols())
                    out->reservedWords[id * width + k++] = symbolIds.at(token);
            }
        }

        void AddParseTable() {
            std::map<ParseTableEntry, std::size_t> entryIds;
            std::size_t                            nextActionIndex = 0;
            const auto                             actionListId    = [&](const ParseTableEntry& entry) {
                if (const auto it = entryIds.find(entry); it != entryIds.end())
                    return it->second;
                const std::size_t result = nextActionIndex;
                entryIds.emplace(entry, result);
                nextActionIndex += 1 + entry.actions.size();
                return result;
            };
            actionListId(ParseTableEntry{.actions = {}, .reusable = false}); // id 0: no actions

            const std::size_t symbolCount = in.parseTable.symbols.size();
            out->parseTable.assign(largeStateCount * symbolCount, 0);
            for (std::size_t i = 0; i < largeStateCount; ++i) {
                const ParseState& state = in.parseTable.states[i];
                for (const auto& [symbol, action] : state.nonterminalEntries)
                    out->parseTable[i * symbolCount + symbolIds.at(symbol)] = static_cast<std::uint16_t>(action.shiftExtra ? i : action.state);
                for (const auto& [symbol, entry] : TerminalEntriesById(state))
                    out->parseTable[i * symbolCount + symbolIds.at(symbol)] = static_cast<std::uint16_t>(actionListId(*entry));
            }

            // A small state lists groups of symbols sharing a value, ordered
            // by group size, terminals before non-terminals, then value; the
            // engine walks them in this order, so it decides which lookahead
            // an error-recovery search tries first.
            struct Group {
                std::size_t         value;
                bool                nonTerminal;
                std::vector<Symbol> symbols;
            };
            for (std::size_t i = largeStateCount; i < in.parseTable.states.size(); ++i) {
                const ParseState& state = in.parseTable.states[i];
                out->smallParseTableMap.push_back(static_cast<std::uint32_t>(out->smallParseTable.size()));
                std::vector<Group> groups;
                const auto         groupFor = [&](std::size_t value, bool nonTerminal) -> Group& {
                    for (Group& group : groups)
                        if (group.value == value && group.nonTerminal == nonTerminal)
                            return group;
                    groups.push_back({.value = value, .nonTerminal = nonTerminal, .symbols = {}});
                    return groups.back();
                };
                for (const auto& [symbol, entry] : TerminalEntriesById(state))
                    groupFor(actionListId(*entry), false).symbols.push_back(symbol);
                for (const auto& [symbol, action] : state.nonterminalEntries)
                    groupFor(action.shiftExtra ? i : action.state, true).symbols.push_back(symbol);
                std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
                    return std::make_tuple(a.symbols.size(), a.nonTerminal, a.value, a.symbols.front()) < std::make_tuple(b.symbols.size(), b.nonTerminal, b.value, b.symbols.front());
                });
                out->smallParseTable.push_back(static_cast<std::uint16_t>(groups.size()));
                for (Group& group : groups) {
                    std::sort(group.symbols.begin(), group.symbols.end());
                    out->smallParseTable.push_back(static_cast<std::uint16_t>(group.value));
                    out->smallParseTable.push_back(static_cast<std::uint16_t>(group.symbols.size()));
                    for (const Symbol s : group.symbols)
                        out->smallParseTable.push_back(symbolIds.at(s));
                }
            }

            std::vector<std::pair<std::size_t, ParseTableEntry>> entries;
            for (const auto& [entry, id] : entryIds)
                entries.emplace_back(id, entry);
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            out->parseActions.resize(nextActionIndex);
            for (const auto& [id, entry] : entries) {
                parse::abi::ParseActionEntry header{};
                header.entry.count    = static_cast<std::uint8_t>(entry.actions.size());
                header.entry.reusable = entry.reusable;
                out->parseActions[id] = header;
                for (std::size_t k = 0; k < entry.actions.size(); ++k) {
                    const ParseAction&           action = entry.actions[k];
                    parse::abi::ParseActionEntry cell{};
                    switch (action.kind) {
                        case ParseAction::Kind::Accept:
                            cell.action.type = parse::abi::ParseActionTypeAccept;
                            break;
                        case ParseAction::Kind::Recover:
                            cell.action.type = parse::abi::ParseActionTypeRecover;
                            break;
                        case ParseAction::Kind::ShiftExtra:
                            cell.action.shift = {.type = parse::abi::ParseActionTypeShift, .state = 0, .extra = true, .repetition = false};
                            break;
                        case ParseAction::Kind::Shift:
                            cell.action.shift = {.type = parse::abi::ParseActionTypeShift, .state = static_cast<parse::abi::StateId>(action.state), .extra = false, .repetition = action.isRepetition};
                            break;
                        case ParseAction::Kind::Reduce:
                            cell.action.reduce = {.type              = parse::abi::ParseActionTypeReduce,
                                                  .childCount        = static_cast<std::uint8_t>(action.childCount),
                                                  .symbol            = symbolIds.at(action.symbol),
                                                  .dynamicPrecedence = static_cast<std::int16_t>(action.dynamicPrecedence),
                                                  .productionId      = static_cast<std::uint16_t>(action.productionId)};
                            break;
                    }
                    out->parseActions[id + 1 + k] = cell;
                }
            }
        }

        void AddExternalScanner() {
            const std::size_t tokenCount = in.syntax.externalTokens.size();
            if (tokenCount == 0)
                return;
            for (std::size_t i = 0; i < tokenCount; ++i) {
                const ExternalToken& token = in.syntax.externalTokens[i];
                out->externalScannerSymbolMap.push_back(symbolIds.at(token.correspondingInternalToken.value_or(Symbol::External(static_cast<std::uint32_t>(i)))));
            }
            const std::size_t stateCount   = in.parseTable.externalLexStates.size();
            out->externalScannerStateCount = stateCount;
            out->externalScannerStates     = std::make_unique<bool[]>(stateCount * tokenCount);
            for (std::size_t s = 0; s < stateCount; ++s)
                for (const Symbol token : in.parseTable.externalLexStates[s].Symbols())
                    out->externalScannerStates[s * tokenCount + token.index] = true;
        }

        std::unique_ptr<CompiledLanguage> Finish() {
            Init();
            AddSymbolNamesAndMetadata();
            AddAliasSequences();
            AddNonTerminalAliasMap();
            AddPrimaryStateIds();
            AddFieldSequences();
            AddSupertypeMap();
            AddLexModes();
            AddReservedWordSets();
            AddParseTable();
            AddExternalScanner();

            out->language_          = std::make_unique<parse::DfaLanguage>();
            out->name               = in.name;
            out->language_->main    = RenderLexDfa(in.lexTables.main, in.lexTables.largeCharacterSets, symbolIds);
            out->language_->keyword = RenderLexDfa(in.lexTables.keyword, in.lexTables.largeCharacterSets, symbolIds);
            for (const auto& [_, set] : in.lexTables.largeCharacterSets) {
                std::vector<parse::LexCharacterRange> ranges;
                for (const CodepointRange& range : set.Ranges())
                    ranges.push_back({.start = static_cast<std::int32_t>(range.start), .end = static_cast<std::int32_t>(range.end - 1)});
                out->language_->largeCharacterSets.push_back(std::move(ranges));
            }

            std::size_t tokenCount = 0;
            for (const Symbol symbol : in.parseTable.symbols) {
                if (symbol.IsTerminal() || symbol.IsEof())
                    tokenCount++;
                else if (symbol.IsExternal() && !in.syntax.externalTokens[symbol.index].correspondingInternalToken)
                    tokenCount++;
            }

            LanguageData& data          = out->language_->data;
            data.abiVersion             = parse::abi::kAbiVersion;
            data.symbolCount            = static_cast<std::uint32_t>(in.parseTable.symbols.size());
            data.aliasCount             = static_cast<std::uint32_t>(uniqueAliases.size());
            data.tokenCount             = static_cast<std::uint32_t>(tokenCount);
            data.externalTokenCount     = static_cast<std::uint32_t>(in.syntax.externalTokens.size());
            data.stateCount             = static_cast<std::uint32_t>(in.parseTable.states.size());
            data.largeStateCount        = static_cast<std::uint32_t>(largeStateCount);
            data.productionIdCount      = static_cast<std::uint32_t>(in.parseTable.productionInfos.size());
            data.fieldCount             = static_cast<std::uint32_t>(fieldNames.size());
            data.maxAliasSequenceLength = static_cast<std::uint16_t>(in.parseTable.maxAliasedProductionLength);
            data.keywordCaptureToken    = in.syntax.wordToken ? symbolIds.at(*in.syntax.wordToken) : 0;
            data.maxReservedWordSetSize = static_cast<std::uint16_t>(MaxReservedWordSetSize());
            data.supertypeCount         = static_cast<std::uint32_t>(out->supertypeSymbols.size());
            data.externalScanner        = {};
            out->Link();
            return std::move(out);
        }
    };

} // namespace

void CompiledLanguage::Link() {
    symbolNames.clear();
    for (const std::string& symbolName : symbolNameStorage)
        symbolNames.push_back(symbolName.c_str());
    fieldNames.clear();
    fieldNames.push_back(nullptr); // field id 0 is "no field"
    for (std::size_t i = 1; i < fieldNameStorage.size(); ++i)
        fieldNames.push_back(fieldNameStorage[i].c_str());

    LanguageData& data      = language_->data;
    const bool    hasFields = data.fieldCount > 0;
    data.parseTable         = parseTable.data();
    data.smallParseTable    = smallParseTable.empty() ? nullptr : smallParseTable.data();
    data.smallParseTableMap = smallParseTableMap.empty() ? nullptr : smallParseTableMap.data();
    data.parseActions       = parseActions.data();
    data.symbolNames        = symbolNames.data();
    data.fieldNames         = hasFields ? fieldNames.data() : nullptr;
    data.fieldMapSlices     = hasFields ? fieldMapSlices.data() : nullptr;
    data.fieldMapEntries    = hasFields ? fieldMapEntries.data() : nullptr;
    data.symbolMetadata     = symbolMetadata.data();
    data.publicSymbolMap    = publicSymbolMap.data();
    data.aliasMap           = aliasMap.data();
    data.aliasSequences     = aliasSequences.empty() ? nullptr : aliasSequences.data();
    data.lexModes           = lexModes.data();
    if (data.externalTokenCount > 0) {
        data.externalScanner.states    = externalScannerStates.get();
        data.externalScanner.symbolMap = externalScannerSymbolMap.data();
    }
    data.primaryStateIds     = primaryStateIds.data();
    data.name                = name.c_str();
    data.reservedWords       = reservedWords.empty() ? nullptr : reservedWords.data();
    data.supertypeSymbols    = supertypeSymbols.empty() ? nullptr : supertypeSymbols.data();
    data.supertypeMapSlices  = supertypeSymbols.empty() ? nullptr : supertypeMapSlices.data();
    data.supertypeMapEntries = supertypeSymbols.empty() ? nullptr : supertypeMapEntries.data();
    data.metadata            = {.majorVersion = 0, .minorVersion = 0, .patchVersion = 0};
}

void CompiledLanguage::AdoptExternalScanner(const parse::ScannerVTable& scanner) {
    language_->data.externalScanner.create      = scanner.create;
    language_->data.externalScanner.destroy     = scanner.destroy;
    language_->data.externalScanner.scan        = scanner.scan;
    language_->data.externalScanner.serialize   = scanner.serialize;
    language_->data.externalScanner.deserialize = scanner.deserialize;
}

std::unique_ptr<CompiledLanguage> AssembleTables(AssemblyInput input) {
    Assembler assembler{.in = std::move(input)};
    return assembler.Finish();
}

} // namespace ned::editor::grammar::compile
