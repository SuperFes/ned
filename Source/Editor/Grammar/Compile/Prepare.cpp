#include "Prepare.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

#include "Editor/Grammar/Compile/Regex.h"

namespace ned::editor::grammar::compile {

namespace {

    // --- parse_grammar ------------------------------------------------------

    using FileRule = GrammarFile::Rule;

    Rule RuleFromFile(const FileRule& in, bool isToken) {
        const auto child = [&](bool token) { return RuleFromFile(in.Child(), token); };
        switch (in.kind) {
            case FileRule::Kind::Blank:
                return Rule::Blank();
            case FileRule::Kind::Symbol:
                if (isToken)
                    throw CompileError("Grammar error: Unexpected rule `" + in.text + "` in `token()` call");
                return Rule::Named(in.text);
            case FileRule::Kind::String:
                return Rule::String(in.text);
            case FileRule::Kind::Pattern: {
                std::string flags;
                if (in.flags.find('i') != std::string::npos)
                    flags = "i";
                return Rule::Pattern(in.text, flags);
            }
            case FileRule::Kind::Seq: {
                std::vector<Rule> members;
                for (const FileRule& m : in.children)
                    members.push_back(RuleFromFile(m, isToken));
                return Rule::Seq(std::move(members));
            }
            case FileRule::Kind::Choice: {
                std::vector<Rule> members;
                for (const FileRule& m : in.children)
                    members.push_back(RuleFromFile(m, isToken));
                return Rule::Choice(std::move(members));
            }
            case FileRule::Kind::Repeat:
                return Rule::Choice({Rule::Repeat(child(isToken)), Rule::Blank()});
            case FileRule::Kind::Repeat1:
                return Rule::Repeat(child(isToken));
            case FileRule::Kind::Prec:
            case FileRule::Kind::PrecLeft:
            case FileRule::Kind::PrecRight: {
                const Precedence value = in.precedenceName.empty() ? Precedence::Integer(in.precedence) : Precedence::Named(in.precedenceName);
                if (in.kind == FileRule::Kind::Prec)
                    return Rule::Prec(value, child(isToken));
                if (in.kind == FileRule::Kind::PrecLeft)
                    return Rule::PrecLeft(value, child(isToken));
                return Rule::PrecRight(value, child(isToken));
            }
            case FileRule::Kind::PrecDynamic:
                return Rule::PrecDynamic(in.precedence, child(isToken));
            case FileRule::Kind::Token:
                return Rule::Token(child(true));
            case FileRule::Kind::TokenImmediate:
                return Rule::ImmediateToken(child(isToken));
            case FileRule::Kind::Alias:
                return Rule::AliasOf(child(isToken), in.text, in.named);
            case FileRule::Kind::Field:
                return Rule::Field(in.text, child(isToken));
            case FileRule::Kind::Reserved:
                return Rule::Reserved(child(isToken), in.text);
        }
        throw CompileError("unknown rule form");
    }

    bool RuleIsReferenced(const Rule& rule, const std::string& target, bool isExternal) {
        switch (rule.kind) {
            case Rule::Kind::NamedSymbol:
                return rule.text == target && !isExternal;
            case Rule::Kind::Choice:
            case Rule::Kind::Seq:
                return std::any_of(rule.children.begin(), rule.children.end(), [&](const Rule& r) { return RuleIsReferenced(r, target, false); });
            case Rule::Kind::Metadata:
            case Rule::Kind::Reserved:
                return RuleIsReferenced(rule.Child(), target, isExternal);
            case Rule::Kind::Repeat:
                return RuleIsReferenced(rule.Child(), target, false);
            default:
                return false;
        }
    }

    bool VariableIsUsed(const std::vector<std::pair<std::string, Rule>>& rules, const std::vector<Rule>& extras,
                        const std::vector<Rule>& externals, const std::string& target, std::set<std::string>& inProgress) {
        if (target == rules.front().first)
            return true;
        if (std::any_of(extras.begin(), extras.end(), [&](const Rule& r) { return RuleIsReferenced(r, target, false); }))
            return true;
        if (std::any_of(externals.begin(), externals.end(), [&](const Rule& r) { return RuleIsReferenced(r, target, true); }))
            return true;
        inProgress.insert(target);
        bool result = false;
        for (const auto& [name, rule] : rules) {
            if (name == target)
                continue;
            if (!RuleIsReferenced(rule, target, false) || inProgress.count(name) > 0)
                continue;
            if (VariableIsUsed(rules, extras, externals, name, inProgress)) {
                result = true;
                break;
            }
        }
        inProgress.erase(target);
        return result;
    }

    // --- validation -------------------------------------------------------

    void ValidatePrecedences(const InputGrammar& grammar) {
        // Any two entries ordered one way in one list may not be ordered the
        // other way in another.
        std::map<std::pair<PrecedenceEntry, PrecedenceEntry>, bool> pairs;
        for (const auto& list : grammar.precedenceOrderings) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                for (std::size_t j = i + 1; j < list.size(); ++j) {
                    const PrecedenceEntry* a = &list[i];
                    const PrecedenceEntry* b = &list[j];
                    if (*a == *b)
                        continue;
                    bool greater = true;
                    if (*a > *b) {
                        greater = false;
                        std::swap(a, b);
                    }
                    const auto [it, inserted] = pairs.emplace(std::make_pair(*a, *b), greater);
                    if (!inserted && it->second != greater)
                        throw CompileError("Conflicting orderings for precedences " + a->ToString() + " and " + b->ToString());
                }
            }
        }

        std::set<std::string> names;
        for (const auto& list : grammar.precedenceOrderings)
            for (const PrecedenceEntry& entry : list)
                if (entry.kind == PrecedenceEntry::Kind::Name)
                    names.insert(entry.text);

