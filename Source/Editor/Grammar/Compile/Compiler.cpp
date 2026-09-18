#include "Compiler.h"

#include <algorithm>
#include <map>

#include "Editor/Grammar/Compile/Prepare.h"
#include "Editor/Grammar/Compile/Unicode.h"

namespace ned::editor::grammar::compile {

namespace {

    // Tokens that are words the word token could also match: a keyword is
    // lexed as the word token and re-lexed by the keyword lexer.
    TokenSet IdentifyKeywords(const LexicalGrammar& lexical, const ParseTable& table, std::optional<Symbol> wordToken, const TokenConflictMap& conflicts, const CoincidentTokenIndex& coincident) {
        TokenSet keywords;
        if (!wordToken)
            return keywords;

        NfaCursor  cursor(lexical.nfa, {});
        const auto allAlphabetical = [&]() {
            for (const auto& [chars, isSep] : cursor.TransitionChars()) {
                if (isSep)
                    continue;
                for (const CodepointRange& range : chars->Ranges())
                    for (std::uint32_t c = range.start; c < range.end; ++c)
                        if (c != '_' && !unicode::IsAlphabetic(c))
                            return false;
            }
            return true;
        };

        TokenSet candidates;
        for (std::size_t i = 0; i < lexical.variables.size(); ++i) {
            cursor.Reset({lexical.variables[i].startState});
            if (allAlphabetical() && conflicts.DoesMatchSameString(i, wordToken->index) && !conflicts.DoesMatchDifferentString(i, wordToken->index))
                candidates.Insert(Symbol::Terminal(static_cast<std::uint32_t>(i)));
        }

        TokenSet unshadowed;
        for (const Symbol token : candidates.Symbols()) {
            bool shadowed = false;
            for (const Symbol other : candidates.Symbols())
                if (other != token && conflicts.DoesMatchSameString(other.index, token.index)) {
                    shadowed = true;
                    break;
                }
            if (!shadowed)
                unshadowed.Insert(token);
        }

        for (const Symbol token : unshadowed.Symbols()) {
            bool include = true;
            for (std::size_t other = 0; other < lexical.variables.size(); ++other) {
                if (candidates.Contains(Symbol::Terminal(static_cast<std::uint32_t>(other))))
                    continue;
                const std::vector<ParseStateId>& states = coincident.StatesWith(token, Symbol::Terminal(static_cast<std::uint32_t>(other)));
                if (std::all_of(states.begin(), states.end(), [&](ParseStateId id) { return table.states[id].terminalEntries.count(*wordToken) > 0; }))
                    continue;
                if (!conflicts.HasSameConflictStatus(token.index, wordToken->index, other)) {
                    include = false;
                    break;
                }
            }
            if (include)
                keywords.Insert(token);
        }
        return keywords;
    }

    void PopulateErrorState(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const CoincidentTokenIndex& coincident, const TokenConflictMap& conflicts, const TokenSet& keywords) {
        ParseState&       state = table.states[0];
        const std::size_t n     = lexical.variables.size();

        TokenSet conflictFree;
        for (std::size_t i = 0; i < n; ++i) {
            bool conflicting = false;
            for (std::size_t j = 0; j < n && !conflicting; ++j)
                conflicting = j != i && !coincident.Contains(Symbol::Terminal(static_cast<std::uint32_t>(i)), Symbol::Terminal(static_cast<std::uint32_t>(j))) && conflicts.DoesMatchShorterOrLonger(i, j);
            if (!conflicting)
                conflictFree.Insert(Symbol::Terminal(static_cast<std::uint32_t>(i)));
        }

        const ParseTableEntry recover{.actions = {ParseAction::Recover()}, .reusable = false};
        for (std::size_t i = 0; i < n; ++i) {
            const Symbol symbol = Symbol::Terminal(static_cast<std::uint32_t>(i));
            if (!conflictFree.Contains(symbol) && !keywords.Contains(symbol) && syntax.wordToken != symbol) {
                bool excluded = false;
                for (const Symbol t : conflictFree.Symbols())
                    if (!coincident.Contains(symbol, t) && conflicts.DoesConflict(symbol.index, t.index)) {
                        excluded = true;
                        break;
                    }
                if (excluded)
                    continue;
            }
            state.terminalEntries.try_emplace(symbol, recover);
        }
        for (std::size_t i = 0; i < syntax.externalTokens.size(); ++i)
            if (!syntax.externalTokens[i].correspondingInternalToken)
                state.terminalEntries.try_emplace(Symbol::External(static_cast<std::uint32_t>(i)), recover);
        state.terminalEntries[Symbol::End()] = recover;
    }

