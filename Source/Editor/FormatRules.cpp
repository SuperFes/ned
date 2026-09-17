#include "FormatRules.h"

#include <algorithm>
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

    bool operator==(const WrapRuleValue& a, const WrapRuleValue& b) {
        return a.policy == b.policy && a.forceTrailingComma == b.forceTrailingComma;
    }

    bool operator==(const CaseRuleValue& a, const CaseRuleValue& b) {
        return a.convention == b.convention;
    }

    bool operator==(const AlignRuleValue& a, const AlignRuleValue& b) {
        return a.enabled == b.enabled;
    }

    bool operator==(const ArrangeRuleValue& a, const ArrangeRuleValue& b) {
        return a.enabled == b.enabled && a.caseInsensitive == b.caseInsensitive;
    }

    bool operator==(const RewriteRuleValue& a, const RewriteRuleValue& b) {
        return a.quoteStyle == b.quoteStyle;
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

    std::unordered_map<std::string, WrapRuleValue>& WrapRules() {
        static std::unordered_map<std::string, WrapRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, CaseRuleValue>& CaseRules() {
        static std::unordered_map<std::string, CaseRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, AlignRuleValue>& AlignRules() {
        static std::unordered_map<std::string, AlignRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, ArrangeRuleValue>& ArrangeRules() {
        static std::unordered_map<std::string, ArrangeRuleValue> rules;
        return rules;
    }

    std::unordered_map<std::string, RewriteRuleValue>& RewriteRules() {
        static std::unordered_map<std::string, RewriteRuleValue> rules;
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
    void SetWrapField(const std::string& name, std::optional<T> value, Field WrapRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = WrapRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetCaseField(const std::string& name, std::optional<T> value, Field CaseRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = CaseRules()[name];
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

    template <typename T, typename Field>
    void SetAlignField(const std::string& name, std::optional<T> value, Field AlignRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = AlignRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetArrangeField(const std::string& name, std::optional<T> value, Field ArrangeRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = ArrangeRules()[name];
        entry.*field                            = std::move(value);
        ++Generation();
    }

    template <typename T, typename Field>
    void SetRewriteField(const std::string& name, std::optional<T> value, Field RewriteRuleValue::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        auto&                             entry = RewriteRules()[name];
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

void SetWrapPolicy(const std::string& name, std::optional<WrapPolicy> value) {
    SetWrapField(name, value, &WrapRuleValue::policy);
}

void SetWrapForceTrailingComma(const std::string& name, std::optional<bool> value) {
    SetWrapField(name, value, &WrapRuleValue::forceTrailingComma);
}

WrapRuleValue WrapRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(WrapRules(), name);
}

WrapRuleValue WrapRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<WrapRuleValue>(name, language, [](std::string_view n) { return WrapRuleFor(n); });
}

void SetCaseConvention(const std::string& name, std::optional<CaseConvention> value) {
    SetCaseField(name, value, &CaseRuleValue::convention);
}

CaseRuleValue CaseRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(CaseRules(), name);
}

CaseRuleValue CaseRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<CaseRuleValue>(name, language, [](std::string_view n) { return CaseRuleFor(n); });
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

void SetAlignEnabled(const std::string& name, std::optional<bool> value) {
    SetAlignField(name, value, &AlignRuleValue::enabled);
}

AlignRuleValue AlignRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(AlignRules(), name);
}

AlignRuleValue AlignRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<AlignRuleValue>(name, language, [](std::string_view n) { return AlignRuleFor(n); });
}

void SetArrangeEnabled(const std::string& name, std::optional<bool> value) {
    SetArrangeField(name, value, &ArrangeRuleValue::enabled);
}

void SetArrangeCaseInsensitive(const std::string& name, std::optional<bool> value) {
    SetArrangeField(name, value, &ArrangeRuleValue::caseInsensitive);
}

ArrangeRuleValue ArrangeRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(ArrangeRules(), name);
}

ArrangeRuleValue ArrangeRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<ArrangeRuleValue>(name, language, [](std::string_view n) { return ArrangeRuleFor(n); });
}

void SetRewriteQuoteStyle(const std::string& name, std::optional<QuoteStyle> value) {
    SetRewriteField(name, value, &RewriteRuleValue::quoteStyle);
}

RewriteRuleValue RewriteRuleFor(std::string_view name) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return RuleFor(RewriteRules(), name);
}

RewriteRuleValue RewriteRuleFor(std::string_view name, std::string_view language) {
    return ScopedRuleFor<RewriteRuleValue>(name, language, [](std::string_view n) { return RewriteRuleFor(n); });
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

WrapPolicy WrapPolicyByName(const std::string& name) {
    if (name == "never") {
        return WrapPolicy::Never;
    }
    if (name == "always") {
        return WrapPolicy::Always;
    }
    throw std::runtime_error("ned: invalid wrap policy \"" + name + "\" -- expected \"never\" or \"always\"");
}

std::string WrapPolicyName(WrapPolicy policy) {
    switch (policy) {
        case WrapPolicy::Never:
            return "never";
        case WrapPolicy::Always:
            return "always";
    }
    throw std::runtime_error("ned: internal error: unhandled WrapPolicy");
}

QuoteStyle QuoteStyleByName(const std::string& name) {
    if (name == "single") {
        return QuoteStyle::Single;
    }
    if (name == "double") {
        return QuoteStyle::Double;
    }
    throw std::runtime_error("ned: invalid quote style \"" + name + "\" -- expected \"single\" or \"double\"");
}

std::string QuoteStyleName(QuoteStyle style) {
    switch (style) {
        case QuoteStyle::Single:
            return "single";
        case QuoteStyle::Double:
            return "double";
    }
    throw std::runtime_error("ned: internal error: unhandled QuoteStyle");
}

CaseConvention CaseConventionByName(const std::string& name) {
    if (name == "none") {
        return CaseConvention::None;
    }
    if (name == "lowercase") {
        return CaseConvention::Lowercase;
    }
    if (name == "uppercase") {
        return CaseConvention::Uppercase;
    }
    if (name == "camel-case") {
        return CaseConvention::CamelCase;
    }
    if (name == "pascal-case") {
        return CaseConvention::PascalCase;
    }
    if (name == "snake-case") {
        return CaseConvention::SnakeCase;
    }
    if (name == "leading-snake-case") {
        return CaseConvention::LeadingSnakeCase;
    }
    if (name == "upper-snake-case") {
        return CaseConvention::UpperSnakeCase;
    }
    if (name == "screaming-snake-case") {
        return CaseConvention::ScreamingSnakeCase;
    }
    if (name == "lisp-case") {
        return CaseConvention::LispCase;
    }
    throw std::runtime_error("ned: invalid case convention \"" + name +
                             "\" -- expected one of none, lowercase, uppercase, camel-case, pascal-case, "
                             "snake-case, leading-snake-case, upper-snake-case, screaming-snake-case, lisp-case");
}

std::string CaseConventionName(CaseConvention convention) {
    switch (convention) {
        case CaseConvention::None:
            return "none";
        case CaseConvention::Lowercase:
            return "lowercase";
        case CaseConvention::Uppercase:
            return "uppercase";
        case CaseConvention::CamelCase:
            return "camel-case";
        case CaseConvention::PascalCase:
            return "pascal-case";
        case CaseConvention::SnakeCase:
            return "snake-case";
        case CaseConvention::LeadingSnakeCase:
            return "leading-snake-case";
        case CaseConvention::UpperSnakeCase:
            return "upper-snake-case";
        case CaseConvention::ScreamingSnakeCase:
            return "screaming-snake-case";
        case CaseConvention::LispCase:
            return "lisp-case";
    }
    throw std::runtime_error("ned: internal error: unhandled CaseConvention");
}

namespace {

    bool IsAsciiLower(char c) {
        return c >= 'a' && c <= 'z';
    }
    bool IsAsciiUpper(char c) {
        return c >= 'A' && c <= 'Z';
    }
    bool IsAsciiDigit(char c) {
        return c >= '0' && c <= '9';
    }

    // A single "word" of a separator-delimited convention (snake_case's
    // own "snake"/"case"). Each convention's own casing requirement per
    // word is one of these four shapes.
    enum class WordCase { AllLower,
                          AllUpper,
                          Capitalized };

    bool WordMatches(std::string_view word, WordCase wordCase) {
        if (word.empty()) {
            return false; // a leading/trailing/doubled separator, e.g. "foo__bar" or "_foo"
        }
        // A word with no letters at all (a pure-digit suffix like the "123"
        // in "MAX_VALUE_123") has no case to violate -- found live by this
        // rollout's own test suite (a real bug, not a hypothetical): the
        // first-character-must-be-a-letter checks below would otherwise
        // reject a perfectly ordinary numbered constant. Still requires
        // every byte be a digit (not, say, punctuation), just doesn't
        // impose a case requirement on it.
        if (std::none_of(word.begin(), word.end(), [](char c) { return IsAsciiLower(c) || IsAsciiUpper(c); })) {
            return std::all_of(word.begin(), word.end(), IsAsciiDigit);
        }
        // A real letter IS present past this point (the all-digit bypass
        // above already returned) -- the word's own FIRST character is
        // still required to be a letter of the right case, same as
        // Capitalized already does below. This is what keeps "123foo"
        // rejected as Lowercase (a digit-then-letters word, not pure
        // digits) while still accepting a pure-digit word like "123" --
        // confirmed by this rollout's own tests for both shapes.
        switch (wordCase) {
            case WordCase::AllLower:
                return IsAsciiLower(word.front()) &&
                       std::all_of(word.begin() + 1, word.end(),
                                   [](char c) { return IsAsciiLower(c) || IsAsciiDigit(c); });
            case WordCase::AllUpper:
                return IsAsciiUpper(word.front()) &&
                       std::all_of(word.begin() + 1, word.end(),
                                   [](char c) { return IsAsciiUpper(c) || IsAsciiDigit(c); });
            case WordCase::Capitalized:
                return IsAsciiUpper(word.front()) &&
                       std::all_of(word.begin() + 1, word.end(),
                                   [](char c) { return IsAsciiLower(c) || IsAsciiDigit(c); });
        }
        return false;
    }

    // "foo_bar_baz" / "Foo_Bar_Baz" / "FOO_BAR_BAZ" / "foo-bar-baz" shaped
    // conventions: split on `separator`, apply `firstWord`'s own case rule
    // to the first word and `restWords`' to every word after it (they
    // differ for LeadingSnakeCase alone; every other separated convention
    // uses the same rule for every word). A name with no separator at all
    // is just "one word", handled the same way as any other -- SnakeCase
    // correctly accepts a bare "foo", ScreamingSnakeCase a bare "FOO".
    bool MatchesSeparated(std::string_view name, char separator, WordCase firstWord, WordCase restWords) {
        std::size_t start   = 0;
        bool        isFirst = true;
        while (true) {
            const std::size_t      sep  = name.find(separator, start);
            const std::string_view word = (sep == std::string_view::npos) ? name.substr(start)
                                                                          : name.substr(start, sep - start);
            if (!WordMatches(word, isFirst ? firstWord : restWords)) {
                return false;
            }
            if (sep == std::string_view::npos) {
                return true;
            }
            start   = sep + 1;
            isFirst = false;
        }
    }

} // namespace

bool MatchesCaseConvention(std::string_view name, CaseConvention convention) {
    if (convention == CaseConvention::None) {
        return true;
    }
    if (name.empty()) {
        return false;
    }
    switch (convention) {
        case CaseConvention::None:
            return true; // unreachable, handled above -- kept for the switch's own exhaustiveness
        case CaseConvention::Lowercase:
            return WordMatches(name, WordCase::AllLower);
        case CaseConvention::Uppercase:
            return WordMatches(name, WordCase::AllUpper);
        case CaseConvention::CamelCase:
            return IsAsciiLower(name.front()) &&
                   std::all_of(name.begin() + 1, name.end(), [](char c) { return IsAsciiLower(c) || IsAsciiUpper(c) || IsAsciiDigit(c); });
        case CaseConvention::PascalCase:
            return IsAsciiUpper(name.front()) &&
                   std::all_of(name.begin() + 1, name.end(), [](char c) { return IsAsciiLower(c) || IsAsciiUpper(c) || IsAsciiDigit(c); });
        case CaseConvention::SnakeCase:
            return MatchesSeparated(name, '_', WordCase::AllLower, WordCase::AllLower);
        case CaseConvention::LeadingSnakeCase:
            return MatchesSeparated(name, '_', WordCase::Capitalized, WordCase::AllLower);
        case CaseConvention::UpperSnakeCase:
            return MatchesSeparated(name, '_', WordCase::Capitalized, WordCase::Capitalized);
        case CaseConvention::ScreamingSnakeCase:
            return MatchesSeparated(name, '_', WordCase::AllUpper, WordCase::AllUpper);
        case CaseConvention::LispCase:
            return MatchesSeparated(name, '-', WordCase::AllLower, WordCase::AllLower);
    }
    return false;
}

} // namespace ned::editor
