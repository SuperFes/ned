#include "FileWatch.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace ned::editor {

namespace {

    std::mutex& FileWatchMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& FileWatchStorage() {
        static bool enabled = true;
        return enabled;
    }

    // How long the read loop blocks per poll() -- bounds shutdown latency.
    constexpr int kPollTimeoutMs = 500;
    // A burst of events (one save can emit several) coalesces into a single
    // callback: fire after this much quiet...
    constexpr int kDebounceQuietMs = 100;
    // ...but never later than this after the burst's first event, so a
    // sustained writer can't starve the callback indefinitely.
    constexpr std::chrono::milliseconds kDebounceCap{500};

    // file-rename-propagation follow-up: IN_MOVED_FROM joins the set purely
    // so a rename can be recognized AS a rename -- without it, inotify's
    // report of a "git mv" is an unrelated IN_DELETE and IN_CREATE with
    // nothing tying the two together.
    constexpr std::uint32_t kWatchMask =
        IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_ONLYDIR;

    // EINTR-retrying single-fd poll (ChildProcess::WaitReadable's idiom);
    // true on readable, false on timeout or error (an error here just means
    // the next read reports it).
    bool PollReadable(int fd, int timeoutMs) {
        pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLIN;
        while (true) {
            const int result = ::poll(&pfd, 1, timeoutMs);
            if (result < 0 && errno == EINTR) {
                continue;
            }
            return result > 0;
        }
    }

} // namespace

void SetFileWatchEnabled(bool enabled) {
    const std::lock_guard lock(FileWatchMutex());
    FileWatchStorage() = enabled;
}

bool FileWatchEnabled() {
    const std::lock_guard lock(FileWatchMutex());
    return FileWatchStorage();
}

FileWatcher::FileWatcher(std::function<void()> onChange, std::function<void(std::vector<FileMove>)> onMoved) : onChange_(std::move(onChange)), onMoved_(std::move(onMoved)) {
    fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
        return; // inert -- the poll-tick sweep is the sole detection mechanism
    }
    thread_ = std::jthread([this](const std::stop_token& stopToken) { ReadLoop(stopToken); });
}

FileWatcher::~FileWatcher() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool FileWatcher::Active() const {
    return fd_ >= 0;
}

void FileWatcher::SetWatchedFiles(const std::vector<std::filesystem::path>& files) {
    if (fd_ < 0) {
        return;
    }

    std::map<std::filesystem::path, std::set<std::string>> wanted;
    for (const std::filesystem::path& file : files) {
        std::error_code              ec;
        const std::filesystem::path  canonical = std::filesystem::weakly_canonical(file, ec);
        const std::filesystem::path& resolved  = ec ? file : canonical;
        if (!resolved.has_parent_path() || !resolved.has_filename()) {
            continue;
        }
        wanted[resolved.parent_path()].insert(resolved.filename().string());
    }

    const std::lock_guard lock(mutex_);
    for (auto it = dirToWd_.begin(); it != dirToWd_.end();) {
        if (wanted.contains(it->first)) {
            ++it;
            continue;
        }
        ::inotify_rm_watch(fd_, it->second);
        wdToDir_.erase(it->second);
        it = dirToWd_.erase(it);
    }
    for (const auto& [dir, basenames] : wanted) {
        if (dirToWd_.contains(dir)) {
            continue; // an unchanged dir keeps its watch; IN_IGNORED-dropped ones retry here
        }
        const int wd = ::inotify_add_watch(fd_, dir.c_str(), kWatchMask);
        if (wd < 0) {
            continue; // tolerated (missing dir, watch budget) -- poll sweep covers it
        }
        dirToWd_[dir] = wd;
        wdToDir_[wd]  = dir;
    }
    dirToBasenames_ = std::move(wanted);
}

bool FileWatcher::DrainEvents() {
    bool relevant = false;
    while (true) {
        alignas(inotify_event) char buffer[4096];
        const ssize_t               count = ::read(fd_, buffer, sizeof(buffer));
        if (count <= 0) {
            break; // EAGAIN (drained) or a real error -- either way, done for now
        }
        const std::lock_guard lock(mutex_);
        for (ssize_t offset = 0; offset < count;) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
            if ((event->mask & IN_Q_OVERFLOW) != 0) {
                relevant = true; // events were lost -- assume something watched changed
                continue;
            }
            if ((event->mask & IN_IGNORED) != 0) {
                // The kernel dropped this watch (dir deleted/unmounted) and
                // will reuse the wd number -- the mapping must go now.
                // dirToBasenames_ keeps its entry so the next resync re-adds
                // the watch if the directory reappears.
                if (const auto it = wdToDir_.find(event->wd); it != wdToDir_.end()) {
                    dirToWd_.erase(it->second);
                    wdToDir_.erase(it);
                }
                continue;
            }
            if (event->len == 0) {
                continue;
            }
            const auto dirIt = wdToDir_.find(event->wd);
            if (dirIt == wdToDir_.end()) {
                continue;
            }
            const auto namesIt = dirToBasenames_.find(dirIt->second);
            const bool watched = namesIt != dirToBasenames_.end() && namesIt->second.contains(event->name);
            if (watched) {
                relevant = true;
            }

            // A rename's two halves share a cookie. Any entry of a watched
            // directory can start a pair, not just a watched basename: the
            // events arrive either way, and the file worth following is
            // routinely one no buffer has open (a `git mv` of a header
            // nobody is editing). The IN_MOVED_TO half is matched on the
            // cookie alone, since the destination name is by definition not
            // one that was being watched.
            //
            // That width is why the consumer, not this class, decides what
            // a move MEANS: every atomic save in this codebase is a
            // write-sibling-then-rename, so ".ned-tmp -> foo.cpp" is a
            // perfectly real rename pair that must not be mistaken for one
            // (see WindowManager::HandleExternalMoves' own filter).
            if ((event->mask & IN_MOVED_FROM) != 0) {
                pendingMoves_[event->cookie] = dirIt->second / event->name;
            }
            else if ((event->mask & IN_MOVED_TO) != 0) {
                if (const auto pending = pendingMoves_.find(event->cookie); pending != pendingMoves_.end()) {
                    completedMoves_.push_back(FileMove{pending->second, dirIt->second / event->name});
                    pendingMoves_.erase(pending);
                    relevant = true;
                }
            }
        }
    }
    return relevant;
}

void FileWatcher::ReadLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        if (!PollReadable(fd_, kPollTimeoutMs)) {
            continue;
        }
        if (!DrainEvents()) {
            continue;
        }
        // Debounce: keep draining until the directory has been quiet for
        // kDebounceQuietMs (capped at kDebounceCap total), then fire once.
        const auto burstStart = std::chrono::steady_clock::now();
        while (!stopToken.stop_requested() && std::chrono::steady_clock::now() - burstStart < kDebounceCap &&
               PollReadable(fd_, kDebounceQuietMs)) {
            DrainEvents();
        }
        std::vector<FileMove> moves;
        {
            const std::lock_guard lock(mutex_);
            moves.swap(completedMoves_);
            // Anything still half-paired had its other half land outside
            // every watched directory -- a real change (onChange below
            // still fires), but not a move with a destination to name.
            pendingMoves_.clear();
        }
        if (stopToken.stop_requested()) {
            continue;
        }
        if (onMoved_ && !moves.empty()) {
            onMoved_(moves);
        }
        onChange_();
    }
}

} // namespace ned::editor
