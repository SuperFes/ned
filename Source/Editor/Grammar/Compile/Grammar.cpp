#include "Grammar.h"

#include <algorithm>

namespace ned::editor::grammar::compile {

std::string Precedence::ToString() const {
    switch (kind) {
        case Kind::Integer:
            return std::to_string(value);
        case Kind::Name:
            return "'" + name + "'";
        case Kind::None:
            break;
    }
    return "none";
}

std::string PrecedenceEntry::ToString() const {
    return kind == Kind::Name ? "'" + text + "'" : "$." + text;
}

// --- Rule --------------------------------------------------------------------

std::strong_ordering Rule::Compare(const Rule& other) const {
    if (const auto c = kind <=> other.kind; c != 0)
        return c;
    if (const auto c = text <=> other.text; c != 0)
        return c;
    if (const auto c = flags <=> other.flags; c != 0)
        return c;
    if (const auto c = symbol <=> other.symbol; c != 0)
        return c;
    if (const auto c = params <=> other.params; c != 0)
        return c;
    for (std::size_t i = 0; i < children.size() && i < other.children.size(); ++i)
        if (const auto c = children[i].Compare(other.children[i]); c != 0)
            return c;
    return children.size() <=> other.children.size();
}

Rule Rule::String(std::string value) {
    Rule rule;
    rule.kind = Kind::String;
    rule.text = std::move(value);
    return rule;
}

Rule Rule::Pattern(std::string value, std::string flags) {
    Rule rule;
    rule.kind  = Kind::Pattern;
    rule.text  = std::move(value);
    rule.flags = std::move(flags);
    return rule;
}

Rule Rule::Named(std::string name) {
    Rule rule;
    rule.kind = Kind::NamedSymbol;
    rule.text = std::move(name);
    return rule;
}

Rule Rule::Sym(compile::Symbol symbol) {
    Rule rule;
    rule.kind   = Kind::Symbol;
    rule.symbol = symbol;
    return rule;
}

namespace {
    void ChoiceHelper(std::vector<Rule>& result, Rule rule) {
        if (rule.kind == Rule::Kind::Choice) {
            for (Rule& element : rule.children)
                ChoiceHelper(result, std::move(element));
        }
        else if (std::find(result.begin(), result.end(), rule) == result.end()) {
            result.push_back(std::move(rule));
        }
    }
} // namespace

Rule Rule::Choice(std::vector<Rule> rules) {
    Rule rule;
    rule.kind = Kind::Choice;
    for (Rule& element : rules)
        ChoiceHelper(rule.children, std::move(element));
    return rule;
}

Rule Rule::Seq(std::vector<Rule> rules) {
    Rule rule;
    rule.kind     = Kind::Seq;
    rule.children = std::move(rules);
    return rule;
}

Rule Rule::Repeat(Rule content) {
    Rule rule;
    rule.kind = Kind::Repeat;
    rule.children.push_back(std::move(content));
    return rule;
}

Rule Rule::Reserved(Rule content, std::string contextName) {
    Rule rule;
    rule.kind = Kind::Reserved;
    rule.text = std::move(contextName);
    rule.children.push_back(std::move(content));
    return rule;
}

Rule Rule::Metadata(Rule content, MetadataParams params) {
    Rule rule;
    rule.kind   = Kind::Metadata;
    rule.params = std::move(params);
    rule.children.push_back(std::move(content));
    return rule;
}

namespace {
    // Adds to an existing metadata wrapper unless it is a token wrapper
    // (which stays a distinct layer), else wraps.
    template <typename F>
    Rule AddMetadata(Rule input, F&& apply) {
        if (input.kind == Rule::Kind::Metadata && !input.params.isToken) {
            apply(input.params);
            return input;
        }
        MetadataParams params;
        apply(params);
        return Rule::Metadata(std::move(input), std::move(params));
    }
} // namespace

Rule Rule::Field(std::string name, Rule content) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) { p.fieldName = std::move(name); });
}

Rule Rule::AliasOf(Rule content, std::string value, bool named) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) { p.alias = Alias{.value = std::move(value), .named = named}; });
}

