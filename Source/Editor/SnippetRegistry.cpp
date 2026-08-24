#include "Editor/SnippetRegistry.h"

#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace ned::editor {

namespace {

    std::mutex& RegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // languageKey -> trigger -> body. Ordered maps so SnippetTriggers is
    // deterministic without a separate sort.
    std::map<std::string, std::map<std::string, std::string>>& Registry() {
        static std::map<std::string, std::map<std::string, std::string>> registry;
        return registry;
    }

} // namespace

void RegisterSnippet(const std::string& languageKey, const std::string& trigger, const std::string& body) {
    if (trigger.empty()) {
        throw std::runtime_error("RegisterSnippet: trigger must not be empty");
    }
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    if (body.empty()) {
        const auto tier = Registry().find(languageKey);
        if (tier != Registry().end()) {
            tier->second.erase(trigger);
            if (tier->second.empty()) {
                Registry().erase(tier);
            }
        }
        return;
    }
    Registry()[languageKey][trigger] = body;
}

std::optional<std::string> SnippetBodyForTrigger(const std::string& languageKey, const std::string& trigger) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    const auto                        lookup = [&trigger](const std::string& key) -> std::optional<std::string> {
        const auto tier = Registry().find(key);
        if (tier == Registry().end()) {
            return std::nullopt;
        }
        const auto entry = tier->second.find(trigger);
        if (entry == tier->second.end()) {
            return std::nullopt;
        }
        return entry->second;
    };
    if (!languageKey.empty()) {
        if (auto body = lookup(languageKey)) {
            return body;
        }
    }
    return lookup(std::string{});
}

std::vector<std::string> SnippetTriggers(const std::string& languageKey) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    std::set<std::string>             merged; // ordered -> sorted, deduplicated output
    const auto                        collect = [&merged](const std::string& key) {
        const auto tier = Registry().find(key);
        if (tier == Registry().end()) {
            return;
        }
        for (const auto& [trigger, body] : tier->second) {
            merged.insert(trigger);
        }
    };
    if (!languageKey.empty()) {
        collect(languageKey);
    }
    collect(std::string{});
    return std::vector<std::string>(merged.begin(), merged.end());
}

void ClearAllSnippets() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().clear();
}

} // namespace ned::editor