        const std::function<void(const std::string&, const Rule&)> validate = [&](const std::string& ruleName, const Rule& rule) {
            switch (rule.kind) {
                case Rule::Kind::Repeat:
                    validate(ruleName, rule.Child());
                    break;
                case Rule::Kind::Seq:
                case Rule::Kind::Choice:
                    for (const Rule& e : rule.children)
                        validate(ruleName, e);
                    break;
                case Rule::Kind::Metadata:
                    if (rule.params.precedence.kind == Precedence::Kind::Name && names.count(rule.params.precedence.name) == 0)
                        throw CompileError("Undeclared precedence '" + rule.params.precedence.name + "' in rule '" + ruleName + "'");
                    validate(ruleName, rule.Child());
                    break;
                default:
                    break;
            }
        };
        for (const Variable& variable : grammar.variables)
            validate(variable.name, variable.rule);
    }

    std::set<std::string> SingleSymbolProductions(const Rule& rule) {
        switch (rule.kind) {
            case Rule::Kind::NamedSymbol:
                return {rule.text};
            case Rule::Kind::Choice: {
                std::set<std::string> out;
                for (const Rule& c : rule.children) {
                    auto inner = SingleSymbolProductions(c);
                    out.insert(inner.begin(), inner.end());
                }
                return out;
            }
            case Rule::Kind::Metadata:
                return SingleSymbolProductions(rule.Child());
            default:
                return {};
        }
    }

    void ValidateIndirectRecursion(const InputGrammar& grammar) {
        std::vector<std::pair<std::string, std::set<std::string>>> transitions;
        std::map<std::string, std::size_t>                         index;
        for (const Variable& variable : grammar.variables) {
            std::set<std::string> productions = SingleSymbolProductions(variable.rule);
            productions.erase(variable.name);
            index[variable.name] = transitions.size();
            transitions.emplace_back(variable.name, std::move(productions));
        }

        std::set<std::string>                         visited;
        std::vector<std::string>                      path;
        const std::function<bool(const std::string&)> findCycle = [&](const std::string& current) {
            if (const auto first = std::find(path.begin(), path.end(), current); first != path.end()) {
                path.push_back(current);
                path.erase(path.begin(), first);
                return true;
            }
            if (visited.count(current) > 0)
                return false;
            path.push_back(current);
            visited.insert(current);
            if (const auto it = index.find(current); it != index.end()) {
                for (const std::string& next : transitions[it->second].second)
                    if (findCycle(next))
                        return true;
            }
            path.pop_back();
            return false;
        };
        for (const auto& [start, _] : transitions) {
            visited.clear();
            path.clear();
            if (findCycle(start)) {
                std::string message = "Grammar contains an indirectly recursive rule: ";
                for (std::size_t i = 0; i < path.size(); ++i)
                    message += (i > 0 ? " -> " : "") + path[i];
                throw CompileError(message);
            }
        }
    }

    // --- intern_symbols ---------------------------------------------------

    template <typename T, typename U>
    struct IntermediateGrammar {
        std::vector<Variable>                     variables;
        std::vector<T>                            extraSymbols;
        std::vector<std::vector<Symbol>>          expectedConflicts;
        std::vector<std::vector<PrecedenceEntry>> precedenceOrderings;
        std::vector<U>                            externalTokens;
        std::vector<Symbol>                       variablesToInline;
        std::vector<Symbol>                       supertypeSymbols;
        std::optional<Symbol>                     wordToken;
        std::vector<ReservedWordContext<T>>       reservedWordSets;
    };

    using InternedGrammar        = IntermediateGrammar<Rule, Variable>;
    using ExtractedSyntaxGrammar = IntermediateGrammar<Symbol, ExternalToken>;

    struct ExtractedLexicalGrammar {
        std::vector<Variable> variables;
        std::vector<Rule>     separators;
    };

    VariableType VariableTypeForName(const std::string& name) {
        return name.starts_with('_') ? VariableType::Hidden : VariableType::Named;
    }

    class Interner {
      public:
        explicit Interner(const InputGrammar& grammar) : grammar_(grammar) {
        }

        Rule InternRule(const Rule& rule) const {
            switch (rule.kind) {
                case Rule::Kind::Choice:
                case Rule::Kind::Seq: {
                    Rule out = rule;
                    out.children.clear();
                    for (const Rule& e : rule.children)
                        out.children.push_back(InternRule(e));
                    return out;
                }
                case Rule::Kind::Repeat:
                    return Rule::Repeat(InternRule(rule.Child()));
                case Rule::Kind::Metadata:
                    return Rule::Metadata(InternRule(rule.Child()), rule.params);
                case Rule::Kind::Reserved:
                    return Rule::Reserved(InternRule(rule.Child()), rule.text);
                case Rule::Kind::NamedSymbol: {
                    const std::optional<Symbol> symbol = InternName(rule.text);
                    if (!symbol)
                        throw CompileError("Undefined symbol `" + rule.text + "`");
                    return Rule::Sym(*symbol);
                }
                default:
                    return rule;
            }
        }

        std::optional<Symbol> InternName(const std::string& name) const {
            for (std::size_t i = 0; i < grammar_.variables.size(); ++i)
                if (grammar_.variables[i].name == name)
                    return Symbol::NonTerminal(static_cast<std::uint32_t>(i));
            for (std::size_t i = 0; i < grammar_.externalTokens.size(); ++i) {
                const Rule& external = grammar_.externalTokens[i];
                if (external.kind == Rule::Kind::NamedSymbol && external.text == name)
                    return Symbol::External(static_cast<std::uint32_t>(i));
            }
            return std::nullopt;
        }

      private:
        const InputGrammar& grammar_;
    };

    InternedGrammar InternSymbols(const InputGrammar& grammar) {
        const Interner interner(grammar);
        if (VariableTypeForName(grammar.variables.front().name) == VariableType::Hidden)
            throw CompileError("A grammar's start rule must be visible.");

        InternedGrammar out;
        for (const Variable& variable : grammar.variables)
            out.variables.push_back({.name = variable.name, .kind = VariableTypeForName(variable.name), .rule = interner.InternRule(variable.rule)});

        for (const Rule& external : grammar.externalTokens) {
            Rule         rule = interner.InternRule(external);
            std::string  name;
            VariableType kind = VariableType::Anonymous;
            if (external.kind == Rule::Kind::NamedSymbol) {
                name = external.text;
                kind = VariableTypeForName(name);
            }
            out.externalTokens.push_back({.name = std::move(name), .kind = kind, .rule = std::move(rule)});
        }

        for (const Rule& extra : grammar.extraSymbols)
            out.extraSymbols.push_back(interner.InternRule(extra));

        for (const std::string& name : grammar.supertypeSymbols) {
            const std::optional<Symbol> symbol = interner.InternName(name);
            if (!symbol)
                throw CompileError("Undefined symbol `" + name + "` in grammar's supertypes array");
            out.supertypeSymbols.push_back(*symbol);
        }

        for (const ReservedWordContext<Rule>& context : grammar.reservedWords) {
            ReservedWordContext<Rule> interned{.name = context.name};
            for (const Rule& rule : context.reservedWords)
                interned.reservedWords.push_back(interner.InternRule(rule));
            out.reservedWordSets.push_back(std::move(interned));
        }

        for (const std::vector<std::string>& conflict : grammar.expectedConflicts) {
            std::vector<Symbol> interned;
            for (const std::string& name : conflict) {
                const std::optional<Symbol> symbol = interner.InternName(name);
                if (!symbol)
                    throw CompileError("Undefined symbol `" + name + "` in grammar's conflicts array");
                interned.push_back(*symbol);
            }
            out.expectedConflicts.push_back(std::move(interned));
        }

        for (const std::string& name : grammar.variablesToInline)
            if (const std::optional<Symbol> symbol = interner.InternName(name))
                out.variablesToInline.push_back(*symbol);

        if (grammar.wordToken) {
            const std::optional<Symbol> symbol = interner.InternName(*grammar.wordToken);
            if (!symbol)
                throw CompileError("Undefined symbol `" + *grammar.wordToken + "` as grammar's word token");
            out.wordToken = symbol;
        }

        for (std::size_t i = 0; i < out.variables.size(); ++i)
            if (std::find(out.supertypeSymbols.begin(), out.supertypeSymbols.end(), Symbol::NonTerminal(static_cast<std::uint32_t>(i))) != out.supertypeSymbols.end())
                out.variables[i].kind = VariableType::Hidden;

        out.precedenceOrderings = grammar.precedenceOrderings;
        return out;
    }

    // --- extract_tokens ---------------------------------------------------

    class TokenExtractor {
      public:
        std::vector<Variable>    extractedVariables;
        std::vector<std::size_t> extractedUsageCounts;

        void ExtractTokensInVariable(bool isFirst, Variable& variable) {
            currentVariableName_       = variable.name;
            currentVariableTokenCount_ = 0;
            isFirstRule_               = isFirst;
            variable.rule              = ExtractTokensInRule(variable.rule);
        }

      private:
        std::string currentVariableName_;
        std::size_t currentVariableTokenCount_ = 0;
        bool        isFirstRule_               = false;

        Rule ExtractTokensInRule(const Rule& input) {
            switch (input.kind) {
                case Rule::Kind::String:
                    return Rule::Sym(ExtractToken(input, &input.text));
                case Rule::Kind::Pattern:
                    return Rule::Sym(ExtractToken(input, nullptr));
                case Rule::Kind::Metadata: {
                    if (input.params.isToken) {
                        MetadataParams params            = input.params;
                        params.isToken                   = false;
                        const std::string* stringValue   = input.Child().kind == Rule::Kind::String ? &input.Child().text : nullptr;
                        const Rule&        ruleToExtract = params == MetadataParams{} ? input.Child() : input;
                        return Rule::Sym(ExtractToken(ruleToExtract, stringValue));
                    }
                    return Rule::Metadata(ExtractTokensInRule(input.Child()), input.params);
                }
                case Rule::Kind::Repeat:
                    return Rule::Repeat(ExtractTokensInRule(input.Child()));
                case Rule::Kind::Seq:
                case Rule::Kind::Choice: {
                    Rule out = input;
                    out.children.clear();
                    for (const Rule& e : input.children)
                        out.children.push_back(ExtractTokensInRule(e));
                    return out;
                }
                case Rule::Kind::Reserved:
                    return Rule::Reserved(ExtractTokensInRule(input.Child()), input.text);
                default:
                    return input;
            }
        }

        Symbol ExtractToken(const Rule& rule, const std::string* stringValue) {
            for (std::size_t i = 0; i < extractedVariables.size(); ++i) {
                if (extractedVariables[i].rule == rule) {
                    extractedUsageCounts[i]++;
                    return Symbol::Terminal(static_cast<std::uint32_t>(i));
                }
            }
            const auto index = static_cast<std::uint32_t>(extractedVariables.size());
            if (stringValue != nullptr) {
                if (stringValue->empty() && !isFirstRule_)
                    throw CompileError("The rule `" + currentVariableName_ + "` contains an empty string.\n\nTree-sitter does not support syntactic rules that contain an empty string\nunless they are used only as the grammar's start rule.\n");
                extractedVariables.push_back({.name = *stringValue, .kind = VariableType::Anonymous, .rule = rule});
            }
            else {
                currentVariableTokenCount_++;
                extractedVariables.push_back({.name = currentVariableName_ + "_token" + std::to_string(currentVariableTokenCount_),
                                              .kind = VariableType::Auxiliary,
                                              .rule = rule});
            }
            extractedUsageCounts.push_back(1);
            return Symbol::Terminal(index);
        }
    };

    class SymbolReplacer {
      public:
        std::map<std::size_t, std::size_t> replacements;

        Rule ReplaceSymbolsInRule(const Rule& rule) const {
            switch (rule.kind) {
                case Rule::Kind::Symbol:
                    return Rule::Sym(ReplaceSymbol(rule.symbol));
                case Rule::Kind::Choice:
                case Rule::Kind::Seq: {
                    Rule out = rule;
                    out.children.clear();
                    for (const Rule& e : rule.children)
                        out.children.push_back(ReplaceSymbolsInRule(e));
                    return out;
                }
                case Rule::Kind::Repeat:
                    return Rule::Repeat(ReplaceSymbolsInRule(rule.Child()));
                case Rule::Kind::Metadata:
                    return Rule::Metadata(ReplaceSymbolsInRule(rule.Child()), rule.params);
                case Rule::Kind::Reserved:
                    return Rule::Reserved(ReplaceSymbolsInRule(rule.Child()), rule.text);
                default:
                    return rule;
            }
        }

        Symbol ReplaceSymbol(Symbol symbol) const {
            if (!symbol.IsNonTerminal())
                return symbol;
            if (const auto it = replacements.find(symbol.index); it != replacements.end())
                return Symbol::Terminal(static_cast<std::uint32_t>(it->second));
            std::uint32_t adjusted = symbol.index;
            for (const auto& [replaced, _] : replacements)
                if (replaced < symbol.index)
                    adjusted--;
            return Symbol::NonTerminal(adjusted);
        }
    };

    std::pair<ExtractedSyntaxGrammar, ExtractedLexicalGrammar> ExtractTokens(InternedGrammar grammar) {
        TokenExtractor extractor;
        for (std::size_t i = 0; i < grammar.variables.size(); ++i)
            extractor.ExtractTokensInVariable(i == 0, grammar.variables[i]);
        for (Variable& variable : grammar.externalTokens)
            extractor.ExtractTokensInVariable(false, variable);

        std::vector<Variable> lexicalVariables = std::move(extractor.extractedVariables);

        // A variable whose whole rule became a token used nowhere else moves
        // into the lexical grammar under its own name.
        std::vector<Variable> variables;
        SymbolReplacer        replacer;
        for (std::size_t i = 0; i < grammar.variables.size(); ++i) {
            Variable& variable = grammar.variables[i];
            if (variable.rule.kind == Rule::Kind::Symbol && variable.rule.symbol.IsTerminal()) {
                const std::size_t index = variable.rule.symbol.index;
                if (i > 0 && extractor.extractedUsageCounts[index] == 1) {
                    Variable& lexicalVariable = lexicalVariables[index];
                    if (lexicalVariable.kind == VariableType::Auxiliary || variable.kind != VariableType::Hidden) {
                        lexicalVariable.kind = variable.kind;
                        lexicalVariable.name = variable.name;
                        replacer.replacements.emplace(i, index);
                        continue;
                    }
                }
            }
            variables.push_back(std::move(variable));
        }
        for (Variable& variable : variables)
            variable.rule = replacer.ReplaceSymbolsInRule(variable.rule);

        ExtractedSyntaxGrammar syntax;
        for (const std::vector<Symbol>& conflict : grammar.expectedConflicts) {
            std::vector<Symbol> result;
            for (const Symbol symbol : conflict)
                result.push_back(replacer.ReplaceSymbol(symbol));
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            syntax.expectedConflicts.push_back(std::move(result));
        }
        for (const Symbol symbol : grammar.supertypeSymbols)
            syntax.supertypeSymbols.push_back(replacer.ReplaceSymbol(symbol));
        for (const Symbol symbol : grammar.variablesToInline)
            syntax.variablesToInline.push_back(replacer.ReplaceSymbol(symbol));

        ExtractedLexicalGrammar lexical;
        for (const Rule& rule : grammar.extraSymbols) {
            if (rule.kind == Rule::Kind::Symbol) {
                syntax.extraSymbols.push_back(replacer.ReplaceSymbol(rule.symbol));
            }
            else if (const auto it = std::find_if(lexicalVariables.begin(), lexicalVariables.end(), [&](const Variable& v) { return v.rule == rule; });
                     it != lexicalVariables.end()) {
                syntax.extraSymbols.push_back(Symbol::Terminal(static_cast<std::uint32_t>(it - lexicalVariables.begin())));
            }
            else {
                lexical.separators.push_back(rule);
            }
        }

        for (const Variable& external : grammar.externalTokens) {
            const Rule rule = replacer.ReplaceSymbolsInRule(external.rule);
            if (rule.kind != Rule::Kind::Symbol)
                throw CompileError("Non-symbol rules cannot be used as external tokens");
            if (rule.symbol.IsNonTerminal())
                throw CompileError("Rule '" + variables[rule.symbol.index].name + "' cannot be used as both an external token and a non-terminal rule");
            if (rule.symbol.IsExternal())
                syntax.externalTokens.push_back({.name = external.name, .kind = external.kind, .correspondingInternalToken = std::nullopt});
            else
                syntax.externalTokens.push_back({.name = lexicalVariables[rule.symbol.index].name, .kind = external.kind, .correspondingInternalToken = rule.symbol});
        }

        if (grammar.wordToken) {
            const Symbol token = replacer.ReplaceSymbol(*grammar.wordToken);
            if (token.IsNonTerminal()) {
                const Variable& wordVariable = variables[token.index];
                std::string     message      = "Non-terminal symbol '" + wordVariable.name + "' cannot be used as the word token";
                for (std::size_t i = 0; i < variables.size(); ++i) {
                    if (i != token.index && variables[i].rule == wordVariable.rule) {
                        message += ", because its rule is duplicated in '" + variables[i].name + "'";
                        break;
                    }
                }
                throw CompileError(message + "\n");
            }
            syntax.wordToken = token;
        }

        for (const ReservedWordContext<Rule>& context : grammar.reservedWordSets) {
            ReservedWordContext<Symbol> out{.name = context.name};
            for (const Rule& reserved : context.reservedWords) {
                if (reserved.kind == Rule::Kind::Symbol) {
                    out.reservedWords.push_back(replacer.ReplaceSymbol(reserved.symbol));
                }
                else if (const auto it = std::find_if(lexicalVariables.begin(), lexicalVariables.end(), [&](const Variable& v) { return v.rule == reserved; });
                         it != lexicalVariables.end()) {
                    out.reservedWords.push_back(Symbol::Terminal(static_cast<std::uint32_t>(it - lexicalVariables.begin())));
                }
                else {
                    const std::string name = reserved.kind == Rule::Kind::String || reserved.kind == Rule::Kind::Pattern ? reserved.text : "unknown";
                    throw CompileError("Reserved word '" + name + "' must be a token");
                }
            }
            syntax.reservedWordSets.push_back(std::move(out));
        }

        syntax.variables           = std::move(variables);
        syntax.precedenceOrderings = std::move(grammar.precedenceOrderings);
        lexical.variables          = std::move(lexicalVariables);
        return {std::move(syntax), std::move(lexical)};
    }

    // --- expand_repeats -----------------------------------------------------

    class RepeatExpander {
      public:
        explicit RepeatExpander(std::size_t precedingSymbolCount) : precedingSymbolCount_(precedingSymbolCount) {
        }

        std::vector<Variable> auxiliaryVariables;

        // Returns true when a hidden variable's top-level repeat became the
        // variable itself (which then cannot be inlined).
        bool ExpandVariable(std::size_t index, Variable& variable) {
            variableName_          = variable.name;
            repeatCountInVariable_ = 0;
            Rule rule              = std::move(variable.rule);
            variable.rule          = Rule::Blank();
            if (variable.kind == VariableType::Hidden && rule.kind == Rule::Kind::Repeat) {
                Rule inner    = ExpandRule(rule.Child());
                variable.rule = WrapInBinaryTree(Symbol::NonTerminal(static_cast<std::uint32_t>(index)), std::move(inner));
                variable.kind = VariableType::Auxiliary;
                return true;
            }
            variable.rule = ExpandRule(rule);
            return false;
        }

      private:
        std::string            variableName_;
        std::size_t            repeatCountInVariable_ = 0;
        std::size_t            precedingSymbolCount_;
        std::map<Rule, Symbol> existingRepeats_;

        Rule ExpandRule(const Rule& rule) {
            switch (rule.kind) {
                case Rule::Kind::Choice:
                case Rule::Kind::Seq: {
                    Rule out = rule;
                    out.children.clear();
                    for (const Rule& e : rule.children)
                        out.children.push_back(ExpandRule(e));
                    return out;
                }
                case Rule::Kind::Metadata:
                    return Rule::Metadata(ExpandRule(rule.Child()), rule.params);
                case Rule::Kind::Repeat: {
                    Rule inner = ExpandRule(rule.Child());
                    if (const auto it = existingRepeats_.find(inner); it != existingRepeats_.end())
                        return Rule::Sym(it->second);
                    repeatCountInVariable_++;
                    const Symbol repeatSymbol = Symbol::NonTerminal(static_cast<std::uint32_t>(precedingSymbolCount_ + auxiliaryVariables.size()));
                    existingRepeats_.emplace(inner, repeatSymbol);
                    auxiliaryVariables.push_back({.name = variableName_ + "_repeat" + std::to_string(repeatCountInVariable_),
                                                  .kind = VariableType::Auxiliary,
                                                  .rule = WrapInBinaryTree(repeatSymbol, std::move(inner))});
                    return Rule::Sym(repeatSymbol);
                }
                default:
                    return rule;
            }
        }

        static Rule WrapInBinaryTree(Symbol symbol, Rule rule) {
            return Rule::Choice({Rule::Seq({Rule::Sym(symbol), Rule::Sym(symbol)}), std::move(rule)});
        }
    };

    ExtractedSyntaxGrammar ExpandRepeats(ExtractedSyntaxGrammar grammar) {
        RepeatExpander expander(grammar.variables.size());
        for (std::size_t i = 0; i < grammar.variables.size(); ++i) {
            if (expander.ExpandVariable(i, grammar.variables[i])) {
                const Symbol symbol = Symbol::NonTerminal(static_cast<std::uint32_t>(i));
                grammar.variablesToInline.erase(std::remove(grammar.variablesToInline.begin(), grammar.variablesToInline.end(), symbol),
                                                grammar.variablesToInline.end());
            }
        }
        for (Variable& aux : expander.auxiliaryVariables)
            grammar.variables.push_back(std::move(aux));
        return grammar;
    }

    // --- flatten_grammar ----------------------------------------------------

    class RuleFlattener {
      public:
        explicit RuleFlattener(std::map<std::string, ReservedWordSetId> reservedWordSetIds) : reservedWordSetIds_(std::move(reservedWordSetIds)) {
        }

        SyntaxVariable FlattenVariable(Variable variable) {
            std::vector<Production> productions;
            for (Rule& rule : ExtractChoices(std::move(variable.rule))) {
                Production production = FlattenRule(std::move(rule));
                if (std::find(productions.begin(), productions.end(), production) == productions.end())
                    productions.push_back(std::move(production));
            }
            return {.name = std::move(variable.name), .kind = variable.kind, .productions = std::move(productions)};
        }

      private:
        Production                               production_;
        std::map<std::string, ReservedWordSetId> reservedWordSetIds_;
        std::vector<Precedence>                  precedenceStack_;
        std::vector<Associativity>               associativityStack_;
        std::vector<ReservedWordSetId>           reservedWordStack_;
        std::vector<Alias>                       aliasStack_;
        std::vector<std::string>                 fieldNameStack_;

        Production FlattenRule(Rule rule) {
            production_ = Production{};
            aliasStack_.clear();
            reservedWordStack_.clear();
            precedenceStack_.clear();
            associativityStack_.clear();
            fieldNameStack_.clear();
            Apply(std::move(rule), true);
            return production_;
        }

        bool Apply(Rule rule, bool atEnd) {
            switch (rule.kind) {
                case Rule::Kind::Seq: {
                    bool              result    = false;
                    const std::size_t lastIndex = rule.children.size() - 1;
                    for (std::size_t i = 0; i < rule.children.size(); ++i)
                        result |= Apply(std::move(rule.children[i]), i == lastIndex && atEnd);
                    return result;
                }
                case Rule::Kind::Metadata: {
                    MetadataParams& params        = rule.params;
                    const bool      hasPrecedence = !params.precedence.IsNone();
                    if (hasPrecedence)
                        precedenceStack_.push_back(params.precedence);
                    const bool hasAssociativity = params.associativity.has_value();
                    if (hasAssociativity)
                        associativityStack_.push_back(*params.associativity);
                    const bool hasAlias = params.alias.has_value();
                    if (hasAlias)
                        aliasStack_.push_back(*params.alias);
                    const bool hasFieldName = params.fieldName.has_value();
                    if (hasFieldName)
                        fieldNameStack_.push_back(*params.fieldName);
                    if (std::abs(params.dynamicPrecedence) > std::abs(production_.dynamicPrecedence))
                        production_.dynamicPrecedence = params.dynamicPrecedence;

                    const bool didPush = Apply(std::move(rule.children.front()), atEnd);

                    if (hasPrecedence) {
                        precedenceStack_.pop_back();
                        if (didPush && !atEnd)
                            production_.steps.back().precedence = precedenceStack_.empty() ? Precedence{} : precedenceStack_.back();
                    }
                    if (hasAssociativity) {
                        associativityStack_.pop_back();
                        if (didPush && !atEnd)
                            production_.steps.back().associativity = associativityStack_.empty() ? std::nullopt : std::optional(associativityStack_.back());
                    }
                    if (hasAlias)
                        aliasStack_.pop_back();
                    if (hasFieldName)
                        fieldNameStack_.pop_back();
                    return didPush;
                }
                case Rule::Kind::Reserved: {
                    const auto it = reservedWordSetIds_.find(rule.text);
                    if (it == reservedWordSetIds_.end())
                        throw CompileError("No such reserved word set: " + rule.text);
                    reservedWordStack_.push_back(it->second);
                    const bool didPush = Apply(std::move(rule.children.front()), atEnd);
                    reservedWordStack_.pop_back();
                    return didPush;
                }
                case Rule::Kind::Symbol:
                    production_.steps.push_back(ProductionStep{
                        .symbol            = rule.symbol,
                        .precedence        = precedenceStack_.empty() ? Precedence{} : precedenceStack_.back(),
                        .associativity     = associativityStack_.empty() ? std::nullopt : std::optional(associativityStack_.back()),
                        .alias             = aliasStack_.empty() ? std::nullopt : std::optional(aliasStack_.back()),
                        .fieldName         = fieldNameStack_.empty() ? std::nullopt : std::optional(fieldNameStack_.back()),
                        .reservedWordSetId = reservedWordStack_.empty() ? ReservedWordSetId{0} : reservedWordStack_.back(),
                    });
                    return true;
                default:
                    return false;
            }
        }

        static std::vector<Rule> ExtractChoices(Rule rule) {
            switch (rule.kind) {
                case Rule::Kind::Seq: {
                    std::vector<Rule> result = {Rule::Blank()};
                    for (Rule& element : rule.children) {
                        const std::vector<Rule> extraction = ExtractChoices(std::move(element));
                        std::vector<Rule>       next;
                        for (const Rule& entry : result)
                            for (const Rule& extracted : extraction)
                                next.push_back(Rule::Seq({entry, extracted}));
                        result = std::move(next);
                    }
                    return result;
                }
                case Rule::Kind::Choice: {
                    std::vector<Rule> result;
                    for (Rule& element : rule.children)
                        for (Rule& r : ExtractChoices(std::move(element)))
                            result.push_back(std::move(r));
                    return result;
                }
                case Rule::Kind::Metadata: {
                    std::vector<Rule> result;
                    for (Rule& r : ExtractChoices(std::move(rule.children.front())))
                        result.push_back(Rule::Metadata(std::move(r), rule.params));
                    return result;
                }
                case Rule::Kind::Reserved: {
                    std::vector<Rule> result;
                    for (Rule& r : ExtractChoices(std::move(rule.children.front())))
                        result.push_back(Rule::Reserved(std::move(r), rule.text));
                    return result;
                }
                default:
                    return {std::move(rule)};
            }
        }
    };

    bool SymbolIsUsed(const std::vector<SyntaxVariable>& variables, Symbol symbol) {
        for (const SyntaxVariable& variable : variables)
            for (const Production& production : variable.productions)
                for (const ProductionStep& step : production.steps)
                    if (step.symbol == symbol)
                        return true;
        return false;
    }

    SyntaxGrammar FlattenGrammar(ExtractedSyntaxGrammar grammar) {
        std::map<std::string, ReservedWordSetId> idsByName;
        for (std::size_t i = 0; i < grammar.reservedWordSets.size(); ++i)
            idsByName[grammar.reservedWordSets[i].name] = i;

        RuleFlattener               flattener(std::move(idsByName));
        std::vector<SyntaxVariable> variables;
        for (Variable& variable : grammar.variables)
            variables.push_back(flattener.FlattenVariable(std::move(variable)));

        for (std::size_t i = 0; i < variables.size(); ++i) {
            const Symbol symbol  = Symbol::NonTerminal(static_cast<std::uint32_t>(i));
            const bool   used    = SymbolIsUsed(variables, symbol);
            const bool   inlined = std::find(grammar.variablesToInline.begin(), grammar.variablesToInline.end(), symbol) != grammar.variablesToInline.end();
            for (const Production& production : variables[i].productions) {
                if (used && production.steps.empty())
                    throw CompileError("The rule `" + variables[i].name + "` matches the empty string.\n\nTree-sitter does not support syntactic rules that match the empty string\nunless they are used only as the grammar's start rule.\n");
                if (inlined && std::any_of(production.steps.begin(), production.steps.end(), [&](const ProductionStep& s) { return s.symbol == symbol; }))
                    throw CompileError("Rule `" + variables[i].name + "` cannot be inlined because it contains a reference to itself");
            }
        }

        std::vector<TokenSet> reservedWordSets;
        for (const ReservedWordContext<Symbol>& context : grammar.reservedWordSets) {
            TokenSet set;
            for (const Symbol symbol : context.reservedWords)
                set.Insert(symbol);
            reservedWordSets.push_back(std::move(set));
        }
        if (reservedWordSets.empty())
            reservedWordSets.emplace_back();

        return SyntaxGrammar{
            .variables           = std::move(variables),
            .extraSymbols        = std::move(grammar.extraSymbols),
            .expectedConflicts   = std::move(grammar.expectedConflicts),
            .externalTokens      = std::move(grammar.externalTokens),
            .supertypeSymbols    = std::move(grammar.supertypeSymbols),
            .variablesToInline   = std::move(grammar.variablesToInline),
            .wordToken           = grammar.wordToken,
            .precedenceOrderings = std::move(grammar.precedenceOrderings),
            .reservedWordSets    = std::move(reservedWordSets),
        };
    }

    // --- expand_tokens ------------------------------------------------------

    int ImplicitPrecedence(const Rule& rule) {
        switch (rule.kind) {
            case Rule::Kind::String:
                return 2;
            case Rule::Kind::Metadata:
                return ImplicitPrecedence(rule.Child()) + (rule.params.isMainToken ? 1 : 0);
            default:
                return 0;
        }
    }

    int CompletionPrecedence(const Rule& rule) {
        if (rule.kind == Rule::Kind::Metadata && rule.params.precedence.kind == Precedence::Kind::Integer)
            return rule.params.precedence.value;
        return 0;
    }

    // The reference substitutes the ASCII meanings of \w \s \d textually
    // before parsing (Unicode mode would make them far larger).
    std::string SubstituteAsciiClasses(std::string pattern) {
        const auto replaceAll = [&](std::string_view from, std::string_view to) {
            std::size_t pos = 0;
            while ((pos = pattern.find(from, pos)) != std::string::npos) {
                pattern.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replaceAll("\\w", "[0-9A-Za-z_]");
        replaceAll("\\s", "[\\t-\\r ]");
        replaceAll("\\d", "[0-9]");
        replaceAll("\\W", "[^0-9A-Za-z_]");
        replaceAll("\\S", "[^\\t-\\r ]");
        replaceAll("\\D", "[^0-9]");
        return pattern;
    }

    class NfaBuilder {
      public:
        Nfa  nfa;
        bool isSep = true;

        NfaBuilder() : precedenceStack_{0} {
        }

        bool ExpandRule(const Rule& rule, std::uint32_t nextStateId) {
            switch (rule.kind) {
                case Rule::Kind::Pattern: {
                    const RegexNode regex = ParseRegex(SubstituteAsciiClasses(rule.text), rule.flags.find('i') != std::string::npos);
                    return ExpandRegex(regex, nextStateId);
                }
                case Rule::Kind::String: {
                    // Codepoints of the literal, in reverse.
                    std::vector<std::uint32_t> codepoints = Decode(rule.text);
                    for (auto it = codepoints.rbegin(); it != codepoints.rend(); ++it) {
                        PushAdvance(CharacterSet::FromChar(*it), nextStateId);
                        nextStateId = nfa.LastStateId();
                    }
                    return !rule.text.empty();
                }
                case Rule::Kind::Choice: {
                    std::vector<std::uint32_t> alternatives;
                    for (const Rule& element : rule.children)
                        alternatives.push_back(ExpandRule(element, nextStateId) ? nfa.LastStateId() : nextStateId);
                    FinishAlternatives(alternatives);
                    return true;
                }
                case Rule::Kind::Seq: {
                    bool result = false;
                    for (auto it = rule.children.rbegin(); it != rule.children.rend(); ++it) {
                        if (ExpandRule(*it, nextStateId))
                            result = true;
                        nextStateId = nfa.LastStateId();
                    }
                    return result;
                }
                case Rule::Kind::Repeat: {
                    nfa.states.push_back(NfaState::Accept(0, 0)); // placeholder for the split
                    const std::uint32_t splitStateId = nfa.LastStateId();
                    if (ExpandRule(rule.Child(), splitStateId)) {
                        nfa.states[splitStateId] = NfaState::Split(nfa.LastStateId(), nextStateId);
                        return true;
                    }
                    return false;
                }
                case Rule::Kind::Metadata: {
                    const bool hasPrecedence = rule.params.precedence.kind == Precedence::Kind::Integer;
                    if (hasPrecedence)
                        precedenceStack_.push_back(rule.params.precedence.value);
                    const bool result = ExpandRule(rule.Child(), nextStateId);
                    if (hasPrecedence)
                        precedenceStack_.pop_back();
                    return result;
                }
                case Rule::Kind::Blank:
                    return false;
                default:
                    throw CompileError("Grammar error: Unexpected rule inside a token");
            }
        }

      private:
        std::vector<int> precedenceStack_;

        static std::vector<std::uint32_t> Decode(const std::string& text) {
            std::vector<std::uint32_t> out;
            for (std::size_t i = 0; i < text.size();) {
                const auto    lead = static_cast<unsigned char>(text[i]);
                std::uint32_t cp   = lead;
                std::size_t   len  = 1;
                if (lead >= 0xF0) {
                    cp  = lead & 0x07;
                    len = 4;
                }
                else if (lead >= 0xE0) {
                    cp  = lead & 0x0F;
                    len = 3;
                }
                else if (lead >= 0xC0) {
                    cp  = lead & 0x1F;
                    len = 2;
                }
                for (std::size_t k = 1; k < len && i + k < text.size(); ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
                out.push_back(cp);
                i += len;
            }
            return out;
        }

        void FinishAlternatives(std::vector<std::uint32_t> alternatives) {
            std::sort(alternatives.begin(), alternatives.end());
            alternatives.erase(std::unique(alternatives.begin(), alternatives.end()), alternatives.end());
            const std::uint32_t last = nfa.LastStateId();
            alternatives.erase(std::remove(alternatives.begin(), alternatives.end(), last), alternatives.end());
            for (const std::uint32_t id : alternatives)
                PushSplit(id);
        }

        bool ExpandRegex(const RegexNode& node, std::uint32_t nextStateId) {
            switch (node.kind) {
                case RegexNode::Kind::Empty:
                    return false;
                case RegexNode::Kind::Class: {
                    CharacterSet chars = node.set;
                    // The reference strips the long s that folding an `s`
                    // drags in.
                    if (chars.RangeCount() == 3 && chars.Ranges()[0] == CodepointRange{'S', 'S' + 1} && chars.Ranges()[1] == CodepointRange{'s', 's' + 1} &&
                        chars.Ranges()[2] == CodepointRange{0x17F, 0x180})
                        chars = chars.Difference(CharacterSet::FromChar(0x17F));
                    PushAdvance(std::move(chars), nextStateId);
                    return true;
                }
                case RegexNode::Kind::Repetition: {
                    const RegexNode& sub = node.children.front();
                    if (node.min == 0 && node.max == 1)
                        return ExpandZeroOrOne(sub, nextStateId);
                    if (node.min == 1 && node.max == kRegexUnbounded)
                        return ExpandOneOrMore(sub, nextStateId);
                    if (node.min == 0 && node.max == kRegexUnbounded)
                        return ExpandZeroOrMore(sub, nextStateId);
                    if (node.max != kRegexUnbounded && node.min == node.max)
                        return ExpandCount(sub, node.min, nextStateId);
                    if (node.max == kRegexUnbounded) {
                        // Reference behaviour, quirk included: the star chain
                        // is built first and the counted chain then chains to
                        // the original successor, so `x{n,}` lexes as `x{n}`.
                        if (ExpandZeroOrMore(sub, nextStateId))
                            return ExpandCount(sub, node.min, nextStateId);
                        return false;
                    }
                    bool result = ExpandCount(sub, node.min, nextStateId);
                    for (std::uint32_t i = node.min; i < node.max; ++i) {
                        if (result)
                            nextStateId = nfa.LastStateId();
                        if (ExpandZeroOrOne(sub, nextStateId))
                            result = true;
                    }
                    return result;
                }
                case RegexNode::Kind::Concat: {
                    bool result = false;
                    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                        if (ExpandRegex(*it, nextStateId)) {
                            result      = true;
                            nextStateId = nfa.LastStateId();
                        }
                    }
                    return result;
                }
                case RegexNode::Kind::Alternation: {
                    std::vector<std::uint32_t> alternatives;
                    for (const RegexNode& branch : node.children)
                        alternatives.push_back(ExpandRegex(branch, nextStateId) ? nfa.LastStateId() : nextStateId);
                    FinishAlternatives(alternatives);
                    return true;
                }
            }
            return false;
        }

        bool ExpandOneOrMore(const RegexNode& node, std::uint32_t nextStateId) {
            nfa.states.push_back(NfaState::Accept(0, 0)); // placeholder for the split
            const std::uint32_t splitStateId = nfa.LastStateId();
            if (ExpandRegex(node, splitStateId)) {
                nfa.states[splitStateId] = NfaState::Split(nfa.LastStateId(), nextStateId);
                return true;
            }
            nfa.states.pop_back();
            return false;
        }

        bool ExpandZeroOrOne(const RegexNode& node, std::uint32_t nextStateId) {
            if (ExpandRegex(node, nextStateId)) {
                PushSplit(nextStateId);
                return true;
            }
            return false;
        }

        bool ExpandZeroOrMore(const RegexNode& node, std::uint32_t nextStateId) {
            if (ExpandOneOrMore(node, nextStateId)) {
                PushSplit(nextStateId);
                return true;
            }
            return false;
        }

        bool ExpandCount(const RegexNode& node, std::uint32_t count, std::uint32_t nextStateId) {
            bool result = false;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (ExpandRegex(node, nextStateId)) {
                    result      = true;
                    nextStateId = nfa.LastStateId();
                }
            }
            return result;
        }

        void PushAdvance(CharacterSet chars, std::uint32_t stateId) {
            nfa.states.push_back(NfaState::Advance(std::move(chars), stateId, isSep, precedenceStack_.back()));
        }

        void PushSplit(std::uint32_t stateId) {
            const std::uint32_t last = nfa.LastStateId();
            nfa.states.push_back(NfaState::Split(stateId, last));
        }
    };

    LexicalGrammar ExpandTokens(ExtractedLexicalGrammar grammar) {
        NfaBuilder builder;
        Rule       separatorRule = Rule::Blank();
        if (!grammar.separators.empty()) {
            grammar.separators.push_back(Rule::Blank());
            separatorRule = Rule::Repeat(Rule::Choice(std::move(grammar.separators)));
        }

        LexicalGrammar out;
        for (std::size_t i = 0; i < grammar.variables.size(); ++i) {
            Variable& variable = grammar.variables[i];
            if (variable.rule.IsEmpty())
                throw CompileError("The rule `" + variable.name + "` matches the empty string.\nTree-sitter does not support syntactic rules that match the empty string\nunless they are used only as the grammar's start rule.\n        ");
            const bool isImmediateToken = variable.rule.kind == Rule::Kind::Metadata && variable.rule.params.isMainToken;

            builder.isSep = false;
            builder.nfa.states.push_back(NfaState::Accept(i, CompletionPrecedence(variable.rule)));
            try {
                builder.ExpandRule(variable.rule, builder.nfa.LastStateId());
            }
            catch (const CompileError& error) {
                throw CompileError("Error processing rule " + variable.name + ": " + error.what());
            }

            if (!isImmediateToken) {
                builder.isSep = true;
                builder.ExpandRule(separatorRule, builder.nfa.LastStateId());
            }

            out.variables.push_back({.name = std::move(variable.name), .kind = variable.kind, .implicitPrecedence = ImplicitPrecedence(variable.rule), .startState = builder.nfa.LastStateId()});
        }
        out.nfa = std::move(builder.nfa);
        return out;
    }

    // --- extract_default_aliases --------------------------------------------

    struct SymbolStatus {
        std::vector<std::pair<Alias, std::size_t>> aliases;
        bool                                       appearsUnaliased = false;
    };

    AliasMap ExtractDefaultAliases(SyntaxGrammar& syntax, const LexicalGrammar& lexical) {
        std::vector<SymbolStatus> terminalStatus(lexical.variables.size());
        std::vector<SymbolStatus> nonTerminalStatus(syntax.variables.size());
        std::vector<SymbolStatus> externalStatus(syntax.externalTokens.size());
        const auto                statusFor = [&](Symbol symbol) -> SymbolStatus& {
            switch (symbol.kind) {
                case SymbolType::External:
                    return externalStatus[symbol.index];
                case SymbolType::NonTerminal:
                    return nonTerminalStatus[symbol.index];
                case SymbolType::Terminal:
                    return terminalStatus[symbol.index];
                default:
                    throw CompileError("Unexpected end token");
            }
        };

        for (const SyntaxVariable& variable : syntax.variables) {
            for (const Production& production : variable.productions) {
                for (const ProductionStep& step : production.steps) {
                    SymbolStatus& status = statusFor(step.symbol);
                    if (syntax.IsInlined(step.symbol))
                        continue;
                    if (step.alias) {
                        auto it = std::find_if(status.aliases.begin(), status.aliases.end(), [&](const auto& entry) { return entry.first == *step.alias; });
                        if (it != status.aliases.end())
                            it->second++;
                        else
                            status.aliases.emplace_back(*step.alias, 1);
                    }
                    else {
                        status.appearsUnaliased = true;
                    }
                }
            }
        }
        for (const Symbol symbol : syntax.extraSymbols)
            statusFor(symbol).appearsUnaliased = true;

        AliasMap   result;
        const auto decide = [&](Symbol symbol, SymbolStatus& status) {
            if (status.appearsUnaliased) {
                status.aliases.clear();
                return;
            }
            if (status.aliases.empty())
                return;
            // The most-used alias, earliest on a tie.
            std::size_t best = 0;
            for (std::size_t i = 1; i < status.aliases.size(); ++i)
                if (status.aliases[i].second > status.aliases[best].second)
                    best = i;
            const std::pair<Alias, std::size_t> entry = status.aliases[best];
            status.aliases.clear();
            status.aliases.push_back(entry);
            result.emplace(symbol, entry.first);
        };
        for (std::size_t i = 0; i < terminalStatus.size(); ++i)
            decide(Symbol::Terminal(static_cast<std::uint32_t>(i)), terminalStatus[i]);
        for (std::size_t i = 0; i < nonTerminalStatus.size(); ++i)
            decide(Symbol::NonTerminal(static_cast<std::uint32_t>(i)), nonTerminalStatus[i]);
        for (std::size_t i = 0; i < externalStatus.size(); ++i)
            decide(Symbol::External(static_cast<std::uint32_t>(i)), externalStatus[i]);

        std::vector<std::pair<std::size_t, std::size_t>> positionsToClear;
        for (SyntaxVariable& variable : syntax.variables) {
            positionsToClear.clear();
            for (std::size_t i = 0; i < variable.productions.size(); ++i) {
                const Production& production = variable.productions[i];
                for (std::size_t j = 0; j < production.steps.size(); ++j) {
                    const ProductionStep& step   = production.steps[j];
                    const SymbolStatus&   status = statusFor(step.symbol);
                    if (step.alias && !status.aliases.empty() && *step.alias == status.aliases.front().first) {
                        bool othersMustUseThisAlias = false;
                        for (std::size_t otherI = 0; otherI < variable.productions.size(); ++otherI) {
                            const Production& other = variable.productions[otherI];
                            if (otherI != i && other.steps.size() > j && other.steps[j].alias == step.alias) {
                                const auto found = result.find(other.steps[j].symbol);
                                if (found == result.end() || found->second != *step.alias) {
                                    othersMustUseThisAlias = true;
                                    break;
                                }
                            }
                        }
                        if (!othersMustUseThisAlias)
                            positionsToClear.emplace_back(i, j);
                    }
                }
            }
            for (const auto& [i, j] : positionsToClear)
                variable.productions[i].steps[j].alias = std::nullopt;
        }
        return result;
    }

    // --- process_inlines ----------------------------------------------------

    struct ProductionStepId {
        std::optional<std::size_t> variableIndex; // nullopt: an inlined production in the builder's list
        std::size_t                productionIndex = 0;
        std::size_t                stepIndex       = 0;

        auto operator<=>(const ProductionStepId&) const = default;
    };

    class InlinedProductionMapBuilder {
      public:
        InlinedProductionMap Build(const SyntaxGrammar& grammar) {
            std::vector<ProductionStepId> toProcess;
            for (std::size_t variableIndex = 0; variableIndex < grammar.variables.size(); ++variableIndex) {
                for (std::size_t productionIndex = 0; productionIndex < grammar.variables[variableIndex].productions.size(); ++productionIndex) {
                    toProcess.push_back({.variableIndex = variableIndex, .productionIndex = productionIndex, .stepIndex = 0});
                    while (!toProcess.empty()) {
                        std::size_t i = 0;
                        while (i < toProcess.size()) {
                            const ProductionStepId id = toProcess[i];
                            if (const ProductionStep* step = StepForId(id, grammar)) {
                                if (grammar.IsInlined(step->symbol)) {
                                    const std::vector<std::size_t> inlined = InlineProductionAtStep(id, grammar);
                                    std::vector<ProductionStepId>  replacement;
                                    for (const std::size_t productionIndex2 : inlined)
                                        replacement.push_back({.variableIndex = std::nullopt, .productionIndex = productionIndex2, .stepIndex = id.stepIndex});
                                    toProcess.erase(toProcess.begin() + static_cast<std::ptrdiff_t>(i));
                                    toProcess.insert(toProcess.begin() + static_cast<std::ptrdiff_t>(i), replacement.begin(), replacement.end());
                                }
                                else {
                                    toProcess[i].stepIndex++;
                                    i++;
                                }
                            }
                            else {
                                toProcess.erase(toProcess.begin() + static_cast<std::ptrdiff_t>(i));
                            }
                        }
                    }
                }
            }

            InlinedProductionMap map;
            map.productions = std::move(productions_);
            for (const auto& [id, indices] : indicesByStepId_) {
                const Production* production = id.variableIndex ? &grammar.variables[*id.variableIndex].productions[id.productionIndex] : &map.productions[id.productionIndex];
                map.productionMap.emplace(std::make_pair(production, static_cast<std::uint32_t>(id.stepIndex)), indices);
            }
            return map;
        }

      private:
        std::map<ProductionStepId, std::vector<std::size_t>> indicesByStepId_;
        std::vector<Production>                              productions_;

        const Production& ProductionForId(const ProductionStepId& id, const SyntaxGrammar& grammar) const {
            return id.variableIndex ? grammar.variables[*id.variableIndex].productions[id.productionIndex] : productions_[id.productionIndex];
        }

        const ProductionStep* StepForId(const ProductionStepId& id, const SyntaxGrammar& grammar) const {
            const Production& production = ProductionForId(id, grammar);
            return id.stepIndex < production.steps.size() ? &production.steps[id.stepIndex] : nullptr;
        }

        std::vector<std::size_t> InlineProductionAtStep(const ProductionStepId& id, const SyntaxGrammar& grammar) {
            if (const auto cached = indicesByStepId_.find(id); cached != indicesByStepId_.end())
                return cached->second;

            const std::size_t       stepIndex = id.stepIndex;
            std::vector<Production> toAdd     = {ProductionForId(id, grammar)};
            std::size_t             i         = 0;
            while (i < toAdd.size()) {
                if (stepIndex < toAdd[i].steps.size()) {
                    const Symbol symbol = toAdd[i].steps[stepIndex].symbol;
                    if (grammar.IsInlined(symbol)) {
                        const Production        production = std::move(toAdd[i]);
                        std::vector<Production> replacement;
                        for (const Production& p : grammar.variables[symbol.index].productions) {
                            Production           copy        = production;
                            const ProductionStep removedStep = copy.steps[stepIndex];
                            copy.steps.erase(copy.steps.begin() + static_cast<std::ptrdiff_t>(stepIndex));
                            copy.steps.insert(copy.steps.begin() + static_cast<std::ptrdiff_t>(stepIndex), p.steps.begin(), p.steps.end());
                            for (std::size_t k = stepIndex; k < stepIndex + p.steps.size(); ++k) {
                                if (removedStep.alias)
                                    copy.steps[k].alias = removedStep.alias;
                                if (removedStep.fieldName)
                                    copy.steps[k].fieldName = removedStep.fieldName;
                            }
                            if (!p.steps.empty()) {
                                ProductionStep& lastInserted = copy.steps[stepIndex + p.steps.size() - 1];
                                if (lastInserted.precedence.IsNone())
                                    lastInserted.precedence = removedStep.precedence;
                                if (!lastInserted.associativity)
                                    lastInserted.associativity = removedStep.associativity;
                            }
                            if (std::abs(p.dynamicPrecedence) > std::abs(copy.dynamicPrecedence))
                                copy.dynamicPrecedence = p.dynamicPrecedence;
                            replacement.push_back(std::move(copy));
                        }
                        toAdd.erase(toAdd.begin() + static_cast<std::ptrdiff_t>(i));
                        toAdd.insert(toAdd.begin() + static_cast<std::ptrdiff_t>(i), replacement.begin(), replacement.end());
                        continue;
                    }
                }
                i++;
            }

            std::vector<std::size_t> result;
            for (Production& production : toAdd) {
                const auto found = std::find(productions_.begin(), productions_.end(), production);
                if (found != productions_.end()) {
                    result.push_back(static_cast<std::size_t>(found - productions_.begin()));
                }
                else {
                    productions_.push_back(std::move(production));
                    result.push_back(productions_.size() - 1);
                }
            }
            return indicesByStepId_.emplace(id, result).first->second;
        }
    };

    InlinedProductionMap ProcessInlines(const SyntaxGrammar& grammar, const LexicalGrammar& lexical) {
        for (const Symbol symbol : grammar.variablesToInline) {
            switch (symbol.kind) {
                case SymbolType::External:
                    throw CompileError("External token `" + grammar.externalTokens[symbol.index].name + "` cannot be inlined");
                case SymbolType::Terminal:
                    throw CompileError("Token `" + lexical.variables[symbol.index].name + "` cannot be inlined");
                case SymbolType::NonTerminal:
                    if (symbol.index == 0)
                        throw CompileError("Rule `" + grammar.variables[0].name + "` cannot be inlined because it is the first rule");
                    break;
                default:
                    break;
            }
        }
        return InlinedProductionMapBuilder().Build(grammar);
    }

} // namespace

