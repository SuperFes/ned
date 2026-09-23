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

    // Over the chain the caller already holds when there is one -- a query
    // walk knows the path it arrived by. NodeParent otherwise, which
    // re-descends from the tree root on every single step.
    bool NodeHasAncestorOfType(const PredicateOperand& operand, std::string_view typeName, bool immediateOnly) {
        if (!operand.ancestors.empty()) {
            for (const parse::RedNode ancestor : operand.ancestors) {
                if (parse::NodeType(ancestor) == typeName) {
                    return true;
                }
                if (immediateOnly) {
                    return false;
                }
            }
            return false;
        }
        for (parse::RedNode current = parse::NodeParent(operand.node); !parse::NodeIsNull(current); current = parse::NodeParent(current)) {
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

std::optional<std::regex> CompilePredicateRegex(std::string_view pattern) {
    // "(?i)" is Rust/Lua/PCRE inline-flag syntax, which ECMAScript has no
    // spelling for -- it is the flag, not a group, so it is lifted out of
    // the pattern rather than failing to compile.
    std::string translated = TranslateLuaPatternClasses(std::string(pattern));
    auto        flags      = std::regex::ECMAScript;
    for (std::size_t at = translated.find("(?i)"); at != std::string::npos; at = translated.find("(?i)", at)) {
        translated.erase(at, 4);
        flags |= std::regex::icase;
    }
    try {
        return std::regex(translated, flags);
    }
    catch (const std::regex_error&) {
        return std::nullopt; // a Lua-only construct std::regex can't parse -- don't block on it
    }
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
        if (operands[1].regexInvalid) {
            return true; // precompiled and rejected -- same inert result as failing here
        }
        const std::regex* compiled = operands[1].regex;
        if (compiled == nullptr) {
            // No precompiled pattern (a capture as the pattern operand, or a
            // caller that does not precompile): translate and compile here,
            // keyed by the pattern as written.
            std::string key     = std::string(*operands[1].text);
            auto        cacheIt = regexCache.find(key);
            if (cacheIt == regexCache.end()) {
                std::optional<std::regex> built = CompilePredicateRegex(*operands[1].text);
                if (!built) {
                    return true;
                }
                cacheIt = regexCache.emplace(std::move(key), std::move(*built)).first;
            }
            compiled = &cacheIt->second;
        }
        const bool matched = std::regex_search(operands[0].text->begin(), operands[0].text->end(), *compiled);
        return negated ? !matched : matched;
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
            if (NodeHasAncestorOfType(operands[0], *operands[i].text, baseName == "has-parent?")) {
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

bool PredicateMatchesRegex(std::string_view name, std::size_t operandCount) {
    const auto [baseName, negated] = ParsePredicateName(name);
    (void)negated; // a negated match is the same evaluation, inverted
    return (baseName == "match?" || baseName == "lua-match?") && operandCount == 2;
}

bool PredicateReadsOutsideSubtree(std::string_view name, std::size_t operandCount) {
    const auto [baseName, negated] = ParsePredicateName(name);
    (void)negated; // reads-outside-subtree-ness doesn't depend on negation
    return (baseName == "has-ancestor?" || baseName == "has-parent?") && operandCount >= 2;
}

} // namespace ned::editor::grammar
