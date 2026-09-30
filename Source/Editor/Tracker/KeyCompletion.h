//
// Issue keys typed into any buffer -- a commit message, a code comment, a
// README: where a key being typed starts, what completes it (the user's
// own issues), and keeping the per-connection caches it draws on fresh.
//

#ifndef NED_EDITOR_TRACKER_KEYCOMPLETION_H
#define NED_EDITOR_TRACKER_KEYCOMPLETION_H

#include <chrono>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Completion.h"
#include "Provider.h"

namespace ned::editor::tracker {

class Runner;

inline constexpr std::chrono::minutes kKeyCacheLifetime{5};

// Where the issue key being typed at the end of `before` starts: a known
// project key and '-' ("DEV-", "DEV-4"), or with numericKeys a '#' ("#",
// "#4"), not glued to the word or '#'/'&' before it. An offset into before.
[[nodiscard]] std::optional<std::size_t> IssueKeyPrefixStart(std::string_view before, const std::set<std::string>& projectKeys,
                                                             bool numericKeys);

// The issues whose keys continue prefix's project ("DEV-" of "DEV-4", or
// "#"), in the order given -- the completion session narrows on the
// digits typed after it.
[[nodiscard]] std::vector<Completion> IssueKeyCompletions(const std::vector<Issue>& issues, std::string_view prefix);

// A whole key in line covering byte offset: "DEV-450" with DEV a known
// project key, or with numericKeys "#42". For following it as a link.
struct KeyInLine {
    std::string key;
    std::size_t start = 0;
    std::size_t end   = 0; // exclusive
};
[[nodiscard]] std::optional<KeyInLine> IssueKeyAt(std::string_view line, std::size_t offset, const std::set<std::string>& projectKeys,
                                                  bool numericKeys);

// The connection to fetch key through: the one that listed it, else the one
// its project key belongs to, else (for "#42") the first whose provider
// writes keys that way.
[[nodiscard]] std::optional<std::string> ConnectionForKey(const std::string& key);

// Refetches each connection's project keys and own issues (Registry.h's
// SetProjectKeys/SetMineIssues) not attempted within kKeyCacheLifetime.
// Cheap when they are fresh, so any completion request may call it.
void RefreshKeyCaches(Runner& runner, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
// Test-only: forgets when each cache was last attempted.
void ResetKeyCacheAttempts();

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_KEYCOMPLETION_H