    void PopulateUsedSymbols(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical) {
        std::vector<bool> terminalUsed(lexical.variables.size(), false);
        std::vector<bool> nonTerminalUsed(syntax.variables.size(), false);
        std::vector<bool> externalUsed(syntax.externalTokens.size(), false);
        for (const ParseState& state : table.states) {
            for (const auto& [symbol, _] : state.terminalEntries) {
                if (symbol.IsTerminal())
                    terminalUsed[symbol.index] = true;
                else if (symbol.IsExternal())
                    externalUsed[symbol.index] = true;
            }
            for (const auto& [symbol, _] : state.nonterminalEntries)
                nonTerminalUsed[symbol.index] = true;
        }
        table.symbols.push_back(Symbol::End());
        for (std::size_t i = 0; i < terminalUsed.size(); ++i) {
            if (!terminalUsed[i])
                continue;
            // The word token gets a low index so a subtree's symbol can be
            // reassigned to it in place.
            if (syntax.wordToken && syntax.wordToken->index == i)
                table.symbols.insert(table.symbols.begin() + 1, Symbol::Terminal(static_cast<std::uint32_t>(i)));
            else
                table.symbols.push_back(Symbol::Terminal(static_cast<std::uint32_t>(i)));
        }
        for (std::size_t i = 0; i < externalUsed.size(); ++i)
            if (externalUsed[i])
                table.symbols.push_back(Symbol::External(static_cast<std::uint32_t>(i)));
        for (std::size_t i = 0; i < nonTerminalUsed.size(); ++i)
            if (nonTerminalUsed[i])
                table.symbols.push_back(Symbol::NonTerminal(static_cast<std::uint32_t>(i)));
    }

    void PopulateExternalLexStates(ParseTable& table, const SyntaxGrammar& syntax) {
        std::map<std::uint32_t, std::uint32_t> externalByInternal;
        for (std::size_t i = 0; i < syntax.externalTokens.size(); ++i)
            if (const auto internal = syntax.externalTokens[i].correspondingInternalToken)
                externalByInternal[internal->index] = static_cast<std::uint32_t>(i);

        table.externalLexStates.emplace_back(); // state 0: no external tokens
        for (ParseState& state : table.states) {
            TokenSet externals;
            for (const auto& [token, _] : state.terminalEntries) {
                if (token.IsExternal())
                    externals.Insert(token);
                else if (token.IsTerminal())
                    if (const auto it = externalByInternal.find(token.index); it != externalByInternal.end())
                        externals.Insert(Symbol::External(it->second));
            }
            const auto found = std::find(table.externalLexStates.begin(), table.externalLexStates.end(), externals);
            if (found != table.externalLexStates.end()) {
                state.externalLexStateId = static_cast<std::size_t>(found - table.externalLexStates.begin());
            }
            else {
                table.externalLexStates.push_back(std::move(externals));
                state.externalLexStateId = table.externalLexStates.size() - 1;
            }
        }
    }

    void MarkFragileTokens(ParseTable& table, const LexicalGrammar& lexical, const TokenConflictMap& conflicts) {
        const std::size_t n = lexical.variables.size();
        std::vector<bool> valid(n);
        for (ParseState& state : table.states) {
            std::fill(valid.begin(), valid.end(), false);
            for (const auto& [token, _] : state.terminalEntries)
                if (token.IsTerminal())
                    valid[token.index] = true;
            for (auto& [token, entry] : state.terminalEntries) {
                if (!token.IsTerminal())
                    continue;
                for (std::size_t i = 0; i < n; ++i) {
                    if (valid[i] && conflicts.DoesOverlap(i, token.index)) {
                        entry.reusable = false;
                        break;
                    }
                }
            }
        }
    }

} // namespace

std::unique_ptr<CompiledLanguage> CompileGrammar(const GrammarFile& file) {
    const InputGrammar input    = ParseInputGrammar(file);
    PreparedGrammar    prepared = PrepareGrammar(input);

    const std::vector<VariableInfo> variableInfo = GetVariableInfo(prepared.syntax, prepared.lexical, prepared.defaultAliases);
    auto                            supertypes   = GetSupertypeSymbolMap(prepared.syntax, variableInfo);

    const std::vector<TokenSet> following = GetFollowingTokens(prepared.syntax, prepared.lexical, prepared.inlines);
    ParseTable                  table     = BuildParseTable(prepared.syntax, prepared.lexical, prepared.inlines, variableInfo);
    const TokenConflictMap      conflicts(prepared.lexical, following);
    const CoincidentTokenIndex  coincident(table, prepared.lexical);
    const TokenSet              keywords = IdentifyKeywords(prepared.lexical, table, prepared.syntax.wordToken, conflicts, coincident);
    PopulateErrorState(table, prepared.syntax, prepared.lexical, coincident, conflicts, keywords);
    PopulateUsedSymbols(table, prepared.syntax, prepared.lexical);
    MinimizeParseTable(table, prepared.syntax, prepared.lexical, prepared.defaultAliases, conflicts, keywords);
    LexTables lexTables = BuildLexTable(table, prepared.syntax, prepared.lexical, keywords, coincident, conflicts);
    PopulateExternalLexStates(table, prepared.syntax);
    MarkFragileTokens(table, prepared.lexical, conflicts);

    return AssembleTables(AssemblyInput{
        .name               = file.name,
        .parseTable         = std::move(table),
        .lexTables          = std::move(lexTables),
        .syntax             = std::move(prepared.syntax),
        .lexical            = std::move(prepared.lexical),
        .defaultAliases     = std::move(prepared.defaultAliases),
        .supertypeSymbolMap = std::move(supertypes),
    });
}

} // namespace ned::editor::grammar::compile
