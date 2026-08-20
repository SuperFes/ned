#include "VcsProviderRegistry.h"

#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ned::editor::vcs {

namespace {

    std::mutex& RegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // registration order matters for ActiveProviderFor's "first match
    // wins" rule, so providers are kept in a vector rather than an
    // unordered_map; g_index lets RegisterProvider still overwrite an
    // existing name in place instead of appending a duplicate.
    std::vector<std::pair<std::string, std::unique_ptr<VcsProvider>>>& Providers() {
        static std::vector<std::pair<std::string, std::unique_ptr<VcsProvider>>> providers;
        return providers;
    }

    std::unordered_map<std::string, std::size_t>& NameIndex() {
        static std::unordered_map<std::string, std::size_t> index;
        return index;
    }

    std::unordered_map<std::filesystem::path, VcsProvider*>& RootCache() {
        static std::unordered_map<std::filesystem::path, VcsProvider*> cache;
        return cache;
    }

} // namespace

void RegisterProvider(const std::string& name, std::unique_ptr<VcsProvider> provider) {
    const std::lock_guard lock(RegistryMutex());

    auto& providers = Providers();
    auto& index      = NameIndex();

    if (const auto it = index.find(name); it != index.end()) {
        providers[it->second].second = std::move(provider);
        return;
    }

    index.emplace(name, providers.size());
    providers.emplace_back(name, std::move(provider));
}

VcsProvider* ActiveProviderFor(const std::filesystem::path& root) {
    const std::lock_guard lock(RegistryMutex());

    auto& cache = RootCache();
    if (const auto it = cache.find(root); it != cache.end()) {
        return it->second;
    }

    for (const auto& [name, provider] : Providers()) {
        if (provider->Detect(root)) {
            cache.emplace(root, provider.get());
            return provider.get();
        }
    }

    cache.emplace(root, nullptr);
    return nullptr;
}

void ClearProviderCache() {
    const std::lock_guard lock(RegistryMutex());
    RootCache().clear();
}

void ClearRegistry() {
    const std::lock_guard lock(RegistryMutex());
    Providers().clear();
    NameIndex().clear();
    RootCache().clear();
}

} // namespace ned::editor::vcs
