#include "NodeTypes.h"

#include <algorithm>

namespace ned::editor::grammar::compile {

void ChildQuantity::Append(ChildQuantity other) {
    if (other.exists) {
        if (exists || other.multiple)
            multiple = true;
        if (other.required)
            required = true;
        exists = true;
    }
}

bool ChildQuantity::Union(ChildQuantity other) {
    bool result = false;
    if (!exists && other.exists) {
        result = true;
        exists = true;
    }
    if (required && !other.required) {
        result   = true;
        required = false;
    }
    if (!multiple && other.multiple) {
        result   = true;
        multiple = true;
    }
    return result;
}

namespace {

    bool ExtendSorted(std::vector<ChildType>& into, const ChildType& type) {
        const auto pos = std::lower_bound(into.begin(), into.end(), type);
        if (pos != into.end() && *pos == type)
            return false;
        into.insert(pos, type);
        return true;
    }

    bool ExtendSorted(std::vector<ChildType>& into, const std::vector<ChildType>& types) {
        bool changed = false;
        for (const ChildType& type : types)
            changed |= ExtendSorted(into, type);
        return changed;
    }

    VariableType VariableTypeForChildType(const ChildType& type, const SyntaxGrammar& syntax, const LexicalGrammar& lexical) {
        if (type.kind == ChildType::Kind::Aliased)
            return type.alias.named ? VariableType::Named : VariableType::Anonymous;
        switch (type.symbol.kind) {
            case SymbolType::NonTerminal:
                return syntax.variables[type.symbol.index].kind;
            case SymbolType::Terminal:
                return lexical.variables[type.symbol.index].kind;
            case SymbolType::External:
                return syntax.externalTokens[type.symbol.index].kind;
            default:
                return VariableType::Hidden;
        }
    }

} // namespace

std::vector<VariableInfo> GetVariableInfo(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const AliasMap& defaultAliases) {
    const auto isVisible   = [&](const ChildType& t) { return VariableTypeForChildType(t, syntax, lexical) >= VariableType::Anonymous; };
    const auto isNamed     = [&](const ChildType& t) { return VariableTypeForChildType(t, syntax, lexical) == VariableType::Named; };
    const auto isSupertype = [&](Symbol s) { return std::find(syntax.supertypeSymbols.begin(), syntax.supertypeSymbols.end(), s) != syntax.supertypeSymbols.end(); };

    std::vector<VariableInfo> result(syntax.variables.size());
    bool                      didChange      = true;
    bool                      allInitialized = false;
    while (didChange) {
        didChange = false;
        for (std::size_t i = 0; i < syntax.variables.size(); ++i) {
            const SyntaxVariable& variable = syntax.variables[i];
            VariableInfo          info     = result[i];

            for (const Production& production : variable.productions) {
                std::map<std::string, ChildQuantity> productionFieldQuantities;
                ChildQuantity                        productionChildrenQuantity              = ChildQuantity::Zero();
                ChildQuantity                        productionChildrenWithoutFieldsQuantity = ChildQuantity::Zero();
                bool                                 hasUninitializedInvisibleChildren       = false;

                if (production.steps.size() > 1)
                    info.hasMultiStepProduction = true;

                for (const ProductionStep& step : production.steps) {
                    const Symbol childSymbol = step.symbol;
                    ChildType    childType;
                    if (step.alias) {
                        childType = {.kind = ChildType::Kind::Aliased, .alias = *step.alias};
                    }
                    else if (const auto alias = defaultAliases.find(childSymbol); alias != defaultAliases.end()) {
                        childType = {.kind = ChildType::Kind::Aliased, .alias = alias->second};
                    }
                    else {
                        childType = {.kind = ChildType::Kind::Normal, .symbol = childSymbol};
                    }

                    const bool childIsHidden = !isVisible(childType) && !isSupertype(childSymbol);

                    didChange |= ExtendSorted(info.children.types, childType);
                    if (!childIsHidden)
                        productionChildrenQuantity.Append(ChildQuantity::One());

                    if (step.fieldName) {
                        FieldInfo& fieldInfo = info.fields[*step.fieldName];
                        didChange |= ExtendSorted(fieldInfo.types, childType);
                        ChildQuantity& fieldQuantity = productionFieldQuantities.try_emplace(*step.fieldName, ChildQuantity::Zero()).first->second;
                        if (childIsHidden && childSymbol.IsNonTerminal()) {
                            const VariableInfo& childInfo = result[childSymbol.index];
                            didChange |= ExtendSorted(fieldInfo.types, childInfo.children.types);
                            fieldQuantity.Append(childInfo.children.quantity);
                        }
                        else {
                            fieldQuantity.Append(ChildQuantity::One());
                        }
                    }
                    else if (isNamed(childType)) {
                        productionChildrenWithoutFieldsQuantity.Append(ChildQuantity::One());
                        didChange |= ExtendSorted(info.childrenWithoutFields.types, childType);
                    }

                    if (childIsHidden && childSymbol.IsNonTerminal()) {
                        const VariableInfo& childInfo = result[childSymbol.index];
                        if (childInfo.hasMultiStepProduction)
                            info.hasMultiStepProduction = true;
                        for (const auto& [fieldName, childFieldInfo] : childInfo.fields) {
                            productionFieldQuantities.try_emplace(fieldName, ChildQuantity::Zero()).first->second.Append(childFieldInfo.quantity);
                            didChange |= ExtendSorted(info.fields[fieldName].types, childFieldInfo.types);
                        }
                        productionChildrenQuantity.Append(childInfo.children.quantity);
                        didChange |= ExtendSorted(info.children.types, childInfo.children.types);
                        if (!step.fieldName) {
                            const FieldInfo& grandchildren = childInfo.childrenWithoutFields;
                            if (!grandchildren.types.empty()) {
                                productionChildrenWithoutFieldsQuantity.Append(grandchildren.quantity);
                                didChange |= ExtendSorted(info.childrenWithoutFields.types, grandchildren.types);
                            }
                        }
                    }

                    if (childSymbol.index >= i && !allInitialized)
                        hasUninitializedInvisibleChildren = true;
                }

                if (!hasUninitializedInvisibleChildren) {
                    didChange |= info.children.quantity.Union(productionChildrenQuantity);
                    didChange |= info.childrenWithoutFields.quantity.Union(productionChildrenWithoutFieldsQuantity);
                    for (auto& [fieldName, fieldInfo] : info.fields) {
                        const auto found = productionFieldQuantities.find(fieldName);
                        didChange |= fieldInfo.quantity.Union(found == productionFieldQuantities.end() ? ChildQuantity::Zero() : found->second);
                    }
                }
            }
            result[i] = std::move(info);
        }
        allInitialized = true;
    }

    for (const Symbol supertype : syntax.supertypeSymbols)
        if (result[supertype.index].hasMultiStepProduction)
            throw CompileError("Grammar error: Supertype symbols must always have a single visible child, but `" + syntax.variables[supertype.index].name + "` can have multiple");

    for (const Symbol supertype : syntax.supertypeSymbols) {
        std::vector<ChildType>& types = result[supertype.index].children.types;
        types.erase(std::remove_if(types.begin(), types.end(), [&](const ChildType& t) { return !isVisible(t); }), types.end());
    }
    for (VariableInfo& info : result) {
        for (auto& [_, fieldInfo] : info.fields)
            fieldInfo.types.erase(std::remove_if(fieldInfo.types.begin(), fieldInfo.types.end(), [&](const ChildType& t) { return !isVisible(t); }), fieldInfo.types.end());
        for (auto it = info.fields.begin(); it != info.fields.end();)
            it = it->second.types.empty() ? info.fields.erase(it) : std::next(it);
        std::vector<ChildType>& types = info.childrenWithoutFields.types;
        types.erase(std::remove_if(types.begin(), types.end(), [&](const ChildType& t) { return !isVisible(t); }), types.end());
    }
    return result;
}

std::map<Symbol, std::vector<ChildType>> GetSupertypeSymbolMap(const SyntaxGrammar& syntax, const std::vector<VariableInfo>& info) {
    std::map<Symbol, std::vector<ChildType>> map;
    for (std::size_t i = 0; i < info.size(); ++i) {
        const Symbol symbol = Symbol::NonTerminal(static_cast<std::uint32_t>(i));
        if (std::find(syntax.supertypeSymbols.begin(), syntax.supertypeSymbols.end(), symbol) != syntax.supertypeSymbols.end())
            map.emplace(symbol, info[i].children.types);
    }
    return map;
}

} // namespace ned::editor::grammar::compile
