//
// The table compiler's internal grammar model: the shapes a grammar passes
// through between grammar.janet and the parse/lex tables. A port of
// tree-sitter's generator (`cli/generate`, v0.25.10) kept structurally close
// to the original so its semantics -- what the vendored tables encode -- can
// be held to it case by case.
//
// Pipeline: GrammarFile -> InputGrammar (Prepare.h) -> intern symbols ->
// extract tokens -> expand repeats -> flatten -> expand tokens to an NFA ->
// default aliases -> inlines -> SyntaxGrammar + LexicalGrammar ->
// tables (ParseTable.h, LexTable.h) -> CompiledLanguage (Tables.h).
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_GRAMMAR_H
#define NED_EDITOR_GRAMMAR_COMPILE_GRAMMAR_H

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Grammar/Compile/Nfa.h"

namespace ned::editor::grammar::compile {

class CompileError : public std::runtime_error {
  public:
    explicit CompileError(const std::string& message) : std::runtime_error(message) {
    }
};

// Declaration order is the ordering (Symbol sorts by kind first).
enum class SymbolType : std::uint8_t {
    External,
    End,
    EndOfNonTerminalExtra,
    Terminal,
    NonTerminal,
};

struct Symbol {
    SymbolType    kind  = SymbolType::End;
    std::uint32_t index = 0;

    auto operator<=>(const Symbol&) const = default;

    static constexpr Symbol NonTerminal(std::uint32_t index) {
        return {SymbolType::NonTerminal, index};
    }
    static constexpr Symbol Terminal(std::uint32_t index) {
        return {SymbolType::Terminal, index};
    }
    static constexpr Symbol External(std::uint32_t index) {
        return {SymbolType::External, index};
    }
    static constexpr Symbol End() {
        return {SymbolType::End, 0};
    }
    static constexpr Symbol EndOfNonTerminalExtra() {
        return {SymbolType::EndOfNonTerminalExtra, 0};
    }

    [[nodiscard]] bool IsTerminal() const {
        return kind == SymbolType::Terminal;
    }
    [[nodiscard]] bool IsNonTerminal() const {
        return kind == SymbolType::NonTerminal;
    }
    [[nodiscard]] bool IsExternal() const {
        return kind == SymbolType::External;
    }
    [[nodiscard]] bool IsEof() const {
        return kind == SymbolType::End;
    }
};

enum class Associativity : std::uint8_t { Left,
                                          Right };

struct Alias {
    std::string value;
    bool        named = false;

    auto operator<=>(const Alias&) const = default;
};

struct Precedence {
    enum class Kind : std::uint8_t { None,
                                     Integer,
                                     Name };
    Kind        kind  = Kind::None;
    int         value = 0;
    std::string name;

    auto operator<=>(const Precedence&) const = default;

    static Precedence Integer(int value) {
        return {.kind = Kind::Integer, .value = value};
    }
    static Precedence Named(std::string name) {
        return {.kind = Kind::Name, .name = std::move(name)};
    }
    [[nodiscard]] bool IsNone() const {
        return kind == Kind::None;
    }
    [[nodiscard]] std::string ToString() const;
};

struct MetadataParams {
    Precedence                   precedence;
    int                          dynamicPrecedence = 0;
    std::optional<Associativity> associativity;
    bool                         isToken     = false;
    bool                         isMainToken = false;
    std::optional<Alias>         alias;
    std::optional<std::string>   fieldName;

    auto operator<=>(const MetadataParams&) const = default;
};

// The rule tree. Choice/Seq hold `children`; Metadata, Repeat and Reserved
// hold exactly one child.
struct Rule {
    enum class Kind : std::uint8_t { Blank,
                                     String,
                                     Pattern,
                                     NamedSymbol,
                                     Symbol,
                                     Choice,
                                     Metadata,
                                     Repeat,
                                     Seq,
                                     Reserved };
    Kind              kind = Kind::Blank;
    std::string       text;  // String: literal; Pattern: regex; NamedSymbol: name; Reserved: context name
    std::string       flags; // Pattern
    compile::Symbol   symbol;
    MetadataParams    params; // Metadata
    std::vector<Rule> children;

    // Hand-written: a defaulted <=> is deleted while Rule is incomplete
    // inside its own vector<Rule> member.
    [[nodiscard]] std::strong_ordering Compare(const Rule& other) const;
    bool                               operator==(const Rule& other) const {
        return Compare(other) == std::strong_ordering::equal;
    }
    bool operator<(const Rule& other) const {
        return Compare(other) == std::strong_ordering::less;
    }

    static Rule Blank() {
        return {};
    }
    static Rule String(std::string value);
    static Rule Pattern(std::string value, std::string flags);
    static Rule Named(std::string name);
    static Rule Sym(compile::Symbol symbol);
    static Rule Choice(std::vector<Rule> rules); // flattens nested choices, drops duplicates
    static Rule Seq(std::vector<Rule> rules);
    static Rule Repeat(Rule rule);
    static Rule Reserved(Rule rule, std::string contextName);
    static Rule Metadata(Rule rule, MetadataParams params);

    static Rule Field(std::string name, Rule content);
    static Rule AliasOf(Rule content, std::string value, bool named);
    static Rule Token(Rule content);
    static Rule ImmediateToken(Rule content);
    static Rule Prec(Precedence value, Rule content);
    static Rule PrecLeft(Precedence value, Rule content);
    static Rule PrecRight(Precedence value, Rule content);
    static Rule PrecDynamic(int value, Rule content);

