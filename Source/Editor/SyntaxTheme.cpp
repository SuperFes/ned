#include "SyntaxTheme.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace ned::editor {

namespace {

    std::mutex& OverridesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::map<SyntaxClass, SyntaxStyleOverride>& Overrides() {
        static std::map<SyntaxClass, SyntaxStyleOverride> overrides;
        return overrides;
    }

    std::size_t& Generation() {
        static std::size_t generation = 0;
        return generation;
    }

    bool IsValidHexColor(const std::string& hex) {
        if (hex.size() != 7 || hex[0] != '#') {
            return false;
        }
        return std::all_of(hex.begin() + 1, hex.end(), [](unsigned char c) { return std::isxdigit(c); });
    }

    // The full real SyntaxClass enum (Mode.h), kebab-case Janet-facing
    // names -- covers "each themeable entry," generic and Org-specific
    // alike, with one shared mechanism rather than one-off code per class.
    const std::vector<std::pair<SyntaxClass, std::string>>& NameTable() {
        static const std::vector<std::pair<SyntaxClass, std::string>> table = {
            {SyntaxClass::Default, "default"},
            {SyntaxClass::Comment, "comment"},
            {SyntaxClass::DocComment, "doc-comment"},
            {SyntaxClass::String, "string"},
            {SyntaxClass::StringEscape, "string-escape"},
            {SyntaxClass::Number, "number"},
            {SyntaxClass::Keyword, "keyword"},
            {SyntaxClass::ControlKeyword, "control-keyword"},
            {SyntaxClass::Function, "function"},
            {SyntaxClass::FunctionBuiltin, "function-builtin"},
            {SyntaxClass::Type, "type"},
            {SyntaxClass::TypeBuiltin, "type-builtin"},
            {SyntaxClass::Constant, "constant"},
            {SyntaxClass::ConstantBuiltin, "constant-builtin"},
            {SyntaxClass::Variable, "variable"},
            {SyntaxClass::VariableBuiltin, "variable-builtin"},
            {SyntaxClass::Parameter, "parameter"},
            {SyntaxClass::Property, "property"},
            {SyntaxClass::Operator, "operator"},
            {SyntaxClass::Punctuation, "punctuation"},
            {SyntaxClass::Tag, "tag"},
            {SyntaxClass::Attribute, "attribute"},
            {SyntaxClass::Namespace, "namespace"},
            {SyntaxClass::KeywordModifier, "keyword-modifier"},
            {SyntaxClass::Method, "method"},
            {SyntaxClass::Constructor, "constructor"},
            {SyntaxClass::Label, "label"},
            {SyntaxClass::ReturnType, "return-type"},
            {SyntaxClass::IncludePath, "include-path"},
            {SyntaxClass::HeadlineLevel1, "headline-level-1"},
            {SyntaxClass::HeadlineLevel2, "headline-level-2"},
            {SyntaxClass::HeadlineLevel3, "headline-level-3"},
            {SyntaxClass::TodoKeyword, "todo-keyword"},
            {SyntaxClass::DoneKeyword, "done-keyword"},
            {SyntaxClass::Checkbox, "checkbox"},
            {SyntaxClass::Strong, "strong"},
            {SyntaxClass::Emphasis, "emphasis"},
            {SyntaxClass::Underline, "underline"},
            {SyntaxClass::Strikethrough, "strikethrough"},
        };
        return table;
    }

    const std::unordered_map<std::string, SyntaxClass>& NameToClass() {
        static const std::unordered_map<std::string, SyntaxClass> map = [] {
            std::unordered_map<std::string, SyntaxClass> m;
            for (const auto& [cls, name] : NameTable()) {
                m.emplace(name, cls);
            }
            return m;
        }();
        return map;
    }

    template <typename T, typename Field>
    void SetField(SyntaxClass cls, std::optional<T> value, Field SyntaxStyleOverride::*field) {
        const std::lock_guard<std::mutex> lock(OverridesMutex());
        auto&                             entry = Overrides()[cls];
        entry.*field                             = std::move(value);
        ++Generation();
    }

} // namespace

void SetSyntaxForeground(SyntaxClass cls, std::optional<std::string> hex) {
    if (hex && !IsValidHexColor(*hex)) {
        throw std::runtime_error("ned: invalid color \"" + *hex + "\" -- expected \"#rrggbb\"");
    }
    SetField(cls, std::move(hex), &SyntaxStyleOverride::foreground);
}

void SetSyntaxBackground(SyntaxClass cls, std::optional<std::string> hex) {
    if (hex && !IsValidHexColor(*hex)) {
        throw std::runtime_error("ned: invalid color \"" + *hex + "\" -- expected \"#rrggbb\"");
    }
    SetField(cls, std::move(hex), &SyntaxStyleOverride::background);
}

void SetSyntaxBold(SyntaxClass cls, std::optional<bool> value) {
    SetField(cls, value, &SyntaxStyleOverride::bold);
}

void SetSyntaxItalic(SyntaxClass cls, std::optional<bool> value) {
    SetField(cls, value, &SyntaxStyleOverride::italic);
}

void SetSyntaxUnderlined(SyntaxClass cls, std::optional<bool> value) {
    SetField(cls, value, &SyntaxStyleOverride::underlined);
}

void SetSyntaxStrikethrough(SyntaxClass cls, std::optional<bool> value) {
    SetField(cls, value, &SyntaxStyleOverride::strikethrough);
}

SyntaxStyleOverride SyntaxOverrideFor(SyntaxClass cls) {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    const auto&                       overrides = Overrides();
    const auto                        it         = overrides.find(cls);
    return it != overrides.end() ? it->second : SyntaxStyleOverride{};
}

std::size_t SyntaxThemeGeneration() {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    return Generation();
}

SyntaxClass SyntaxClassByName(const std::string& name) {
    const auto it = NameToClass().find(name);
    if (it == NameToClass().end()) {
        throw std::runtime_error("ned: unrecognized syntax class \"" + name + "\"");
    }
    return it->second;
}

std::string SyntaxClassName(SyntaxClass cls) {
    for (const auto& [candidate, name] : NameTable()) {
        if (candidate == cls) {
            return name;
        }
    }
    throw std::runtime_error("ned: internal error -- SyntaxClass has no name in NameTable");
}

std::vector<std::string> SyntaxClassNames() {
    std::vector<std::string> names;
    names.reserve(NameTable().size());
    for (const auto& [cls, name] : NameTable()) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ned::editor
