#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Editor/Parse/Abi.h"

// The lexer as data: what a generated parser.c's `ts_lex` encodes as a
// switch over states, here as a state list the engine interprets. A
// language compiled by ned (Grammar/Compile/) carries two -- the main lexer
// and the keyword lexer -- behind its LanguageData, whose `lexFn` is null
// to say so (DfaLanguage's `data` is its first member, the same layout
// contract Lexer.h uses for LexerData).
//
// A transition keeps the shape of the C condition the generator would have
// emitted for it, not just its character set: the generated checks treat
// end of input (lookahead 0 with eof), an embedded NUL, and an undecodable
// byte (lookahead -1) differently depending on whether a range starts at
// NUL, whether the class was negated, whether the state used an
// ADVANCE_MAP, and whether a shared large character set was matched. Those
// cases are where a hand-simplified interpreter and parser.c would part
// ways, so the interpreter evaluates the same clauses in the same order.

namespace ned::editor::parse {

// One comparison of the generated `if`, on lookahead as int32 (so -1, the
// undecodable-byte marker, orders below every codepoint the way it does
// in C).
struct LexClause {
    enum class Kind : std::uint8_t {
        NulUnlessEof,    // (!eof && lookahead == 0)
        AtMostUnlessEof, // (!eof && lookahead <= end)
        Equal,           // lookahead == start
        Between,         // (start <= lookahead && lookahead <= end)
        NotEqual,        // lookahead != start
        Outside,         // (lookahead < start || end < lookahead)
        Greater,         // lookahead > end
    };
    Kind         kind  = Kind::Equal;
    std::int32_t start = 0;
    std::int32_t end   = 0;
};

// A `static const TSCharacterRange set[]` searched by `set_contains`.
struct LexCharacterRange {
    std::int32_t start; // inclusive
    std::int32_t end;   // inclusive
};

struct LexDfaTransition {
    std::uint16_t nextState   = 0;
    bool          inMainToken = true; // ADVANCE vs SKIP
    bool          conditional = true; // false: the check simplified away entirely

    // Positive side: `(!eof && set_contains(large, lookahead)) || asserted`.
    std::optional<std::uint32_t> largeSet; // index into DfaLanguage::largeCharacterSets
    bool                         largeSetCheckEof = false;
    bool                         assertedAnyOf    = true; // clauses joined by || (included form) or && (negated form)
    std::vector<LexClause>       asserted;
    // Negative side, && with the positive one: the removals from a large set.
    std::vector<LexClause> negated;
};

struct LexDfaState {
    bool                          hasAccept    = false;
    abi::Symbol                   acceptSymbol = 0;
    bool                          hasEof       = false;
    std::uint16_t                 eofState     = 0;
    std::vector<LexDfaTransition> transitions;
};

struct LexDfa {
    std::vector<LexDfaState> states;
};

struct DfaLanguage {
    abi::LanguageData                           data; // must stay the first member
    LexDfa                                      main;
    LexDfa                                      keyword;
    std::vector<std::vector<LexCharacterRange>> largeCharacterSets;
};

// The generated lexer's loop over `dfa` from `state`; true when a token was
// accepted (lexer->resultSymbol set, end marked).
bool DfaLex(const DfaLanguage& language, const LexDfa& dfa, abi::LexerData* lexer, abi::StateId state);

// The language's lexers, as the engine calls them.
bool LexMain(const abi::LanguageData* language, abi::LexerData* lexer, abi::StateId state);
bool LexKeyword(const abi::LanguageData* language, abi::LexerData* lexer, abi::StateId state);

} // namespace ned::editor::parse