Rule Rule::Token(Rule content) {
    return AddMetadata(std::move(content), [](MetadataParams& p) { p.isToken = true; });
}

Rule Rule::ImmediateToken(Rule content) {
    return AddMetadata(std::move(content), [](MetadataParams& p) {
        p.isToken     = true;
        p.isMainToken = true;
    });
}

Rule Rule::Prec(Precedence value, Rule content) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) { p.precedence = std::move(value); });
}

Rule Rule::PrecLeft(Precedence value, Rule content) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) {
        p.associativity = Associativity::Left;
        p.precedence    = std::move(value);
    });
}

Rule Rule::PrecRight(Precedence value, Rule content) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) {
        p.associativity = Associativity::Right;
        p.precedence    = std::move(value);
    });
}

Rule Rule::PrecDynamic(int value, Rule content) {
    return AddMetadata(std::move(content), [&](MetadataParams& p) { p.dynamicPrecedence = value; });
}

bool Rule::IsEmpty() const {
    switch (kind) {
        case Kind::Blank:
        case Kind::Pattern:
        case Kind::NamedSymbol:
        case Kind::Symbol:
            return false;
        case Kind::String:
            return text.empty();
        case Kind::Metadata:
        case Kind::Repeat:
        case Kind::Reserved:
            return Child().IsEmpty();
        case Kind::Choice:
            return std::any_of(children.begin(), children.end(), [](const Rule& r) { return r.IsEmpty(); });
        case Kind::Seq:
            return std::all_of(children.begin(), children.end(), [](const Rule& r) { return r.IsEmpty(); });
    }
    return false;
}

// --- TokenSet ----------------------------------------------------------------

namespace {
    void SetBit(std::vector<bool>& bits, std::uint32_t index) {
        if (index >= bits.size())
            bits.resize(index + 1, false);
        bits[index] = true;
    }
    bool GetBit(const std::vector<bool>& bits, std::uint32_t index) {
        return index < bits.size() && bits[index];
    }
    bool ClearBit(std::vector<bool>& bits, std::uint32_t index) {
        if (!GetBit(bits, index))
            return false;
        bits[index] = false;
        while (!bits.empty() && !bits.back())
            bits.pop_back();
        return true;
    }
    bool InsertBits(std::vector<bool>& into, const std::vector<bool>& from) {
        bool changed = false;
        if (from.size() > into.size())
            into.resize(from.size(), false);
        for (std::size_t i = 0; i < from.size(); ++i) {
            if (from[i] && !into[i]) {
                into[i] = true;
                changed = true;
            }
        }
        return changed;
    }
} // namespace

void TokenSet::Insert(Symbol symbol) {
    switch (symbol.kind) {
        case SymbolType::Terminal:
            SetBit(terminals_, symbol.index);
            return;
        case SymbolType::External:
            SetBit(externals_, symbol.index);
            return;
        case SymbolType::End:
            eof_ = true;
            return;
        case SymbolType::EndOfNonTerminalExtra:
            endOfNonTerminalExtra_ = true;
            return;
        case SymbolType::NonTerminal:
            throw CompileError("cannot store a non-terminal in a TokenSet");
    }
}

bool TokenSet::Remove(Symbol symbol) {
    switch (symbol.kind) {
        case SymbolType::Terminal:
            return ClearBit(terminals_, symbol.index);
        case SymbolType::External:
            return ClearBit(externals_, symbol.index);
        case SymbolType::End:
            return std::exchange(eof_, false);
        case SymbolType::EndOfNonTerminalExtra:
            return std::exchange(endOfNonTerminalExtra_, false);
        case SymbolType::NonTerminal:
            throw CompileError("cannot store a non-terminal in a TokenSet");
    }
    return false;
}

bool TokenSet::Contains(Symbol symbol) const {
    switch (symbol.kind) {
        case SymbolType::Terminal:
            return GetBit(terminals_, symbol.index);
        case SymbolType::External:
            return GetBit(externals_, symbol.index);
        case SymbolType::End:
            return eof_;
        case SymbolType::EndOfNonTerminalExtra:
            return endOfNonTerminalExtra_;
        case SymbolType::NonTerminal:
            throw CompileError("cannot store a non-terminal in a TokenSet");
    }
    return false;
}

