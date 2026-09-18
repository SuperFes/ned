//
// What each grammar variable looks like from outside: its fields, its
// visible children, and whether it can have more than one -- the summary
// tree-sitter's generator derives in `node_types.rs`. The parse-table builder
// needs a hidden variable's fields (a parent inherits them), and the supertype
// map the query matcher reads comes from a supertype's child types.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_NODETYPES_H
#define NED_EDITOR_GRAMMAR_COMPILE_NODETYPES_H

#include <map>
#include <string>
#include <vector>

#include "Editor/Grammar/Compile/Grammar.h"

namespace ned::editor::grammar::compile {

struct ChildType {
    enum class Kind : std::uint8_t { Normal,
                                     Aliased };
    Kind   kind = Kind::Normal;
    Symbol symbol; // Normal
    Alias  alias;  // Aliased

    auto operator<=>(const ChildType&) const = default;
};

struct ChildQuantity {
    bool exists   = true;
    bool required = true;
    bool multiple = false;

    static ChildQuantity Zero() {
        return {.exists = false, .required = false, .multiple = false};
    }
    static ChildQuantity One() {
        return {};
    }
    void Append(ChildQuantity other);
    bool Union(ChildQuantity other);
    bool operator==(const ChildQuantity&) const = default;
};

struct FieldInfo {
    ChildQuantity          quantity;
    std::vector<ChildType> types; // sorted

    bool operator==(const FieldInfo&) const = default;
};

struct VariableInfo {
    std::map<std::string, FieldInfo> fields;
    FieldInfo                        children;
    FieldInfo                        childrenWithoutFields;
    bool                             hasMultiStepProduction = false;

    bool operator==(const VariableInfo&) const = default;
};

// One per syntax variable. Throws CompileError for a supertype that can
// have more than one visible child.
[[nodiscard]] std::vector<VariableInfo> GetVariableInfo(const SyntaxGrammar& syntax, const LexicalGrammar& lexical, const AliasMap& defaultAliases);

// Each supertype symbol's subtypes.
[[nodiscard]] std::map<Symbol, std::vector<ChildType>> GetSupertypeSymbolMap(const SyntaxGrammar& syntax, const std::vector<VariableInfo>& info);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_NODETYPES_H