InputGrammar ParseInputGrammar(const GrammarFile& file) {
    InputGrammar grammar;
    grammar.name = file.name;

    for (const FileRule& extra : file.extras) {
        Rule rule = RuleFromFile(extra, false);
        if (rule.kind == Rule::Kind::String && rule.text.empty())
            throw CompileError("Rules in the `extras` array must not contain empty strings");
        grammar.extraSymbols.push_back(std::move(rule));
    }
    for (const FileRule& external : file.externals)
        grammar.externalTokens.push_back(RuleFromFile(external, false));

    for (const std::vector<FileRule>& list : file.precedences) {
        std::vector<PrecedenceEntry> ordering;
        for (const FileRule& entry : list) {
            if (entry.kind == FileRule::Kind::String)
                ordering.push_back({.kind = PrecedenceEntry::Kind::Name, .text = entry.text});
            else if (entry.kind == FileRule::Kind::Symbol)
                ordering.push_back({.kind = PrecedenceEntry::Kind::Symbol, .text = entry.text});
            else
                throw CompileError("Invalid rule in precedences array. Only strings and symbols are allowed");
        }
        grammar.precedenceOrderings.push_back(std::move(ordering));
    }

    std::vector<std::pair<std::string, Rule>> rules;
    for (const auto& [name, rule] : file.rules)
        rules.emplace_back(name, RuleFromFile(rule, false));
    if (rules.empty())
        throw CompileError("a grammar needs at least one rule");

    std::vector<std::vector<std::string>> conflicts   = file.conflicts;
    std::vector<std::string>              supertypes  = file.supertypes;
    std::vector<std::string>              inlineRules = file.inlineRules;
    std::set<std::string>                 inProgress;
    for (const auto& [name, rule] : rules) {
        if (file.word != name && !VariableIsUsed(rules, grammar.extraSymbols, grammar.externalTokens, name, inProgress)) {
            const std::string& n = name;
            conflicts.erase(std::remove_if(conflicts.begin(), conflicts.end(), [&](const auto& c) { return std::find(c.begin(), c.end(), n) != c.end(); }), conflicts.end());
            supertypes.erase(std::remove(supertypes.begin(), supertypes.end(), n), supertypes.end());
            inlineRules.erase(std::remove(inlineRules.begin(), inlineRules.end(), n), inlineRules.end());
            grammar.extraSymbols.erase(std::remove_if(grammar.extraSymbols.begin(), grammar.extraSymbols.end(), [&](const Rule& r) { return RuleIsReferenced(r, n, true); }), grammar.extraSymbols.end());
            grammar.externalTokens.erase(std::remove_if(grammar.externalTokens.begin(), grammar.externalTokens.end(), [&](const Rule& r) { return RuleIsReferenced(r, n, true); }), grammar.externalTokens.end());
            grammar.precedenceOrderings.erase(std::remove_if(grammar.precedenceOrderings.begin(), grammar.precedenceOrderings.end(),
                                                             [&](const auto& list) {
                                                                 return std::any_of(list.begin(), list.end(), [&](const PrecedenceEntry& e) { return e.kind == PrecedenceEntry::Kind::Symbol && e.text == n; });
                                                             }),
                                              grammar.precedenceOrderings.end());
            continue;
        }
        grammar.variables.push_back({.name = name, .kind = VariableType::Named, .rule = rule});
    }

    for (const auto& [context, words] : file.reserved) {
        ReservedWordContext<Rule> set{.name = context};
        for (const FileRule& word : words)
            set.reservedWords.push_back(RuleFromFile(word, false));
        grammar.reservedWords.push_back(std::move(set));
    }

    grammar.expectedConflicts = std::move(conflicts);
    grammar.supertypeSymbols  = std::move(supertypes);
    grammar.variablesToInline = std::move(inlineRules);
    if (!file.word.empty())
        grammar.wordToken = file.word;
    return grammar;
}

PreparedGrammar PrepareGrammar(const InputGrammar& input) {
    ValidatePrecedences(input);
    ValidateIndirectRecursion(input);

    InternedGrammar interned                 = InternSymbols(input);
    auto [extractedSyntax, extractedLexical] = ExtractTokens(std::move(interned));
    extractedSyntax                          = ExpandRepeats(std::move(extractedSyntax));
    PreparedGrammar prepared;
    prepared.syntax         = FlattenGrammar(std::move(extractedSyntax));
    prepared.lexical        = ExpandTokens(std::move(extractedLexical));
    prepared.defaultAliases = ExtractDefaultAliases(prepared.syntax, prepared.lexical);
    prepared.inlines        = ProcessInlines(prepared.syntax, prepared.lexical);
    return prepared;
}

} // namespace ned::editor::grammar::compile
