//
// Fetches a panel's issues, or one issue's detail: resolves panel ->
// connection -> provider, builds argv and parses output on the main
// thread, and runs the command through TaskProcess in between.
//

#ifndef NED_EDITOR_TRACKER_RUNNER_H
#define NED_EDITOR_TRACKER_RUNNER_H

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/Tasks/TaskProcess.h"
#include "Provider.h"

namespace ned::editor::tracker {

// The completion half of a fetch, separate so it is testable without a
// running event loop: a failed or unstartable command reports its output,
// a provider parse error reports its message.
[[nodiscard]] std::expected<std::vector<Issue>, std::string> FinishList(const Provider& provider, const std::string& panelName,
                                                                        const std::string& output, std::optional<int> exitCode);

// FinishList's counterpart for one issue. Fields the detail view left empty
// are filled from `listed`, the row the issue was opened from.
[[nodiscard]] std::expected<IssueDetail, std::string> FinishView(const Provider& provider, const Issue& listed,
                                                                 const std::string& output, std::optional<int> exitCode);

// Puts path wherever an argv element says {input-file}. An argv that
// never says it is a provider bug: the input would silently go nowhere.
[[nodiscard]] std::expected<std::vector<std::string>, std::string> SubstituteInputFile(std::vector<std::string> argv,
                                                                                       const std::string&       path);

// The completion half of an action on issue key: it worked when the
// command exited 0. what names the action in the error ("comment").
[[nodiscard]] std::expected<void, std::string> FinishAction(const std::string& key, const std::string& what, const std::string& output,
                                                            std::optional<int> exitCode);

// Capability::Transition's or ::Assign's list for issue key.
[[nodiscard]] std::expected<std::vector<Choice>, std::string> FinishChoices(const Provider& provider, Capability capability,
                                                                            const std::string& key, const std::string& output,
                                                                            std::optional<int> exitCode);

// The user whose email is email moved to the front and marked "(me)".
void PutOwnUserFirst(std::vector<Choice>& users, const std::string& email);

[[nodiscard]] std::expected<std::vector<std::string>, std::string> FinishProjectKeys(const Provider& provider, const std::string& connectionName,
                                                                                     const std::string& output, std::optional<int> exitCode);

// The connection to act on issue key through: the one a panel fetched it
// from, provided its provider can do capability.
[[nodiscard]] std::expected<std::string, std::string> ConnectionForAction(const std::string& key, Capability capability);

class Runner {
  public:
    // eventLoop must outlive this Runner.
    explicit Runner(ned::ui::EventLoop& eventLoop);

    Runner(const Runner&)            = delete;
    Runner& operator=(const Runner&) = delete;

    // Exactly one of onComplete/onError fires. A second request for a panel
    // whose fetch is still running is refused through onError.
    void RequestIssues(const std::string& panelName, std::function<void(std::vector<Issue>)> onComplete,
                       std::function<void(std::string)> onError);

    [[nodiscard]] bool IsFetching(const std::string& panelName) const;

    // One issue listed by panelName, fetched through its connection's
    // provider. A provider with no detail view completes immediately with
    // `listed` alone. Same exactly-one-callback and duplicate-refusal
    // contract as RequestIssues, per issue.
    void RequestIssue(const std::string& panelName, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                      std::function<void(std::string)> onError);
    // RequestIssue through a connection rather than a panel: refetching an
    // issue after acting on it.
    void RequestIssueOn(const std::string& connectionName, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                        std::function<void(std::string)> onError);

    // Actions on issue key through connectionName. Same exactly-one-callback
    // contract as RequestIssues; one action per issue runs at a time, and
    // a connection whose provider lacks the capability is refused through
    // onError.
    void RequestTransitions(const std::string& connectionName, const std::string& key,
                            std::function<void(std::vector<Choice>)> onComplete, std::function<void(std::string)> onError);
    // The connection's own user, recognized by :email, comes first.
    void RequestAssignees(const std::string& connectionName, const std::string& key, std::function<void(std::vector<Choice>)> onComplete,
                          std::function<void(std::string)> onError);
    void Transition(const std::string& connectionName, const std::string& key, const Choice& transition, std::function<void()> onDone,
                    std::function<void(std::string)> onError);
    void Assign(const std::string& connectionName, const std::string& key, const Choice& user, std::function<void()> onDone,
                std::function<void(std::string)> onError);
    void PostComment(const std::string& connectionName, const std::string& key, const std::string& body, std::function<void()> onDone,
                     std::function<void(std::string)> onError);
    void PostWorklog(const std::string& connectionName, const std::string& key, const Worklog& worklog, std::function<void()> onDone,
                     std::function<void(std::string)> onError);

    // What issue-key completion draws on, per connection.
    void RequestProjectKeys(const std::string& connectionName, std::function<void(std::vector<std::string>)> onComplete,
                            std::function<void(std::string)> onError);
    void RequestMine(const std::string& connectionName, std::function<void(std::vector<Issue>)> onComplete,
                     std::function<void(std::string)> onError);

  private:
    struct Resolved {
        Connection                      connection;
        std::string                     query;
        std::shared_ptr<const Provider> provider;
    };
    [[nodiscard]] std::expected<Resolved, std::string> Resolve(const std::string& panelName) const;
    // query is left empty.
    [[nodiscard]] std::expected<Resolved, std::string> ResolveConnection(const std::string& connectionName, Capability capability) const;
    [[nodiscard]] std::expected<Resolved, std::string> ResolveConnection(const std::string& connectionName) const;
    void                                               FetchIssue(const Resolved& resolved, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                                                                  std::function<void(std::string)> onError);

    // Resolves connectionName for capability, builds the command with
    // makeSpec, and runs it under runKey unless that is already running.
    void Run(const std::string& runKey, const std::string& subject, const std::string& what, const std::string& connectionName,
             Capability                                                                   capability,
             const std::function<CommandSpec(const Provider&, const Connection&)>&        makeSpec,
             std::function<void(const Provider&, const std::string&, std::optional<int>)> finish,
             const std::function<void(std::string)>&                                      onError);
    void Act(const std::string& connectionName, const std::string& key, Capability capability, const std::string& what,
             const std::function<CommandSpec(const Provider&, const Connection&)>& makeSpec, std::function<void()> onDone,
             std::function<void(std::string)> onError);

    using Finish = std::function<void(const std::string&, std::optional<int>)>;

    // Runs argv under runKey (refusing a duplicate is the caller's job) and
    // hands its output and exit code to finish on the main thread. runKey
    // is free again by the time finish runs, so finish may reuse it.
    // Returns false after logging when the command can't be spawned;
    // finish is not called then.
    bool Spawn(const std::string& runKey, const std::string& subject, const std::vector<std::string>& argv, Finish finish,
               process::StderrMode stderrMode = process::StderrMode::MergeWithStdout);

    // Spawn for a provider's command, running connection's token command
    // first when spec asks for curl credentials. runKey stays taken across
    // both commands. Failures to start or authenticate go to onError;
    // finish sees only the provider's command.
    // what names the command in a failure ("fetch", "comment"). A spec's
    // input file lives until finish has run.
    void Launch(const std::string& runKey, const std::string& subject, const std::string& what, const Connection& connection,
                const CommandSpec& spec, Finish finish, const std::function<void(std::string)>& onError);

    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<tasks::TaskProcess>> running_;
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_RUNNER_H