    [[nodiscard]] const Rule& Child() const {
        return children.front();
    }
    [[nodiscard]] bool IsEmpty() const;
};

// A set of tokens as bit vectors, one per index; eof and the end of a
// non-terminal extra are two extra bits. Equality is set equality.
class TokenSet {
  public:
    void                      Insert(Symbol symbol);
    bool                      Remove(Symbol symbol);
    [[nodiscard]] bool        Contains(Symbol symbol) const;
    [[nodiscard]] bool        ContainsTerminal(std::uint32_t index) const;
    [[nodiscard]] bool        IsEmpty() const;
    [[nodiscard]] std::size_t Len() const;
    bool                      InsertAllTerminals(const TokenSet& other);
    bool                      InsertAll(const TokenSet& other);

    // Terminals ascending, externals ascending, eof, end-of-extra.
    [[nodiscard]] std::vector<Symbol> Symbols() const;
    [[nodiscard]] std::vector<Symbol> Terminals() const;

    bool operator==(const TokenSet& other) const;
    // Lexicographic over Symbols(); any strict weak order will do for maps.
    bool operator<(const TokenSet& other) const;

    static TokenSet Of(std::initializer_list<Symbol> symbols) {
        TokenSet set;
        for (const Symbol s : symbols)
            set.Insert(s);
        return set;
    }

  private:
    std::vector<bool> terminals_;
    std::vector<bool> externals_;
    bool              eof_                   = false;
    bool              endOfNonTerminalExtra_ = false;
};

using AliasMap = std::map<Symbol, Alias>;

enum class VariableType : std::uint8_t { Hidden,
                                         Auxiliary,
                                         Anonymous,
                                         Named };

inline bool IsVisible(VariableType type) {
    return type == VariableType::Named || type == VariableType::Anonymous;
}

struct Variable {
    std::string  name;
    VariableType kind = VariableType::Named;
    Rule         rule;

    bool operator==(const Variable&) const = default;
};

struct PrecedenceEntry {
    enum class Kind : std::uint8_t { Name,
                                     Symbol };
    Kind        kind = Kind::Name;
    std::string text;

    auto                      operator<=>(const PrecedenceEntry&) const = default;
    [[nodiscard]] std::string ToString() const;
};

template <typename T>
struct ReservedWordContext {
    std::string    name;
    std::vector<T> reservedWords;
};

struct InputGrammar {
    std::string                               name;
    std::vector<Variable>                     variables;
    std::vector<Rule>                         extraSymbols;
    std::vector<std::vector<std::string>>     expectedConflicts;
    std::vector<std::vector<PrecedenceEntry>> precedenceOrderings;
    std::vector<Rule>                         externalTokens;
    std::vector<std::string>                  variablesToInline;
    std::vector<std::string>                  supertypeSymbols;
    std::optional<std::string>                wordToken;
    std::vector<ReservedWordContext<Rule>>    reservedWords;
};

// --- After token extraction ------------------------------------------------

struct LexicalVariable {
    std::string   name;
    VariableType  kind               = VariableType::Named;
    int           implicitPrecedence = 0;
    std::uint32_t startState         = 0;
};

struct LexicalGrammar {
    Nfa                          nfa;
    std::vector<LexicalVariable> variables;

    // The variable each NFA state belongs to; states are contiguous per
    // variable and each variable's start state is its last state.
    [[nodiscard]] std::size_t VariableIndexForNfaState(std::uint32_t stateId) const;
    // Distinct variable indices for a sorted state list, in order.
    [[nodiscard]] std::vector<std::size_t> VariableIndicesForNfaStates(const std::vector<std::uint32_t>& stateIds) const;
};

using ReservedWordSetId                             = std::size_t;
inline constexpr ReservedWordSetId kNoReservedWords = static_cast<ReservedWordSetId>(-1);

struct ProductionStep {
    Symbol                       symbol;
    Precedence                   precedence;
    std::optional<Associativity> associativity;
    std::optional<Alias>         alias;
    std::optional<std::string>   fieldName;
    ReservedWordSetId            reservedWordSetId = 0;

    auto operator<=>(const ProductionStep&) const = default;
};

struct Production {
    std::vector<ProductionStep> steps;
    int                         dynamicPrecedence = 0;

    bool                                operator==(const Production&) const = default;
    [[nodiscard]] std::optional<Symbol> FirstSymbol() const {
        return steps.empty() ? std::nullopt : std::optional(steps.front().symbol);
    }
};

struct SyntaxVariable {
    std::string             name;
    VariableType            kind = VariableType::Named;
    std::vector<Production> productions;

    [[nodiscard]] bool IsAuxiliary() const {
        return kind == VariableType::Auxiliary;
    }
    [[nodiscard]] bool IsHidden() const {
        return kind == VariableType::Hidden || kind == VariableType::Auxiliary;
    }
};

struct ExternalToken {
    std::string           name;
    VariableType          kind = VariableType::Named;
    std::optional<Symbol> correspondingInternalToken;
};

struct SyntaxGrammar {
    std::vector<SyntaxVariable>               variables;
    std::vector<Symbol>                       extraSymbols;
    std::vector<std::vector<Symbol>>          expectedConflicts;
    std::vector<ExternalToken>                externalTokens;
    std::vector<Symbol>                       supertypeSymbols;
    std::vector<Symbol>                       variablesToInline;
    std::optional<Symbol>                     wordToken;
    std::vector<std::vector<PrecedenceEntry>> precedenceOrderings;
    std::vector<TokenSet>                     reservedWordSets;

    [[nodiscard]] bool IsInlined(Symbol symbol) const;
};

// Productions produced by inlining, keyed by (original production, step).
struct InlinedProductionMap {
    std::vector<Production>                                                         productions;
    std::map<std::pair<const Production*, std::uint32_t>, std::vector<std::size_t>> productionMap;

    // The inlined productions for `step` of `production`, or null.
    [[nodiscard]] const std::vector<std::size_t>* InlinedProductions(const Production* production, std::uint32_t step) const;
};

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_GRAMMAR_H
