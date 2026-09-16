#include "Editor/MacroRegistry.h"

#include <map>
#include <mutex>
#include <stdexcept>

namespace ned::editor {

namespace {

    std::mutex& RegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // Ordered so MacroNames() is deterministic without a separate sort.
    std::map<std::string, std::vector<KeyChord>>& Registry() {
        static std::map<std::string, std::vector<KeyChord>> registry;
        return registry;
    }

} // namespace

void RegisterMacro(const std::string& name, const std::vector<KeyChord>& chords) {
    if (name.empty()) {
        throw std::runtime_error("RegisterMacro: name must not be empty");
    }
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    if (chords.empty()) {
        Registry().erase(name);
        return;
    }
    Registry()[name] = chords;
}

std::optional<std::vector<KeyChord>> MacroForName(const std::string& name) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    const auto                        entry = Registry().find(name);
    if (entry == Registry().end()) {
        return std::nullopt;
    }
    return entry->second;
}

std::vector<std::string> MacroNames() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    std::vector<std::string>          names;
    names.reserve(Registry().size());
    for (const auto& [name, chords] : Registry()) {
        names.push_back(name);
    }
    return names;
}

void ClearAllMacros() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().clear();
}

} // namespace ned::editor
