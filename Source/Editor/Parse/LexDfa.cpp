#include "Editor/Parse/LexDfa.h"

namespace ned::editor::parse {

namespace {

    // parser.h's set_contains: a binary search that compares the int32
    // lookahead against int32 range bounds.
    bool SetContains(const std::vector<LexCharacterRange>& ranges, std::int32_t lookahead) {
        std::size_t index = 0;
        std::size_t size  = ranges.size();
        while (size > 1) {
            const std::size_t        half = size / 2;
            const std::size_t        mid  = index + half;
            const LexCharacterRange& r    = ranges[mid];
            if (lookahead >= r.start && lookahead <= r.end)
                return true;
            if (lookahead > r.end)
                index = mid;
            size -= half;
        }
        const LexCharacterRange& r = ranges[index];
        return lookahead >= r.start && lookahead <= r.end;
    }

    bool Holds(const LexClause& clause, std::int32_t lookahead, bool eof) {
        switch (clause.kind) {
            case LexClause::Kind::NulUnlessEof:
                return !eof && lookahead == 0;
            case LexClause::Kind::AtMostUnlessEof:
                return !eof && lookahead <= clause.end;
            case LexClause::Kind::Equal:
                return lookahead == clause.start;
            case LexClause::Kind::Between:
                return clause.start <= lookahead && lookahead <= clause.end;
            case LexClause::Kind::NotEqual:
                return lookahead != clause.start;
            case LexClause::Kind::Outside:
                return lookahead < clause.start || clause.end < lookahead;
            case LexClause::Kind::Greater:
                return lookahead > clause.end;
        }
        return false;
    }

    bool Matches(const DfaLanguage& language, const LexDfaTransition& t, std::int32_t lookahead, bool eof) {
        if (!t.conditional)
            return true;
        bool positive    = false;
        bool hasPositive = false;
        if (t.largeSet) {
            hasPositive = true;
            positive    = (!t.largeSetCheckEof || !eof) && SetContains(language.largeCharacterSets[*t.largeSet], lookahead);
        }
        if (!t.asserted.empty()) {
            hasPositive = true;
            if (!positive) {
                bool value = !t.assertedAnyOf;
                for (const LexClause& clause : t.asserted) {
                    const bool holds = Holds(clause, lookahead, eof);
                    if (t.assertedAnyOf ? holds : !holds) {
                        value = holds;
                        break;
                    }
                }
                positive = value;
            }
        }
        if (hasPositive && !positive)
            return false;
        for (const LexClause& clause : t.negated)
            if (!Holds(clause, lookahead, eof))
                return false;
        return true;
    }

} // namespace

bool DfaLex(const DfaLanguage& language, const LexDfa& dfa, abi::LexerData* lexer, abi::StateId state) {
    bool result = false;
    for (;;) {
        if (state >= dfa.states.size())
            return false; // the switch's default
        const LexDfaState& current   = dfa.states[state];
        const std::int32_t lookahead = lexer->lookahead;
        const bool         eof       = lexer->eof(lexer);

        if (current.hasAccept) {
            result              = true;
            lexer->resultSymbol = current.acceptSymbol;
            lexer->markEnd(lexer);
        }
        if (current.hasEof && eof) {
            lexer->advance(lexer, false);
            state = current.eofState;
            continue;
        }
        bool advanced = false;
        for (const LexDfaTransition& transition : current.transitions) {
            if (Matches(language, transition, lookahead, eof)) {
                lexer->advance(lexer, !transition.inMainToken);
                state    = transition.nextState;
                advanced = true;
                break;
            }
        }
        if (!advanced)
            return result;
    }
}

bool LexMain(const abi::LanguageData* language, abi::LexerData* lexer, abi::StateId state) {
    const auto& dfa = *reinterpret_cast<const DfaLanguage*>(language);
    return DfaLex(dfa, dfa.main, lexer, state);
}

bool LexKeyword(const abi::LanguageData* language, abi::LexerData* lexer, abi::StateId state) {
    const auto& dfa = *reinterpret_cast<const DfaLanguage*>(language);
    return DfaLex(dfa, dfa.keyword, lexer, state);
}

} // namespace ned::editor::parse
