//
// Adapts a Janet-defined VCS plugin onto editor::vcs::VcsProvider. See
// ned/vcs-register-provider (EditorBindings.cpp) for how one of these
// gets constructed and registered.
//

#ifndef NED_JANET_JANETVCSPROVIDER_H
#define NED_JANET_JANETVCSPROVIDER_H

#include <janet.h>

#include <map>
#include <string>

#include "Editor/Vcs/VcsProvider.h"

namespace ned::janet {

// A plugin supplies its callbacks as one struct/table keyed by keyword
// (:detect, :blame-argv, :parse-blame, :log-argv, :parse-log, :diff-argv,
// :parse-diff, :working-diff-argv -- multibuffers follow-up -- :status-argv,
// :parse-status, :stage-argv, :unstage-argv,
// :staged-diff-argv, :stage-patch-argv, :unstage-patch-argv -- hunk-staging
// follow-up -- :commit-argv, :branch-list-argv, :parse-branch-list,
// :branch-switch-argv, :branch-create-argv) -- vocabulary-completion
// follow-up, replacing the
// original 7-positional-argument form outright once the vocabulary grew
// past what positional arguments could carry legibly. Only :detect is
// required; an operation whose callback is absent falls through to
// VcsProvider's own default-throwing implementation, so "this provider
// doesn't do branches" degrades to the same clear status-line error a
// C++ provider would produce, with the wording defined in exactly one
// place (VcsProvider.h).
//
// Each present callback is bound into env under a generated name via
// janet_def and invoked later through janet_dostring -- the same
// "janet_def + janet_dostring, never RootedValue + janet_pcall" pattern
// NedRegisterCommand (EditorBindings.cpp) already established, for the
// same reason: this Janet build (1.32.1) corrupts state when a rooted
// value is later invoked via janet_pcall (see Value.h's RootedValue
// CAUTION comment).
//
// Every method here is a synchronous janet_dostring call and must only
// ever run on the thread that owns env (the main/UI thread) -- see
// VcsProvider.h's header comment for why VcsRunner (Editor/Vcs/VcsRunner.h)
// is careful to call these only before/after, never during, a background
// subprocess wait.
class JanetVcsProvider : public editor::vcs::VcsProvider {
  public:
    // callbacks must be a Janet struct or table; throws std::runtime_error
    // if it isn't, or if :detect is missing (a provider that can't detect
    // a repository is unreachable by ActiveProviderFor, so registering one
    // is always a plugin bug worth failing loudly at registration time).
    JanetVcsProvider(JanetTable* env, std::string name, Janet callbacks);

    [[nodiscard]] bool Detect(const std::filesystem::path& root) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec            BlameArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsBlameLine> ParseBlame(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec           LogArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsLogEntry> ParseLog(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec           DiffArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsDiffHunk> ParseDiff(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec WorkingDiffArgv(const std::filesystem::path& root) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec              StatusArgv(const std::filesystem::path& root) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsStatusEntry> ParseStatus(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec StageArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] editor::vcs::VcsCommandSpec UnstageArgv(const std::filesystem::path& path) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec StagedDiffArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] editor::vcs::VcsCommandSpec StagePatchArgv(const std::filesystem::path& root,
                                                             const std::filesystem::path& patchPath) const override;
    [[nodiscard]] editor::vcs::VcsCommandSpec UnstagePatchArgv(const std::filesystem::path& root,
                                                               const std::filesystem::path& patchPath) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec CommitArgv(const std::filesystem::path& root,
                                                         const std::string&           message) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec              BranchListArgv(const std::filesystem::path& root) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsBranchEntry> ParseBranchList(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec BranchSwitchArgv(const std::filesystem::path& root,
                                                               const std::string&           name) const override;
    [[nodiscard]] editor::vcs::VcsCommandSpec BranchCreateArgv(const std::filesystem::path& root,
                                                               const std::string&           name) const override;

  private:
    // The generated janet_def name for callback key ("detect",
    // "blame-argv", ...), or nullptr if the plugin didn't supply it --
    // callers fall through to VcsProvider's default (throwing) behavior on
    // nullptr.
    [[nodiscard]] const std::string* InternalName(const std::string& key) const;

    // Binds arg as a global Janet string under a generated temp name and
    // invokes "(fnInternalName tempName)" via janet_dostring, returning the
    // raw result. Binding the *value* (rather than splicing arg as escaped
    // source text into the call form) sidesteps needing any string-literal
    // escaping at all -- arg can contain quotes/backslashes/anything else
    // a real file path might, with no special-casing required.
    [[nodiscard]] Janet CallWithString(const std::string& fnInternalName, const std::string& arg) const;

    // Two-argument variant of CallWithString, for the operations whose
    // argv builder needs both a root and a second string (commit's
    // message, branch-switch/-create's branch name) -- same
    // bind-the-values technique, two temp names.
    [[nodiscard]] Janet CallWithStrings(const std::string& fnInternalName, const std::string& first,
                                        const std::string& second) const;

    JanetTable* env_;
    std::string name_;

    std::map<std::string, std::string> callbackNames_;
};

} // namespace ned::janet

#endif // NED_JANET_JANETVCSPROVIDER_H
