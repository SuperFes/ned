#include "QueryPredicates.h"

#include <utility>

namespace ned::editor::treesitter {

namespace {

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
    if (!name.empty() && (name.front() == '#' || name.front() == ':')) {
        name.remove_prefix(1);
    }
    const bool             negated  = name.starts_with("not-");
    const std::string_view baseName = negated ? name.substr(4) : name;

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
                std::regex compiled(translated, std::regex::ECMAScript);
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
        if (operands.size() != 2 || !operands[0].isCapture || operands[1].isCapture) {
            return true;
        }
        if (parse::NodeIsNull(operands[0].node)) {
            return true;
        }
        if (!operands[1].text) {
            return true;
        }
        const bool has = NodeHasAncestorOfType(operands[0].node, *operands[1].text, baseName == "has-parent?");
        return negated ? !has : has;
    }

    return true; // unrecognized predicate name (e.g. "set!") -- inert
}

} // namespace ned::editor::treesitter
