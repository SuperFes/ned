#include "SyntaxTheme.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
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

    // Per-capture-name stores (exhaustive-highlighting follow-up), all
    // guarded by the same OverridesMutex above -- one small file, one lock.
    std::unordered_map<std::string, SyntaxStyleOverride>& CaptureOverrides() {
        static std::unordered_map<std::string, SyntaxStyleOverride> overrides;
        return overrides;
    }

    std::unordered_map<std::string, SyntaxClass>& CaptureClassOverrides() {
        static std::unordered_map<std::string, SyntaxClass> overrides;
        return overrides;
    }

    std::size_t& ClassGeneration() {
        static std::size_t generation = 0;
        return generation;
    }

    // Interning tables: name -> id and id -> name (index id - 1; kNoCapture
    // is reserved and never handed out). Append-only for the process
    // lifetime -- see the header comment.
    std::unordered_map<std::string, CaptureId>& CaptureIds() {
        static std::unordered_map<std::string, CaptureId> ids;
        return ids;
    }

    std::vector<std::string>& CaptureNames() {
        static std::vector<std::string> names;
        return names;
    }

    // Malformed vs merely unknown -- see the header comment's trust-boundary
    // note. Rejects shapes that can only be a mistake (a pasted "@name", a
    // stray dot) rather than restricting the alphabet: grammars are free to
    // pick names this file can't predict.
    void ValidateCaptureName(std::string_view name) {
        const bool malformed = name.empty() || name.front() == '@' || name.front() == '.' || name.back() == '.' ||
                               name.find("..") != std::string_view::npos ||
                               name.find_first_of(" \t\n") != std::string_view::npos;
        if (malformed) {
            throw std::runtime_error("ned: invalid capture name \"" + std::string(name) +
                                     "\" -- expected a dotted tree-sitter capture name without the leading '@', e.g. "
                                     "\"function.builtin\"");
        }
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
            // Markdown-highlighting follow-up's two classes, found missing
            // here during the exhaustive-highlighting follow-up --
            // SyntaxClassName(MarkupMarker/Link) used to throw "internal
            // error", and neither was reachable from ned/set-syntax-*.
            {SyntaxClass::MarkupMarker, "markup-marker"},
            {SyntaxClass::Link, "link"},
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
    void SetField(SyntaxClass cls, std::optional<T> value, Field SyntaxStyleOverride::* field) {
        const std::lock_guard<std::mutex> lock(OverridesMutex());
        auto&                             entry = Overrides()[cls];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetCaptureField(const std::string& name, std::optional<T> value, Field SyntaxStyleOverride::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(OverridesMutex());
        auto&                             entry = CaptureOverrides()[name];
        entry.*field                            = std::move(value);
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
    const auto                        it        = overrides.find(cls);
    return it != overrides.end() ? it->second : SyntaxStyleOverride{};
}

std::size_t SyntaxThemeGeneration() {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    return Generation();
}

CaptureId InternCaptureName(std::string_view name) {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    auto&                             ids = CaptureIds();
    // Transparent lookup would avoid this temporary, but the unordered_map's
    // default hasher isn't heterogeneous and interning is a parse-time path
    // (per capture per reparse), not the per-codepoint render path.
    const std::string key(name);
    if (const auto it = ids.find(key); it != ids.end()) {
        return it->second;
    }
    auto& names = CaptureNames();
    // CaptureId is uint16 and kNoCapture reserves 0 -- an id past the type's
    // range would silently alias, so a (practically unreachable: real
    // grammars produce dozens of names, not tens of thousands) overflow
    // degrades to "not individually stylable" rather than corrupting some
    // other name's styling.
    if (names.size() >= 0xFFFF) {
        return kNoCapture;
    }
    names.push_back(key);
    const auto id = static_cast<CaptureId>(names.size()); // index + 1: 0 stays reserved
    ids.emplace(std::move(key), id);
    return id;
}

std::string CaptureNameForId(CaptureId id) {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    const auto&                       names = CaptureNames();
    if (id == kNoCapture || id > names.size()) {
        return {};
    }
    return names[id - 1];
}

void SetCaptureForeground(const std::string& name, std::optional<std::string> hex) {
    if (hex && !IsValidHexColor(*hex)) {
        throw std::runtime_error("ned: invalid color \"" + *hex + "\" -- expected \"#rrggbb\"");
    }
    SetCaptureField(name, std::move(hex), &SyntaxStyleOverride::foreground);
}

void SetCaptureBackground(const std::string& name, std::optional<std::string> hex) {
    if (hex && !IsValidHexColor(*hex)) {
        throw std::runtime_error("ned: invalid color \"" + *hex + "\" -- expected \"#rrggbb\"");
    }
    SetCaptureField(name, std::move(hex), &SyntaxStyleOverride::background);
}

void SetCaptureBold(const std::string& name, std::optional<bool> value) {
    SetCaptureField(name, value, &SyntaxStyleOverride::bold);
}

void SetCaptureItalic(const std::string& name, std::optional<bool> value) {
    SetCaptureField(name, value, &SyntaxStyleOverride::italic);
}

void SetCaptureUnderlined(const std::string& name, std::optional<bool> value) {
    SetCaptureField(name, value, &SyntaxStyleOverride::underlined);
}

void SetCaptureStrikethrough(const std::string& name, std::optional<bool> value) {
    SetCaptureField(name, value, &SyntaxStyleOverride::strikethrough);
}

SyntaxStyleOverride CaptureOverrideFor(const std::string& name) {
    ValidateCaptureName(name);
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    const auto&                       overrides = CaptureOverrides();
    const auto                        it        = overrides.find(name);
    return it != overrides.end() ? it->second : SyntaxStyleOverride{};
}

SyntaxStyleOverride ResolvedCaptureOverride(std::string_view name) {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    const auto&                       overrides = CaptureOverrides();

    SyntaxStyleOverride resolved;
    // Most-specific-first walk (the same dotted-name stripping
    // SyntaxClassForCapture uses at parse time): each field keeps the first
    // -- i.e. most specific -- value it sees.
    while (!name.empty()) {
        if (const auto it = overrides.find(std::string(name)); it != overrides.end()) {
            const SyntaxStyleOverride& level = it->second;
            if (!resolved.foreground) {
                resolved.foreground = level.foreground;
            }
            if (!resolved.background) {
                resolved.background = level.background;
            }
            if (!resolved.bold) {
                resolved.bold = level.bold;
            }
            if (!resolved.italic) {
                resolved.italic = level.italic;
            }
            if (!resolved.underlined) {
                resolved.underlined = level.underlined;
            }
            if (!resolved.strikethrough) {
                resolved.strikethrough = level.strikethrough;
            }
        }
        const std::size_t dot = name.rfind('.');
        if (dot == std::string_view::npos) {
            break;
        }
        name = name.substr(0, dot);
    }
    return resolved;
}

void SetSyntaxClassForCapture(const std::string& name, std::optional<SyntaxClass> cls) {
    ValidateCaptureName(name);
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    if (cls) {
        CaptureClassOverrides()[name] = *cls;
    }
    else {
        CaptureClassOverrides().erase(name);
    }
    ++ClassGeneration();
}

std::optional<SyntaxClass> SyntaxClassOverrideForCapture(std::string_view name) {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    const auto&                       overrides = CaptureClassOverrides();
    const auto                        it        = overrides.find(std::string(name));
    return it != overrides.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<SyntaxClass> SyntaxClassOverrideForCapture(std::string_view name, std::string_view language) {
    if (!language.empty()) {
        std::string scoped;
        scoped.reserve(language.size() + 1 + name.size());
        scoped.append(language);
        scoped.push_back('/');
        scoped.append(name);
        if (const auto scopedOverride = SyntaxClassOverrideForCapture(std::string_view(scoped))) {
            return scopedOverride;
        }
    }
    return SyntaxClassOverrideForCapture(name);
}

std::size_t CaptureClassGeneration() {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    return ClassGeneration();
}

std::vector<std::string> KnownCaptureNames() {
    const std::lock_guard<std::mutex> lock(OverridesMutex());
    std::vector<std::string>          names = CaptureNames();
    for (const auto& [name, override] : CaptureOverrides()) {
        names.push_back(name);
    }
    for (const auto& [name, cls] : CaptureClassOverrides()) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
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
