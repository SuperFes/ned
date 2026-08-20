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
    void RequestBlame(const text::Buffer& buffer, std::function<void(std::vector<VcsBlameLine>)> onComplete, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestLog(const text::Buffer& buffer, std::function<void(std::vector<VcsLogEntry>)> onComplete, std::function<void(std::string)> onError = [](const std::string&) {});
    // Diff gutter follow-up: same shape/guarantees as RequestBlame/RequestLog
    // above, calling DiffArgv/ParseDiff instead. Meant to be called
    // repeatedly (live refresh, debounced by the caller) rather than once
    // per user action the way blame/log are -- the "already running" guard
    // above is what keeps a slow git process from piling up duplicate
    // concurrent diffs if refreshes are requested faster than one completes.
    void RequestDiff(const text::Buffer& buffer, std::function<void(std::vector<VcsDiffHunk>)> onComplete, std::function<void(std::string)> onError = [](const std::string&) {});

    // Vocabulary-completion follow-up: the status/stage/unstage/commit/
    // branch operations, same provider-resolution/duplicate-guard/error
    // conventions as the three above. Root-scoped operations (status,
    // commit, branch*) act on editor::ProjectRoot() rather than any one
    // buffer, so they take no Buffer at all; stage/unstage take the target
    // file's path directly (BufferView resolves it from the *vcs status*
    // buffer's line at point, or the active buffer's own path -- see its
    // ResolveVcsFileTarget), canonicalized here for the same
    // relative-path-vs-"-C" reason RequestBlame documents. Operations
    // with no parse half (stage/unstage/commit/branch-switch/
    // branch-create) succeed on exit code 0 alone; commit's onSuccess is
    // handed the subprocess output's first line (e.g. git's own
    // "[main abc1234] message" summary) since that's genuinely worth
    // showing, while the rest report nothing beyond success.
    void RequestStatus(std::function<void(std::vector<VcsStatusEntry>)> onComplete, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestStage(const std::filesystem::path& path, std::function<void()> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestUnstage(const std::filesystem::path& path, std::function<void()> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});

    // Hunk-staging follow-up: the whole stage/unstage-one-hunk chain in
    // one call -- raw diff first (the provider's worktree DiffArgv for a
    // stage, its StagedDiffArgv cached diff for an unstage, since the hunk
    // to pull back out of the index isn't in the worktree diff), then
    // ExtractHunkPatch (DiffPatch.h) slices the hunk covering targetLine
    // (1-indexed, counted on the diff's new side -- buffer lines for a
    // stage, index lines for an unstage), the patch is written to a
    // mkstemp temp file (FormatOnSave's exact pattern; TaskProcess has no
    // stdin, deliberately), and Stage/UnstagePatchArgv applies it, with
    // the temp file removed on success and failure alike. Two chained
    // subprocesses means two distinct running_ keys ("<op>-diff:"/
    // "<op>-apply:") -- reusing one key across the chain would make
    // RunAndCollect's erase-after-onDone destroy the second process the
    // moment the first one's completion returned. onError fires for every
    // failure, including "no unstaged/staged change at this line" when no
    // hunk covers targetLine. Both provider callbacks still only ever run
    // on the main thread, same as everywhere else here.
    void RequestHunkApply(const text::Buffer& buffer, std::size_t targetLine, bool stage, std::function<void()> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestCommit(const std::string& message, std::function<void(std::string summary)> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestBranchList(std::function<void(std::vector<VcsBranchEntry>)> onComplete, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestBranchSwitch(const std::string& name, std::function<void()> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});
    void RequestBranchCreate(const std::string& name, std::function<void()> onSuccess, std::function<void(std::string)> onError = [](const std::string&) {});

  private:
    // Spawns argv (keyed by key, to guard against a duplicate concurrent
    // request), collects its full combined stdout+stderr, and calls onDone
    // once it exits with (output, exitCode) -- exitCode is std::nullopt if
    // the process couldn't even be spawned or was terminated by a signal.
    void RunAndCollect(const std::string& key, const std::vector<std::string>& argv,
                       std::function<void(std::string output, std::optional<int> exitCode)> onDone);

    [[nodiscard]] bool IsRunning(const std::string& key) const;

    // The shared resolve-provider/guard/run/report skeleton the
    // vocabulary-completion operations all use (blame/log/diff predate it
    // and keep their own spelled-out copies -- their per-operation
    // comments carry real history not worth flattening into this).
    // buildSpec runs synchronously against the resolved provider;
    // onOutput runs on the main thread with the successful run's output
    // (parse there, or ignore it for exit-code-only operations); any
    // throw from either lands in onError.
    void RunProviderOperation(const char* operation, const std::string& key,
                              const std::function<VcsCommandSpec(VcsProvider&)>&    buildSpec,
                              std::function<void(VcsProvider&, std::string output)> onOutput,
                              std::function<void(std::string)>                      onError);

    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<tasks::TaskProcess>> running_;
};

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSRUNNER_H
