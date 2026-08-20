//
// Async execution glue between VcsProvider (Vcs/VcsProvider.h) and a real
// subprocess -- resolves the active provider, calls its argv-building
// callback synchronously, spawns the resulting command via TaskProcess
// (Tasks/TaskProcess.h) on a background thread, and once it exits, calls
// the provider's parse callback synchronously on the exit-notification's
// own thread (the main thread, per TaskProcess's own contract -- see its
// header comment). Both provider calls happening synchronously on the main
// thread is required, not incidental: see VcsProvider.h's own header
// comment for why a Janet-backed provider's callbacks must never run off
// the main thread.
//

#ifndef NED_EDITOR_VCS_VCSRUNNER_H
#define NED_EDITOR_VCS_VCSRUNNER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/Tasks/TaskProcess.h"
#include "VcsProvider.h"

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor::vcs {

class VcsRunner {
  public:
    // eventLoop must outlive this VcsRunner, same requirement
    // TaskRunner/LspManager's own constructors document.
    explicit VcsRunner(ned::ui::EventLoop& eventLoop);
    ~VcsRunner() = default;

    VcsRunner(const VcsRunner&)            = delete;
    VcsRunner& operator=(const VcsRunner&) = delete;

    // Resolves the active provider for editor::ProjectRoot(), calls its
    // BlameArgv(path) synchronously, spawns the resulting command, and on
    // exit calls ParseBlame(stdout) synchronously before invoking
    // onComplete on the main thread. onError fires instead of onComplete
    // (never both) if buffer has no associated path, no provider resolves
    // for the current project root, the process fails to spawn or exits
    // non-zero, or either provider callback throws -- matching
    // TaskRunner/LspManager's "nothing configured/a failure isn't a crash"
    // convention: report it, don't propagate an exception to the caller.
    // A second call for the same buffer path while one is already running
    // is a no-op (onError fires immediately) -- one concurrent blame/log
    // request per file.
    void RequestBlame(const text::Buffer& buffer, std::function<void(std::vector<VcsBlameLine>)> onComplete,
                       std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestLog(const text::Buffer& buffer, std::function<void(std::vector<VcsLogEntry>)> onComplete,
                     std::function<void(std::string)> onError = [](const std::string&) {});

  private:
    // Spawns argv (keyed by key, to guard against a duplicate concurrent
    // request), collects its full combined stdout+stderr, and calls onDone
    // once it exits with (output, exitCode) -- exitCode is std::nullopt if
    // the process couldn't even be spawned or was terminated by a signal.
    void RunAndCollect(const std::string& key, const std::vector<std::string>& argv,
                        std::function<void(std::string output, std::optional<int> exitCode)> onDone);

    [[nodiscard]] bool IsRunning(const std::string& key) const;

    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<tasks::TaskProcess>> running_;
};

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSRUNNER_H