bool TokenSet::ContainsTerminal(std::uint32_t index) const {
    return GetBit(terminals_, index);
}

bool TokenSet::IsEmpty() const {
    return !eof_ && !endOfNonTerminalExtra_ && std::none_of(terminals_.begin(), terminals_.end(), [](bool b) { return b; }) &&
           std::none_of(externals_.begin(), externals_.end(), [](bool b) { return b; });
}

std::size_t TokenSet::Len() const {
    return static_cast<std::size_t>(eof_) + static_cast<std::size_t>(endOfNonTerminalExtra_) +
           static_cast<std::size_t>(std::count(terminals_.begin(), terminals_.end(), true)) +
           static_cast<std::size_t>(std::count(externals_.begin(), externals_.end(), true));
}

bool TokenSet::InsertAllTerminals(const TokenSet& other) {
    return InsertBits(terminals_, other.terminals_);
}

bool TokenSet::InsertAll(const TokenSet& other) {
    bool changed = false;
    if (other.eof_ && !eof_) {
        eof_    = true;
        changed = true;
    }
    if (other.endOfNonTerminalExtra_ && !endOfNonTerminalExtra_) {
        endOfNonTerminalExtra_ = true;
        changed                = true;
    }
    changed |= InsertBits(terminals_, other.terminals_);
    changed |= InsertBits(externals_, other.externals_);
    return changed;
}

std::vector<Symbol> TokenSet::Symbols() const {
    std::vector<Symbol> out = Terminals();
    for (std::size_t i = 0; i < externals_.size(); ++i)
        if (externals_[i])
            out.push_back(Symbol::External(static_cast<std::uint32_t>(i)));
    if (eof_)
        out.push_back(Symbol::End());
    if (endOfNonTerminalExtra_)
        out.push_back(Symbol::EndOfNonTerminalExtra());
    return out;
}

std::vector<Symbol> TokenSet::Terminals() const {
    std::vector<Symbol> out;
    for (std::size_t i = 0; i < terminals_.size(); ++i)
        if (terminals_[i])
            out.push_back(Symbol::Terminal(static_cast<std::uint32_t>(i)));
    return out;
}

bool TokenSet::operator==(const TokenSet& other) const {
    return eof_ == other.eof_ && endOfNonTerminalExtra_ == other.endOfNonTerminalExtra_ && Terminals() == other.Terminals() &&
           Symbols() == other.Symbols();
}

bool TokenSet::operator<(const TokenSet& other) const {
    return Symbols() < other.Symbols();
}

// --- Grammars ----------------------------------------------------------------

std::size_t LexicalGrammar::VariableIndexForNfaState(std::uint32_t stateId) const {
    for (std::size_t i = 0; i < variables.size(); ++i)
        if (variables[i].startState >= stateId)
            return i;
    throw CompileError("NFA state " + std::to_string(stateId) + " belongs to no lexical variable");
}

std::vector<std::size_t> LexicalGrammar::VariableIndicesForNfaStates(const std::vector<std::uint32_t>& stateIds) const {
    std::vector<std::size_t>   out;
    std::optional<std::size_t> prev;
    for (const std::uint32_t id : stateIds) {
        const std::size_t variable = VariableIndexForNfaState(id);
        if (prev != variable) {
            out.push_back(variable);
            prev = variable;
        }
    }
    return out;
}

bool SyntaxGrammar::IsInlined(Symbol symbol) const {
    return std::find(variablesToInline.begin(), variablesToInline.end(), symbol) != variablesToInline.end();
}

const std::vector<std::size_t>* InlinedProductionMap::InlinedProductions(const Production* production, std::uint32_t step) const {
    const auto it = productionMap.find({production, step});
    return it == productionMap.end() ? nullptr : &it->second;
}

} // namespace ned::editor::grammar::compile
