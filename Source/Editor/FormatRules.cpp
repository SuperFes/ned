#include "FormatRules.h"

#include <algorithm>
#include <array>
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
        return a.quoteStyle == b.quoteStyle && a.expandElseif == b.expandElseif;
    }

    template <typename T>
    void FillUnset(std::optional<T>& into, const std::optional<T>& from) {
        if (!into) {
            into = from;
        }
    }

    // Fills every field `into` leaves unset from `from` -- how a higher
    // layer's partial entry falls through to a lower layer field by field.
    void FillUnset(SpaceRuleValue& into, const SpaceRuleValue& from) {
        FillUnset(into.before, from.before);
        FillUnset(into.after, from.after);
        FillUnset(into.within, from.within);
    }

    void FillUnset(BreakRuleValue& into, const BreakRuleValue& from) {
        FillUnset(into.before, from.before);
        FillUnset(into.after, from.after);
        FillUnset(into.placement, from.placement);
        FillUnset(into.collapseEmpty, from.collapseEmpty);
        FillUnset(into.collapseSimple, from.collapseSimple);
    }

    void FillUnset(BlankRuleValue& into, const BlankRuleValue& from) {
        FillUnset(into.minBefore, from.minBefore);
        FillUnset(into.maxBefore, from.maxBefore);
    }

    void FillUnset(WrapRuleValue& into, const WrapRuleValue& from) {
        FillUnset(into.policy, from.policy);
        FillUnset(into.forceTrailingComma, from.forceTrailingComma);
    }

    void FillUnset(CaseRuleValue& into, const CaseRuleValue& from) {
        FillUnset(into.convention, from.convention);
    }

    void FillUnset(AlignRuleValue& into, const AlignRuleValue& from) {
        FillUnset(into.enabled, from.enabled);
    }

    void FillUnset(ArrangeRuleValue& into, const ArrangeRuleValue& from) {
        FillUnset(into.enabled, from.enabled);
        FillUnset(into.caseInsensitive, from.caseInsensitive);
    }

    void FillUnset(RewriteRuleValue& into, const RewriteRuleValue& from) {
        FillUnset(into.quoteStyle, from.quoteStyle);
        FillUnset(into.expandElseif, from.expandElseif);
    }

    template <typename Value>
    using RuleMap = std::unordered_map<std::string, Value>;

    struct RuleTables {
        RuleMap<SpaceRuleValue>   space;
        RuleMap<BreakRuleValue>   breaks;
        RuleMap<BlankRuleValue>   blank;
        RuleMap<WrapRuleValue>    wrap;
        RuleMap<CaseRuleValue>    caseRules;
        RuleMap<AlignRuleValue>   align;
        RuleMap<ArrangeRuleValue> arrange;
        RuleMap<RewriteRuleValue> rewrite;
    };

    constexpr std::size_t kLayerCount = 3;

    // Highest precedence first -- the order lookups walk the layers in.
    constexpr std::array<FormatRuleLayer, kLayerCount> kPrecedence = {FormatRuleLayer::Runtime, FormatRuleLayer::File,
                                                                      FormatRuleLayer::Builtin};

    std::mutex& RulesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::array<RuleTables, kLayerCount>& Layers() {
        static std::array<RuleTables, kLayerCount> layers;
        return layers;
    }

    RuleTables& Layer(FormatRuleLayer layer) {
        return Layers()[static_cast<std::size_t>(layer)];
    }

    bool& BuiltinStyleEnabled() {
        static bool enabled = true;
        return enabled;
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

    template <typename Value, typename T>
    void SetField(RuleMap<Value> RuleTables::* table, FormatRuleLayer layer, const std::string& name,
                  std::optional<T> value, std::optional<T> Value::* field) {
        ValidateCaptureName(name);
        const std::lock_guard<std::mutex> lock(RulesMutex());
        (Layer(layer).*table)[name].*field = std::move(value);
        ++Generation();
    }

    template <typename Value>
    Value EntryFor(const RuleMap<Value>& rules, std::string_view name) {
        const auto it = rules.find(std::string(name));
        return it != rules.end() ? it->second : Value{};
    }

    // Within one layer, "<language>/<name>" wins as a whole entry over the
    // unscoped one -- SyntaxClassOverrideForCapture(name, language)'s shape.
    // Across layers, fields merge: a higher layer only shadows the fields it
    // actually sets, so a user's unscoped rule still beats a bundled
    // language-scoped default for that one field.
    template <typename Value>
    Value Resolve(RuleMap<Value> RuleTables::* table, std::string_view name, std::string_view language) {
        const std::lock_guard<std::mutex> lock(RulesMutex());
        Value merged{};
        for (const FormatRuleLayer layer : kPrecedence) {
            if (layer == FormatRuleLayer::Builtin && !BuiltinStyleEnabled()) {
                continue;
            }
            const RuleMap<Value>& rules = Layer(layer).*table;
            Value                 entry{};
            if (!language.empty()) {
                std::string scoped;
                scoped.reserve(language.size() + 1 + name.size());
                scoped.append(language);
                scoped.push_back('/');
                scoped.append(name);
                entry = EntryFor(rules, scoped);
            }
            if (entry == Value{}) {
                entry = EntryFor(rules, name);
            }
            FillUnset(merged, entry);
        }
        return merged;
    }

} // namespace

