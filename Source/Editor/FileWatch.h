//
// file-watcher follow-up to AutoRevert.h/AutoMerge.h: an inotify-backed
// change trigger so an external write underneath an open buffer starts the
// existing revert/merge sweeps near-instantly instead of waiting for the
// 5s background timer tick. Trigger plumbing only -- all the actual
// revert/merge logic stays in AutoRevertBuffers/AutoMergeBuffers, and the
// poll-tick sweep is deliberately kept running as a safety net for
// anything inotify can't see (NFS, an exhausted watch budget, a dropped
// event) -- this module only lowers latency, it is never the sole
// detection mechanism.
//
// Watches the *parent directory* of each watched file, not the file
// itself: editors and tools (including ned's own ProjectReplace) save via
// write-sibling-then-rename, which invalidates a file-level inotify watch
// on the first save. Directory events are filtered down to the watched
// basenames, so sibling-file churn in a busy directory never fires the
// callback (it costs one string compare per event).
//

#ifndef NED_EDITOR_FILEWATCH_H
#define NED_EDITOR_FILEWATCH_H

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ned::editor {

// Process-wide toggle (mutex-guarded static state, mirroring AutoRevert.h's
// exact pattern), default on. Configured from Janet via ned/set-file-watch.
// When off, the consumer (WindowManager::ResyncFileWatcher) clears the watch
// set rather than tearing the watcher down, so a later re-enable takes
// effect at the next resync without restart.
void               SetFileWatchEnabled(bool enabled);
[[nodiscard]] bool FileWatchEnabled();

class FileWatcher {
  public:
    // onChange runs on the watcher's own background thread, at most once per
    // debounced event burst -- the consumer marshals onto the main thread
    // via EventLoop::Post. Inert (Active() == false, no thread started) if
    // inotify_init1 itself fails; per-directory add-watch failures are
    // likewise tolerated silently -- the poll-tick sweep covers both.
    explicit FileWatcher(std::function<void()> onChange);
    // Stops the thread (bounded by the read loop's poll timeout plus the
    // debounce cap) and joins before closing the inotify fd; no callback
    // can fire after destruction returns.
    ~FileWatcher();

    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&)                 = delete;
    FileWatcher& operator=(FileWatcher&&)      = delete;

    // Replaces the watched set wholesale (thread-safe; diffs against the
    // current set internally, so repeated calls with an unchanged buffer
    // list are cheap). Paths are weakly_canonical-normalized -- the same
    // comparison BufferList's dedupe-by-path uses -- collapsing symlink
    // aliases before any directory is watched twice.
    void SetWatchedFiles(const std::vector<std::filesystem::path>& files);

    [[nodiscard]] bool Active() const;

  private:
    void ReadLoop(const std::stop_token& stopToken);
    // Drains every event currently readable, updating watch bookkeeping;
    // returns whether any event was relevant to a watched file.
    bool DrainEvents();

    std::function<void()> onChange_;
    int                   fd_ = -1; // declared before thread_: closed only after the join

    mutable std::mutex                                     mutex_; // guards the three maps
    std::map<int, std::filesystem::path>                   wdToDir_;
    std::map<std::filesystem::path, int>                   dirToWd_;
    std::map<std::filesystem::path, std::set<std::string>> dirToBasenames_;

    std::jthread thread_; // last member: stopped/joined before anything above goes away
};

} // namespace ned::editor

#endif // NED_EDITOR_FILEWATCH_H
