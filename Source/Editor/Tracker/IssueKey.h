//
// Issue keys in text no tracker wrote: a branch's name, the branch to make
// for an issue, and the key a new commit message starts from.
//

#ifndef NED_EDITOR_TRACKER_ISSUEKEY_H
#define NED_EDITOR_TRACKER_ISSUEKEY_H

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "Provider.h"

namespace ned::editor::tracker {

// The key a branch name carries. A Jira-style PROJ-123, delimited by
// anything but a letter or digit, found anywhere in it: written upper-case,
// or in any case once its prefix is one of projectKeys
// ("feature/dev-450-login" is DEV-450 once DEV is known).
// Otherwise, with numericKeys, a leading issue number in the last path
// segment ("42-fix-login", "issue-42", "fix/42") is "#42".
[[nodiscard]] std::optional<std::string> IssueKeyFromBranch(std::string_view branch, const std::set<std::string>& projectKeys,
                                                            bool numericKeys);

// "DEV-450-fix-the-login-crash" for DEV-450 "Fix the login crash!"; #42
// gives "42-...". The title becomes lower-case ASCII words joined by '-',
// cut at a word so the whole name stays readable in a branch list.
[[nodiscard]] std::string BranchNameForIssue(const Issue& issue);

// A new commit message's seed for key: keyTemplate for PROJ-123 keys,
// numericTemplate for #42 ones, each with {key} replaced. The seed goes in
// front of the commit template, and point lands at the end of its first
// line. The numeric default puts the key in the body, since git drops a
// first line that starts with '#'. An empty template seeds nothing.
inline constexpr std::string_view kDefaultCommitSeed        = "{key} ";
inline constexpr std::string_view kDefaultNumericCommitSeed = "\n\nRefs {key}";
void                              SetCommitSeeds(std::string keyTemplate, std::string numericTemplate);
[[nodiscard]] std::string         CommitSeed(const std::string& key);

// Whether any connection's provider writes its keys "#42".
[[nodiscard]] bool AnyNumericKeys();

// The key of the project's current branch (Vcs/CurrentBranch.h) against
// the registry's connections, or nullopt without either. Cached, so a
// mode line may ask on every paint.
[[nodiscard]] std::optional<std::string> CurrentIssueKey();

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_ISSUEKEY_H