void SetSpaceBefore(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::space, layer, name, value, &SpaceRuleValue::before);
}

void SetSpaceAfter(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::space, layer, name, value, &SpaceRuleValue::after);
}

void SetSpaceWithin(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::space, layer, name, value, &SpaceRuleValue::within);
}

SpaceRuleValue SpaceRuleFor(std::string_view name) {
    return Resolve(&RuleTables::space, name, {});
}

SpaceRuleValue SpaceRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::space, name, language);
}

void SetBreakBefore(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::breaks, layer, name, value, &BreakRuleValue::before);
}

void SetBreakAfter(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::breaks, layer, name, value, &BreakRuleValue::after);
}

void SetBracePlacement(const std::string& name, std::optional<BracePlacement> value, FormatRuleLayer layer) {
    SetField(&RuleTables::breaks, layer, name, value, &BreakRuleValue::placement);
}

void SetBraceCollapseEmpty(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::breaks, layer, name, value, &BreakRuleValue::collapseEmpty);
}

void SetBraceCollapseSimple(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::breaks, layer, name, value, &BreakRuleValue::collapseSimple);
}

BreakRuleValue BreakRuleFor(std::string_view name) {
    return Resolve(&RuleTables::breaks, name, {});
}

BreakRuleValue BreakRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::breaks, name, language);
}

void SetWrapPolicy(const std::string& name, std::optional<WrapPolicy> value, FormatRuleLayer layer) {
    SetField(&RuleTables::wrap, layer, name, value, &WrapRuleValue::policy);
}

void SetWrapForceTrailingComma(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::wrap, layer, name, value, &WrapRuleValue::forceTrailingComma);
}

WrapRuleValue WrapRuleFor(std::string_view name) {
    return Resolve(&RuleTables::wrap, name, {});
}

WrapRuleValue WrapRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::wrap, name, language);
}

void SetCaseConvention(const std::string& name, std::optional<CaseConvention> value, FormatRuleLayer layer) {
    SetField(&RuleTables::caseRules, layer, name, value, &CaseRuleValue::convention);
}

CaseRuleValue CaseRuleFor(std::string_view name) {
    return Resolve(&RuleTables::caseRules, name, {});
}

CaseRuleValue CaseRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::caseRules, name, language);
}

void SetBlankMinBefore(const std::string& name, std::optional<int> value, FormatRuleLayer layer) {
    SetField(&RuleTables::blank, layer, name, value, &BlankRuleValue::minBefore);
}

void SetBlankMaxBefore(const std::string& name, std::optional<int> value, FormatRuleLayer layer) {
    SetField(&RuleTables::blank, layer, name, value, &BlankRuleValue::maxBefore);
}

BlankRuleValue BlankRuleFor(std::string_view name) {
    return Resolve(&RuleTables::blank, name, {});
}

BlankRuleValue BlankRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::blank, name, language);
}

void SetAlignEnabled(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::align, layer, name, value, &AlignRuleValue::enabled);
}

AlignRuleValue AlignRuleFor(std::string_view name) {
    return Resolve(&RuleTables::align, name, {});
}

AlignRuleValue AlignRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::align, name, language);
}

void SetArrangeEnabled(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::arrange, layer, name, value, &ArrangeRuleValue::enabled);
}

void SetArrangeCaseInsensitive(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::arrange, layer, name, value, &ArrangeRuleValue::caseInsensitive);
}

ArrangeRuleValue ArrangeRuleFor(std::string_view name) {
    return Resolve(&RuleTables::arrange, name, {});
}

ArrangeRuleValue ArrangeRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::arrange, name, language);
}

void SetRewriteQuoteStyle(const std::string& name, std::optional<QuoteStyle> value, FormatRuleLayer layer) {
    SetField(&RuleTables::rewrite, layer, name, value, &RewriteRuleValue::quoteStyle);
}

void SetRewriteExpandElseif(const std::string& name, std::optional<bool> value, FormatRuleLayer layer) {
    SetField(&RuleTables::rewrite, layer, name, value, &RewriteRuleValue::expandElseif);
}

RewriteRuleValue RewriteRuleFor(std::string_view name) {
    return Resolve(&RuleTables::rewrite, name, {});
}

RewriteRuleValue RewriteRuleFor(std::string_view name, std::string_view language) {
    return Resolve(&RuleTables::rewrite, name, language);
}

void ClearFormatRuleLayer(FormatRuleLayer layer) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    Layer(layer) = RuleTables{};
    ++Generation();
}

void SetBuiltinFormatStyleEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    if (BuiltinStyleEnabled() != enabled) {
        BuiltinStyleEnabled() = enabled;
        ++Generation();
    }
}

bool BuiltinFormatStyleEnabled() {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return BuiltinStyleEnabled();
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
