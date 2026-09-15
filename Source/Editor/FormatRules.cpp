#include "FormatRules.h"

#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ned::editor {

namespace {

    bool operator==(const SpaceRuleValue& a, const SpaceRuleValue& b) {
        return a.before == b.before && a.after == b.after && a.within == b.within;
    }

    bool operator==(const BreakRuleValue& a, const BreakRuleValue& b) {
        return a.before == b.before && a.after == b.after && a.placement == b.placement &&
               a.collapseEmpty == b.collapseEmpty && a.collapseSimple == b.collapseSimple;
    }

    bool operator==(const BlankRuleValue& a, const BlankRuleValue& b) {
        return a.minBefore == b.minBefore && a.maxBefore == b.maxBefore;
    }

    std::mutex& RulesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, SpaceRuleValue>& SpaceRules() {
        static std::unordered_map<std::string, SpaceRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, BreakRuleValue>& BreakRules() {
        static std::unordered_map<std::string, BreakRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, BlankRuleValue>& BlankRules() {
        static std::unordered_map<std::string, BlankRuleValue> rules;
        return rules;
    }

    std::size_t& Generation() {
        static std::size_t generation = 0;
        return generation;
    }

    // SyntaxTheme.cpp's ValidateCaptureName, duplicated rather than shared --
    // a small, self-contained rule, same tolerance this codebase already
    // extends to other small per-file helpers (see IndentStyleTest.cpp/
    // WrapOverridesTest.cpp's own per-file guard types).
    void ValidateCaptureName(std::string_view name) {
        const bool malformed = name.empty() || name.front() == '@' || name.front() == '.' || name.back() == '.' ||
                               name.find("..") != std::string_view::npos ||
                               name.find_first_of(" \t\n") != std::string_view::npos;
        if (malformed) {
            throw std::runtime_error("ned: invalid capture name \"" + std::string(name) +
                                     "\" -- expected a dotted tree-sitter capture name without the leading '@', e.g. "
                                     "\"control.parens\"");
        }
    }

    template <typename T, typename Field>
    void SetSpaceField(const std::string& name, std::optional<T> value, Field SpaceRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = SpaceRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetBreakField(const std::string& name, std::optional<T> value, Field BreakRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = BreakRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetBlankField(const std::string& name, std::optional<T> value, Field BlankRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = BlankRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename Value>
    Value RuleFor(const std::unordered_map<std::string, Value>& rules, std::string_view name) {
        const auto it = rules.find(std::string(name));
        return it != rules.end() ? it->second : Value{};
    }

    // Shared by SpaceRuleFor/BreakRuleFor's two-argument overloads --
    // SyntaxClassOverrideForCapture(name, language)'s exact shape: try
    // "<language>/<name>" first, fall back to the unscoped entry.
    template <typename Value, typename Lookup>
    Value ScopedRuleFor(std::string_view name, std::string_view language, Lookup unscoped) {
        if (!language.empty()) {
            std::string scoped;
            scoped.reserve(language.size() + 1 + name.size());
            scoped.append(language);
            scoped.push_back('/');
            scoped.append(name);
            if (const Value scopedValue = unscoped(std::string_view(scoped)); scopedValue != Value{}) {
                return scopedValue;
            }
        }
        return unscoped(name);
    }

} // namespace

void SetSpaceBefore(const std::string& name, std::optional<bool> value) {
    SetSpaceField(name, value, &SpaceRuleValue::before);
}

void SetSpaceAfter(const std::string& name, std::optional<bool> value) {
    SetSpaceField(name, value, &SpaceRuleValue::after);
}

void SetSpaceWithin(const std::string& name, std::optional<bool> value) {
    SetSpaceField(name, value, &SpaceRuleValue::within);
}

SpaceRuleValue SpaceRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(SpaceRules(), name);
}

SpaceRuleValue SpaceRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<SpaceRuleValue>(name, language, [](std::string_view n) { return SpaceRuleFor(n); });
}

void SetBreakBefore(const std::string& name, std::optional<bool> value) {
    SetBreakField(name, value, &BreakRuleValue::before);
}

void SetBreakAfter(const std::string& name, std::optional<bool> value) {
    SetBreakField(name, value, &BreakRuleValue::after);
}

void SetBracePlacement(const std::string& name, std::optional<BracePlacement> value) {
    SetBreakField(name, value, &BreakRuleValue::placement);
}

void SetBraceCollapseEmpty(const std::string& name, std::optional<bool> value) {
    SetBreakField(name, value, &BreakRuleValue::collapseEmpty);
}

void SetBraceCollapseSimple(const std::string& name, std::optional<bool> value) {
    SetBreakField(name, value, &BreakRuleValue::collapseSimple);
}

BreakRuleValue BreakRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(BreakRules(), name);
}

BreakRuleValue BreakRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<BreakRuleValue>(name, language, [](std::string_view n) { return BreakRuleFor(n); });
}

void SetBlankMinBefore(const std::string& name, std::optional<int> value) {
    SetBlankField(name, value, &BlankRuleValue::minBefore);
}

void SetBlankMaxBefore(const std::string& name, std::optional<int> value) {
    SetBlankField(name, value, &BlankRuleValue::maxBefore);
}

BlankRuleValue BlankRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(BlankRules(), name);
}

BlankRuleValue BlankRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<BlankRuleValue>(name, language, [](std::string_view n) { return BlankRuleFor(n); });
}

std::size_t FormatRuleGeneration() {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return Generation();
}

BracePlacement BracePlacementByName(const std::string& name) {
    if (name == "same-line") {
        return BracePlacement::SameLine;
    }
    if (name == "next-line") {
        return BracePlacement::NextLine;
    }
    if (name == "next-line-indented") {
        return BracePlacement::NextLineIndented;
    }
    throw std::runtime_error("ned: invalid brace placement \"" + name +
                             "\" -- expected \"same-line\", \"next-line\", or \"next-line-indented\"");
}

std::string BracePlacementName(BracePlacement placement) {
    switch (placement) {
        case BracePlacement::SameLine:
            return "same-line";
        case BracePlacement::NextLine:
            return "next-line";
        case BracePlacement::NextLineIndented:
            return "next-line-indented";
    }
    throw std::runtime_error("ned: internal error: unhandled BracePlacement");
}

} // namespace ned::editor
