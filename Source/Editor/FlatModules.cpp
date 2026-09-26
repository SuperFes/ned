#include "FlatModules.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "Editor/GitIgnore.h"

namespace ned::editor {

namespace {

    constexpr std::size_t kMaxFlatModuleEntries = 50000;

    std::string Lowered(std::string_view text) {
        std::string lowered(text);
        std::ranges::transform(lowered, lowered.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

} // namespace

std::optional<std::filesystem::path> FindFlatModule(std::string_view name, const std::vector<std::string>& extensions,
                                                    const std::filesystem::path& root) {
    if (name.empty() || extensions.empty() || root.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> wanted;
    for (const std::string& extension : extensions) {
        wanted.push_back(Lowered(name) + "." + Lowered(extension));
    }

    std::error_code ec;
    auto            it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        return std::nullopt;
    }
    const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(root);

    std::optional<std::filesystem::path> best;
    std::size_t                          bestRank = wanted.size();
    std::size_t                          visited  = 0;
    for (const auto end = std::filesystem::recursive_directory_iterator(); it != end; it.increment(ec)) {
        if (ec || ++visited > kMaxFlatModuleEntries) {
            break;
        }
        const std::filesystem::directory_entry& entry    = *it;
        const std::string                       filename = entry.path().filename().string();
        const std::filesystem::path             relative = entry.path().lexically_relative(root);
        if (entry.is_directory(ec)) {
            if (filename.starts_with('.') || gitIgnore.IsIgnored(relative, /*isDirectory=*/true)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        const auto match = std::ranges::find(wanted, Lowered(filename));
        if (match == wanted.end() || !entry.is_regular_file(ec)) {
            continue;
        }
        const auto rank = static_cast<std::size_t>(match - wanted.begin());
        if (rank < bestRank || (rank == bestRank && entry.path() < *best)) {
            best     = entry.path();
            bestRank = rank;
        }
    }
    return best;
}

} // namespace ned::editor
