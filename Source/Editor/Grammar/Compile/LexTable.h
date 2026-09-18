//
// The lexer DFA for each parse state's set of valid tokens -- tree-sitter's
// generator `build_tables/{build_lex_table, token_conflicts,
// coincident_tokens}.rs`. Token conflicts (which tokens can match the same
// text, prefixes of each other, or through separators) decide both which
// parse states may share a lex state and how a completed token competes
// with a longer one still in progress.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_LEXTABLE_H
#define NED_EDITOR_GRAMMAR_COMPILE_LEXTABLE_H

#include <optional>
#include <vector>

#include "Editor/Grammar/Compile/Grammar.h"
#include "Editor/Grammar/Compile/ParseTable.h"

namespace ned::editor::grammar::compile {

struct AdvanceAction {
    LexStateId state       = 0;
    bool       inMainToken = false;

    auto operator<=>(const AdvanceAction&) const = default;
};

struct LexState {
    std::optional<Symbol>                               acceptAction;
    std::optional<AdvanceAction>                        eofAction;
    std::vector<std::pair<CharacterSet, AdvanceAction>> advanceActions;

    bool               operator==(const LexState&) const = default;
    [[nodiscard]] bool Less(const LexState& other) const;
};

struct LexTable {
    std::vector<LexState> states;
};

class TokenConflictMap {
  public:
    TokenConflictMap(const LexicalGrammar& grammar, std::vector<TokenSet> followingTokens);

    [[nodiscard]] bool HasSameConflictStatus(std::size_t a, std::size_t b, std::size_t other) const;
    [[nodiscard]] bool DoesMatchDifferentString(std::size_t i, std::size_t j) const;
    [[nodiscard]] bool DoesMatchSameString(std::size_t i, std::size_t j) const;
    [[nodiscard]] bool DoesConflict(std::size_t i, std::size_t j) const;
    [[nodiscard]] bool DoesMatchPrefix(std::size_t i, std::size_t j) const;
    [[nodiscard]] bool DoesMatchShorterOrLonger(std::size_t i, std::size_t j) const;
    [[nodiscard]] bool DoesOverlap(std::size_t i, std::size_t j) const;

    // Whether `left` (precedence, variable) beats `right` when both complete.
    static bool PreferToken(const LexicalGrammar& grammar, std::pair<int, std::size_t> left, std::pair<int, std::size_t> right);
    // Whether continuing along `t` beats accepting the completed token.
    static bool PreferTransition(const LexicalGrammar& grammar, const NfaTransition& t, std::size_t completedId, int completedPrecedence, bool hasSeparatorTransitions);

  private:
    struct Status {
        bool matchesPrefix              = false;
        bool doesMatchContinuation      = false;
        bool doesMatchValidContinuation = false;
        bool doesMatchSeparators        = false;
        bool matchesSameString          = false;
        bool matchesDifferentString     = false;

        bool operator==(const Status&) const = default;
    };

    [[nodiscard]] const Status& At(std::size_t i, std::size_t j) const {
        return statusMatrix_[n_ * i + j];
    }

    std::size_t               n_;
    std::vector<Status>       statusMatrix_;
    std::vector<TokenSet>     followingTokens_;
    std::vector<CharacterSet> startingChars_;
    std::vector<CharacterSet> followingChars_;
};

// For each pair of tokens, the parse states where both are valid.
class CoincidentTokenIndex {
  public:
    CoincidentTokenIndex(const ParseTable& table, const LexicalGrammar& grammar);

    [[nodiscard]] const std::vector<ParseStateId>& StatesWith(Symbol a, Symbol b) const;
    [[nodiscard]] bool                             Contains(Symbol a, Symbol b) const;

  private:
    [[nodiscard]] std::size_t Index(std::size_t a, std::size_t b) const {
        return a < b ? a * n_ + b : b * n_ + a;
    }

    std::size_t                            n_;
    std::vector<std::vector<ParseStateId>> entries_;
};

// A character set with more than this many ranges is a candidate for a
// shared, binary-searched table.
inline constexpr std::size_t kLargeCharacterRangeCount = 8;

struct LexTables {
    LexTable main;
    LexTable keyword;
    // (token, set): the sets each single token's lexer advances on within
    // the token, plus (nullopt, set) for its separator transitions.
    std::vector<std::pair<std::optional<Symbol>, CharacterSet>> largeCharacterSets;
};

// Assigns every parse state its lex state (`ParseState::lexStateId`).
[[nodiscard]] LexTables BuildLexTable(ParseTable& table, const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const TokenSet& keywords,
                                      const CoincidentTokenIndex& coincident, const TokenConflictMap& conflicts);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_LEXTABLE_H
