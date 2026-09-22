#include "QueryPredicates.h"

#include <utility>

namespace ned::editor::grammar {

namespace {

    // Shared by EvaluatePredicateCall and PredicateReadsOutsideSubtree so the
    // two can never disagree about which call a name/arity pair denotes.
    struct ParsedPredicateName {
        std::string_view baseName;
        bool             negated = false;
    };

    ParsedPredicateName ParsePredicateName(std::string_view name) {
        if (!name.empty() && (name.front() == '#' || name.front() == ':')) {
            name.remove_prefix(1);
        }
        const bool negated = name.starts_with("not-");
        return ParsedPredicateName{.baseName = negated ? name.substr(4) : name, .negated = negated};
    }

    bool NodeHasAncestorOfType(parse::RedNode node, std::string_view typeName, bool immediateOnly) {
        for (parse::RedNode current = parse::NodeParent(node); !parse::NodeIsNull(current); current = parse::NodeParent(current)) {
            if (parse::NodeType(current) == typeName) {
                return true;
            }
            if (immediateOnly) {
                return false;
            }
        }
        return false;
    }

} // namespace

std::string TranslateLuaPatternClasses(std::string pattern) {
    static constexpr std::pair<std::string_view, std::string_view> kClasses[] = {
        {"%u", "A-Z"},
        {"%l", "a-z"},
        {"%d", "0-9"},
        {"%a", "A-Za-z"},
        {"%s", " \\t\\n\\r\\f\\v"},
        {"%w", "A-Za-z0-9"},
        {"%p", "!-/:-@\\[-`{-~"},
    };
    for (const auto& [luaClass, body] : kClasses) {
        const std::string bracketed        = "[" + std::string(luaClass) + "]";
        const std::string bracketedReplace = "[" + std::string(body) + "]";
        for (std::size_t pos = 0; (pos = pattern.find(bracketed, pos)) != std::string::npos;) {
            pattern.replace(pos, bracketed.size(), bracketedReplace);
            pos += bracketedReplace.size();
        }
        const std::string bareReplace = "[" + std::string(body) + "]";
        for (std::size_t pos = 0; (pos = pattern.find(luaClass, pos)) != std::string::npos;) {
            pattern.replace(pos, luaClass.size(), bareReplace);
            pos += bareReplace.size();
        }
    }
    return pattern;
}

bool EvaluatePredicateCall(std::string_view name, std::span<const PredicateOperand> operands,
                           std::unordered_map<std::string, std::regex>& regexCache) {
    const auto [baseName, negated] = ParsePredicateName(name);

    if (baseName == "eq?") {
        if (operands.size() != 2) {
            return true;
        }
        if (!operands[0].text || !operands[1].text) {
            return true;
        }
        return negated ? (*operands[0].text != *operands[1].text) : (*operands[0].text == *operands[1].text);
    }

    if (baseName == "match?" || baseName == "lua-match?") {
        if (operands.size() != 2) {
            return true;
        }
        if (!operands[0].text || !operands[1].text) {
            return true;
        }
        try {
            std::string translated = TranslateLuaPatternClasses(std::string(*operands[1].text));
            auto        cacheIt    = regexCache.find(translated);
            if (cacheIt == regexCache.end()) {
                // "(?i)" is Rust/Lua/PCRE inline-flag syntax, which ECMAScript
                // has no spelling for -- it is the flag, not a group, so it is
                // lifted out of the pattern rather than failing to compile
                // (the cache key keeps the original spelling, so the same
                // pattern without the flag is a separate entry).
                std::string      pattern = translated;
                auto             flags   = std::regex::ECMAScript;
                for (std::size_t at = pattern.find("(?i)"); at != std::string::npos; at = pattern.find("(?i)", at)) {
                    pattern.erase(at, 4);
                    flags |= std::regex::icase;
                }
                std::regex compiled(pattern, flags);
                cacheIt = regexCache.emplace(std::move(translated), std::move(compiled)).first;
            }
            const bool matched = std::regex_search(operands[0].text->begin(), operands[0].text->end(), cacheIt->second);
            return negated ? !matched : matched;
        }
        catch (const std::regex_error&) {
            return true; // a Lua-only pattern construct std::regex can't parse -- don't block on it
        }
    }

    if (baseName == "any-of?") {
        if (operands.empty()) {
            return true;
        }
        if (!operands[0].text) {
            return true;
        }
        bool found = false;
        for (std::size_t i = 1; i < operands.size(); ++i) {
            if (operands[i].text && *operands[i].text == *operands[0].text) {
                found = true;
                break;
            }
        }
        return negated ? !found : found;
    }

    if (baseName == "has-ancestor?" || baseName == "has-parent?") {
        if (operands.size() < 2 || !operands[0].isCapture) {
            return true;
        }
        if (parse::NodeIsNull(operands[0].node)) {
            return true;
        }
        bool sawTypeOperand = false;
        bool has            = false;
        for (std::size_t i = 1; i < operands.size(); ++i) {
            if (operands[i].isCapture || !operands[i].text) {
                continue;
            }
            sawTypeOperand = true;
            if (NodeHasAncestorOfType(operands[0].node, *operands[i].text, baseName == "has-parent?")) {
                has = true;
                break;
            }
        }
        if (!sawTypeOperand) {
            return true; // no usable type operand at all -- same inert quirk as every other malformed call
        }
        return negated ? !has : has;
    }

    return true; // unrecognized predicate name (e.g. "set!") -- inert
}

bool PredicateReadsOutsideSubtree(std::string_view name, std::size_t operandCount) {
    const auto [baseName, negated] = ParsePredicateName(name);
    (void)negated; // reads-outside-subtree-ness doesn't depend on negation
    return (baseName == "has-ancestor?" || baseName == "has-parent?") && operandCount >= 2;
}

} // namespace ned::editor::grammar
