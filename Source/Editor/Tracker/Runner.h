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

  private:
    struct Resolved {
        Connection                      connection;
        std::string                     query;
        std::shared_ptr<const Provider> provider;
    };
    [[nodiscard]] std::expected<Resolved, std::string> Resolve(const std::string& panelName) const;

    // Runs argv under runKey (refusing a duplicate is the caller's job) and
    // hands its merged output and exit code to finish on the main thread.
    // Returns false after logging when the command can't be spawned;
    // finish is not called then.
    bool Spawn(const std::string& runKey, const std::string& subject, const std::vector<std::string>& argv,
               std::function<void(const std::string&, std::optional<int>)> finish);

    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<tasks::TaskProcess>> running_;
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_RUNNER_H
