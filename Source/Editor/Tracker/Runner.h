//
// Fetches a panel's issues: resolves panel -> connection -> provider,
// builds argv and parses output on the main thread, and runs the command
// through TaskProcess in between.
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

  private:
    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<tasks::TaskProcess>> running_;
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_RUNNER_H
