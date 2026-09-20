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

void TokenSet::Set(Words& words, std::uint32_t index) {
    const std::size_t word = index / 64;
    if (word >= words.size())
        words.resize(word + 1, 0);
    words[word] |= std::uint64_t{1} << (index % 64);
}

bool TokenSet::Clear(Words& words, std::uint32_t index) {
    if (!Get(words, index))
        return false;
    words[index / 64] &= ~(std::uint64_t{1} << (index % 64));
    while (!words.empty() && words.back() == 0)
        words.pop_back();
    return true;
}

bool TokenSet::Union(Words& words, const Words& other) {
    if (other.size() > words.size())
        words.resize(other.size(), 0);
    bool changed = false;
    for (std::size_t w = 0; w < other.size(); ++w) {
        const std::uint64_t merged = words[w] | other[w];
        changed |= merged != words[w];
        words[w] = merged;
    }
    return changed;
}

std::optional<std::uint32_t> TokenSet::FirstDifference(const Words& a, const Words& b) {
    const std::size_t n = std::max(a.size(), b.size());
    for (std::size_t w = 0; w < n; ++w) {
        const std::uint64_t x = w < a.size() ? a[w] : 0;
        const std::uint64_t y = w < b.size() ? b[w] : 0;
        if (x != y)
            return static_cast<std::uint32_t>(w * 64 + static_cast<std::size_t>(std::countr_zero(x ^ y)));
    }
    return std::nullopt;
}

bool TokenSet::AnyAbove(const Words& words, std::uint32_t index) {
    const std::size_t word = index / 64;
    if (word >= words.size())
        return false;
    const std::uint32_t shift = index % 64 + 1;
    if (shift < 64 && (words[word] >> shift) != 0)
        return true;
    return word + 1 < words.size(); // canonical: a later word is non-zero
}

void TokenSet::Insert(Symbol symbol) {
    switch (symbol.kind) {
        case SymbolType::Terminal:
            Set(terminals_, symbol.index);
            return;
        case SymbolType::External:
            Set(externals_, symbol.index);
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
            return Clear(terminals_, symbol.index);
        case SymbolType::External:
            return Clear(externals_, symbol.index);
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
            return Get(terminals_, symbol.index);
        case SymbolType::External:
            return Get(externals_, symbol.index);
        case SymbolType::End:
            return eof_;
        case SymbolType::EndOfNonTerminalExtra:
            return endOfNonTerminalExtra_;
        case SymbolType::NonTerminal:
            throw CompileError("cannot store a non-terminal in a TokenSet");
    }
    return false;
}

bool TokenSet::IsEmpty() const {
    return !eof_ && !endOfNonTerminalExtra_ && terminals_.empty() && externals_.empty();
}

std::size_t TokenSet::Len() const {
    std::size_t count = static_cast<std::size_t>(eof_) + static_cast<std::size_t>(endOfNonTerminalExtra_);
    for (const std::uint64_t word : terminals_)
        count += static_cast<std::size_t>(std::popcount(word));
    for (const std::uint64_t word : externals_)
        count += static_cast<std::size_t>(std::popcount(word));
    return count;
}

bool TokenSet::InsertAllTerminals(const TokenSet& other) {
    return Union(terminals_, other.terminals_);
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
    changed |= Union(terminals_, other.terminals_);
    changed |= Union(externals_, other.externals_);
    return changed;
}

std::vector<Symbol> TokenSet::Symbols() const {
    std::vector<Symbol> out;
    out.reserve(Len());
    ForEach([&](Symbol symbol) { out.push_back(symbol); });
    return out;
}

std::vector<Symbol> TokenSet::Terminals() const {
    std::vector<Symbol> out;
    ForEachTerminal([&](Symbol symbol) { out.push_back(symbol); });
    return out;
}

// Symbols() lists terminals, then externals, then eof, then end-of-extra,
// and Symbol orders External < End < EndOfNonTerminalExtra < Terminal. So
// at the first terminal one set has and the other lacks, the one lacking it
// is greater only if it still has a later terminal; otherwise its next
// element is a lower kind (or nothing), which sorts first.
std::strong_ordering TokenSet::Compare(const TokenSet& other) const {
    if (const auto index = FirstDifference(terminals_, other.terminals_)) {
        const bool mineHasIt      = Get(terminals_, *index);
        const bool lackerHasLater = AnyAbove(mineHasIt ? other.terminals_ : terminals_, *index);
        return (mineHasIt == lackerHasLater) ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (const auto index = FirstDifference(externals_, other.externals_)) {
        const bool      mineHasIt      = Get(externals_, *index);
        const TokenSet& lacker         = mineHasIt ? other : *this;
        const bool      lackerHasLater = AnyAbove(lacker.externals_, *index) || lacker.eof_ || lacker.endOfNonTerminalExtra_;
        return (mineHasIt == lackerHasLater) ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (eof_ != other.eof_) {
        // End sorts before EndOfNonTerminalExtra and after nothing at all.
        const TokenSet& lacker = eof_ ? other : *this;
        return (eof_ == lacker.endOfNonTerminalExtra_) ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (endOfNonTerminalExtra_ != other.endOfNonTerminalExtra_)
        return endOfNonTerminalExtra_ ? std::strong_ordering::greater : std::strong_ordering::less;
    return std::strong_ordering::equal;
}

std::size_t TokenSet::Hash() const {
    std::size_t hash = (eof_ ? 1U : 0U) | (endOfNonTerminalExtra_ ? 2U : 0U);
    for (const std::uint64_t word : terminals_)
        hash = HashCombine(hash, word);
    hash = HashCombine(hash, terminals_.size());
    for (const std::uint64_t word : externals_)
        hash = HashCombine(hash, word);
    return hash;
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
