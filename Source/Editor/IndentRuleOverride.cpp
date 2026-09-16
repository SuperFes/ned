#include "IndentRuleOverride.h"

#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace ned::editor {

namespace {

    bool operator==(const IndentRuleValue& a, const IndentRuleValue& b) {
        return a.policy == b.policy && (!a.policy || a.value == b.value);
    }

    std::mutex& IndentRulesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, IndentRuleValue>& IndentRules() {
        static std::unordered_map<std::string, IndentRuleValue> rules;
        return rules;
    }

    // FormatRules.cpp's own ValidateCaptureName, duplicated rather than
    // shared -- see that file's own comment on why (a small, self-contained
    // rule, the same tolerance this codebase already extends to other
    // small per-file helpers).
    void ValidateKey(std::string_view key) {
        const bool malformed = key.empty() || key.front() == '@' || key.front() == '.' || key.back() == '.' ||
                               key.find("..") != std::string_view::npos || key.find_first_of(" \t\n") != std::string_view::npos;
        if (malformed) {
            throw std::runtime_error("ned: invalid indent-rule key \"" + std::string(key) +
                                     "\" -- expected a grammar node-type name, e.g. \"access_specifier\"");
        }
    }

} // namespace

void SetIndentRule(const std::string& key, std::optional<IndentRuleValue> value) {
    ValidateKey(key);
    const std::lock_guard<std::mutex> lock(IndentRulesMutex());
    if (value) {
        IndentRules()[key] = *value;
    }
    else {
        IndentRules().erase(key);
    }
}

IndentRuleValue IndentRuleFor(std::string_view key) {
    const std::lock_guard<std::mutex> lock(IndentRulesMutex());
    const auto                        it = IndentRules().find(std::string(key));
    return it != IndentRules().end() ? it->second : IndentRuleValue{};
}

IndentRuleValue IndentRuleFor(std::string_view key, std::string_view language) {
    if (!language.empty()) {
        std::string scoped;
        scoped.reserve(language.size() + 1 + key.size());
        scoped.append(language);
        scoped.push_back('/');
        scoped.append(key);
        if (const IndentRuleValue scopedValue = IndentRuleFor(std::string_view(scoped)); scopedValue.policy) {
            return scopedValue;
        }
    }
    return IndentRuleFor(key);
}

} // namespace ned::editor
