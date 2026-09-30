#include "IssueKey.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "Editor/Project/Root.h"
#include "Editor/Vcs/CurrentBranch.h"
#include "Registry.h"

namespace ned::editor::tracker {

namespace {

    bool IsDigit(char c) {
        return c >= '0' && c <= '9';
    }
    bool IsAlpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    bool IsAlnum(char c) {
        return IsAlpha(c) || IsDigit(c);
    }
    char Upper(char c) {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    char Lower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::optional<std::string> ProjectStyleKey(std::string_view branch, const std::set<std::string>& projectKeys) {
        for (std::size_t start = 0; start < branch.size(); ++start) {
            if (!IsAlpha(branch[start]) || (start > 0 && IsAlnum(branch[start - 1]))) {
                continue;
            }
            std::size_t dash = start;
            while (dash < branch.size() && (IsAlnum(branch[dash]) || branch[dash] == '_')) {
                ++dash;
            }
            std::size_t end = dash + 1;
            while (end < branch.size() && IsDigit(branch[end])) {
                ++end;
            }
            if (dash >= branch.size() || branch[dash] != '-' || end == dash + 1 || (end < branch.size() && IsAlnum(branch[end]))) {
                continue;
            }
            const std::string_view prefix = branch.substr(start, dash - start);
            std::string            upper;
            std::ranges::transform(prefix, std::back_inserter(upper), Upper);
            const bool accepted = (prefix == upper && prefix.size() >= 2) || projectKeys.contains(upper);
            if (accepted) {
                return upper + std::string(branch.substr(dash, end - dash));
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> NumericKey(std::string_view branch) {
        std::string_view segment = branch.substr(branch.rfind('/') == std::string_view::npos ? 0 : branch.rfind('/') + 1);
        for (const std::string_view word : {"issues", "issue"}) {
            if (segment.size() > word.size() && std::ranges::equal(segment.substr(0, word.size()), word, {}, Lower)) {
                segment.remove_prefix(word.size());
                if (!segment.empty() && (segment.front() == '-' || segment.front() == '_')) {
                    segment.remove_prefix(1);
                }
                break;
            }
        }
        std::size_t end = 0;
        while (end < segment.size() && IsDigit(segment[end])) {
            ++end;
        }
        if (end == 0 || (end < segment.size() && segment[end] != '-' && segment[end] != '_')) {
            return std::nullopt;
        }
        return "#" + std::string(segment.substr(0, end));
    }

    struct Seeds {
        std::mutex  mutex;
        std::string keyTemplate     = std::string(kDefaultCommitSeed);
        std::string numericTemplate = std::string(kDefaultNumericCommitSeed);
    };

    Seeds& CommitSeeds() {
        static Seeds seeds;
        return seeds;
    }

} // namespace

std::optional<std::string> IssueKeyFromBranch(std::string_view branch, const std::set<std::string>& projectKeys, bool numericKeys) {
    if (auto key = ProjectStyleKey(branch, projectKeys)) {
        return key;
    }
    return numericKeys ? NumericKey(branch) : std::nullopt;
}

std::string BranchNameForIssue(const Issue& issue) {
    constexpr std::size_t kMaxTitleLength = 40;

    std::vector<std::string> words(1);
    for (const char c : issue.title) {
        if (IsAlnum(c)) {
            words.back() += Lower(c);
        }
        else if (!words.back().empty()) {
            words.emplace_back();
        }
    }
    std::string title;
    for (const std::string& word : words) {
        if (word.empty() || (!title.empty() && title.size() + 1 + word.size() > kMaxTitleLength)) {
            break;
        }
        title += (title.empty() ? "" : "-") + word;
    }
    // One long first word: cut it rather than lose the whole title.
    title.resize(std::min(title.size(), kMaxTitleLength));

    const std::string name = issue.key.starts_with('#') ? issue.key.substr(1) : issue.key;
    return title.empty() ? name : name + "-" + title;
}

void SetCommitSeeds(std::string keyTemplate, std::string numericTemplate) {
    Seeds&                seeds = CommitSeeds();
    const std::lock_guard lock(seeds.mutex);
    seeds.keyTemplate     = std::move(keyTemplate);
    seeds.numericTemplate = std::move(numericTemplate);
}

std::string CommitSeed(const std::string& key) {
    std::string seed;
    {
        Seeds&                seeds = CommitSeeds();
        const std::lock_guard lock(seeds.mutex);
        seed = key.starts_with('#') ? seeds.numericTemplate : seeds.keyTemplate;
    }
    constexpr std::string_view kPlaceholder = "{key}";
    for (std::size_t at = seed.find(kPlaceholder); at != std::string::npos; at = seed.find(kPlaceholder, at + key.size())) {
        seed.replace(at, kPlaceholder.size(), key);
    }
    return seed;
}

bool AnyNumericKeys() {
    return std::ranges::any_of(Connections(), [](const Connection& connection) {
        const auto provider = FindProvider(connection.provider);
        return provider && provider->NumericKeys();
    });
}

std::optional<std::string> CurrentIssueKey() {
    const std::optional<std::string> branch = vcs::CurrentBranch(ProjectRoot());
    if (!branch) {
        return std::nullopt;
    }

    struct Cache {
        std::mutex                 mutex;
        std::string                branch;
        std::uint64_t              revision = 0;
        bool                       valid    = false;
        std::optional<std::string> key;
    };
    static Cache cache;

    const std::uint64_t   revision = Revision();
    const std::lock_guard lock(cache.mutex);
    if (cache.valid && cache.branch == *branch && cache.revision == revision) {
        return cache.key;
    }
    cache.key      = Connections().empty() ? std::nullopt : IssueKeyFromBranch(*branch, KnownProjectKeys(), AnyNumericKeys());
    cache.branch   = *branch;
    cache.revision = revision;
    cache.valid    = true;
    return cache.key;
}

} // namespace ned::editor::tracker
