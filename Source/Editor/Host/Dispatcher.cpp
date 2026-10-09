#include "Dispatcher.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>

#include "Editor/Host/Codec.h"
#include "Editor/Host/Fs.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/Host/ProcCodec.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/Host/WatchCodec.h"
#include "Editor/Process/Pty.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Tree.h"

namespace ned::editor::host {

using Json = nlohmann::json;
using protocol::Responder;
namespace error_code = protocol::error_code;

namespace {

    // A cancelled request stops its worker.
    struct StopWorker {
        std::stop_source source;
        void             operator()() noexcept {
            source.request_stop();
        }
    };

} // namespace

struct Dispatcher::SearchJob {
    explicit SearchJob(Responder responderIn) : responder(std::move(responderIn)) {
    }

    Responder responder;
    // Matches held while the peer's credit is spent, so a slow link gets
    // fuller chunks rather than a queue of small ones.
    std::vector<SearchMatch>                      pending;
    std::jthread                                  worker;
    std::optional<std::stop_callback<StopWorker>> forwardStop;
};

// A child spawned for the editor. Its readers hand each chunk to the main
// thread and wait to be let go on until the editor's credit has taken it;
// its writer feeds stdin in the order the editor asked.
struct Dispatcher::ProcJob {
    enum class Step { Write,
                      CloseInput,
                      Release };
    struct Item {
        Step        step = Step::Write;
        std::string data;
    };

    ProcJob(Responder responderIn, process::ChildProcess childIn, bool capturesStderr, bool onPtyIn) :
        responder(std::move(responderIn)), stderrDone(!capturesStderr), child(std::move(childIn)), onPty(onPtyIn) {
    }

    ~ProcJob() {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        child.CloseConnection();
        writer       = std::jthread();
        stdoutReader = std::jthread();
        stderrReader = std::jthread();
    }

    void Permit(int fd) {
        {
            const std::lock_guard lock(mutex);
            (fd == 1 ? stdoutMayRead : stderrMayRead) = true;
        }
        wake.notify_all();
    }

    // False once the job is stopping.
    bool AwaitPermit(int fd) {
        std::unique_lock lock(mutex);
        bool&            mayRead = fd == 1 ? stdoutMayRead : stderrMayRead;
        wake.wait(lock, [&] { return mayRead || stopping; });
        mayRead = false;
        return !stopping;
    }

    void Push(Item item) {
        {
            const std::lock_guard lock(mutex);
            items.push_back(std::move(item));
        }
        wake.notify_all();
    }

    // Main thread only.
    Responder                                                responder;
    std::deque<Responder>                                    writeAnswers;
    std::optional<std::optional<int>>                        exit;
    bool                                                     stderrDone;
    bool                                                     released   = false;
    bool                                                     waiting[3] = {false, false, false}; // by fd: a chunk held for credit
    std::optional<std::stop_callback<std::function<void()>>> onCancel;

    process::ChildProcess   child;
    const bool              onPty;
    std::mutex              mutex;
    std::condition_variable wake;
    bool                    stdoutMayRead = false;
    bool                    stderrMayRead = false;
    bool                    stopping      = false;
    bool                    exited        = false;
    std::deque<Item>        items;

    std::jthread writer;
    std::jthread stdoutReader;
    std::jthread stderrReader;
};

struct Dispatcher::WorkerJob {
    explicit WorkerJob(Responder responderIn) : responder(std::move(responderIn)) {
    }

    Responder    responder;
    std::jthread worker;
};

Dispatcher::Dispatcher(protocol::Endpoint& endpoint, PostQueue& postQueue, Identity identity,
                       std::function<void(std::string_view codec)> onCodec) : endpoint_(endpoint), postQueue_(postQueue), identity_(std::move(identity)), onCodec_(std::move(onCodec)) {
    // Bound, so a request arriving after this object is gone is answered
    // InternalError by its dropped Responder rather than reaching freed state.
    endpoint_.Handle(std::string(kHello), lifetime_.Bind([this](Json params, Responder responder) {
        Hello(params, std::move(responder));
    }));
    endpoint_.Handle(std::string(kSearchProject), lifetime_.Bind([this](Json params, Responder responder) {
        SearchProject(params, std::move(responder));
    }));
    endpoint_.Handle(std::string(kFsStat), lifetime_.Bind([this](Json params, Responder responder) { FsStat(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsRead), lifetime_.Bind([this](Json params, Responder responder) { FsRead(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsWrite), lifetime_.Bind([this](Json params, Responder responder) { FsWrite(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsReaddir), lifetime_.Bind([this](Json params, Responder responder) { FsReaddir(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsRealpath), lifetime_.Bind([this](Json params, Responder responder) { FsRealpath(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsMkdir), lifetime_.Bind([this](Json params, Responder responder) { FsMkdir(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsRename), lifetime_.Bind([this](Json params, Responder responder) { FsRename(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsRemove), lifetime_.Bind([this](Json params, Responder responder) { FsRemove(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kFsProjectRoot),
                     lifetime_.Bind([this](Json params, Responder responder) { FsProjectRoot(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kWatchSet), lifetime_.Bind([this](Json params, Responder responder) { WatchSet(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcSpawn), lifetime_.Bind([this](Json params, Responder responder) { ProcSpawn(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcWrite), lifetime_.Bind([this](Json params, Responder responder) { ProcWrite(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcCloseStdin),
                     lifetime_.Bind([this](Json params, Responder responder) { ProcCloseStdin(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcSignal), lifetime_.Bind([this](Json params, Responder responder) { ProcSignal(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcResize), lifetime_.Bind([this](Json params, Responder responder) { ProcResize(params, std::move(responder)); }));
    endpoint_.Handle(std::string(kProcRelease), lifetime_.Bind([this](Json params, Responder responder) { ProcRelease(params, std::move(responder)); }));
}

Dispatcher::~Dispatcher() {
    lifetime_.Revoke();
    watcher_.reset();
    // Killed together rather than each given its grace in turn.
    for (auto& [handle, job] : procs_) {
        job->onCancel.reset();
        job->child.Signal(SIGKILL);
    }
    procs_.clear();
    searches_.clear();
    workerJobs_.clear();
}

void Dispatcher::Hello(const Json& params, Responder responder) {
    HelloRequest request;
    try {
        request = HelloRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    // A major mismatch is the editor's to act on; it needs this answer to
    // see one.
    endpoint_.SetPeerWindow(request.window);
    HelloResponse response;
    response.build        = identity_.build;
    response.arch         = identity_.arch;
    response.codec        = ChooseCodec(request.codecs);
    response.window       = protocol::kDefaultWindow;
    response.capabilities = Capabilities{.fs = 5, .proc = kProcVersion, .search = 1, .watch = 2};
    responder.Result(ToJson(response));
    if (onCodec_) {
        onCodec_(response.codec);
    }
}

void Dispatcher::SearchProject(const Json& params, Responder responder) {
    std::optional<SearchRequest> request;
    std::optional<SearchPattern> pattern;
    try {
        request.emplace(SearchRequestFromJson(params));
        pattern.emplace(request->pattern);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    catch (const SearchPatternError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }

    const std::uint64_t id  = nextJob_++;
    auto                job = std::make_unique<SearchJob>(std::move(responder));
    job->responder.OnDrained(lifetime_.Bind([this, id] {
        if (const auto it = searches_.find(id); it != searches_.end()) {
            SendPending(*it->second);
        }
    }));
    job->worker = std::jthread([this, id, alive = lifetime_.Token(), root = std::move(request->root), pattern = std::move(*pattern),
                                live = std::move(request->live), options = request->options](const std::stop_token& stop) {
        std::string error;
        try {
            StreamSearchDirectory(
                root, pattern, live, options,
                [&](std::vector<SearchMatch> run) {
                    postQueue_.Post(alive.Bind([this, id, run = std::move(run)]() mutable { SearchMatches(id, std::move(run)); }));
                },
                stop);
        }
        catch (const std::exception& e) {
            error = e.what();
        }
        // Posted after every run, so it arrives after them.
        postQueue_.Post(alive.Bind([this, id, error = std::move(error)] { FinishSearch(id, error); }));
    });
    job->forwardStop.emplace(job->responder.StopToken(), StopWorker{job->worker.get_stop_source()});
    searches_.emplace(id, std::move(job));
}

void Dispatcher::SearchMatches(std::uint64_t job, std::vector<SearchMatch> matches) {
    const auto it = searches_.find(job);
    if (it == searches_.end()) {
        return;
    }
    std::vector<SearchMatch>& pending = it->second->pending;
    pending.insert(pending.end(), std::make_move_iterator(matches.begin()), std::make_move_iterator(matches.end()));
    SendPending(*it->second);
}

void Dispatcher::SendPending(SearchJob& job) {
    if (job.pending.empty() || job.responder.Backlog() > 0) {
        return;
    }
    for (const std::span<const SearchMatch> chunk : ChunkMatches(job.pending)) {
        job.responder.Chunk(MatchesToJson(chunk));
    }
    job.pending.clear();
}

void Dispatcher::FinishSearch(std::uint64_t job, const std::string& error) {
    const auto it = searches_.find(job);
    if (it == searches_.end()) {
        return;
    }
    // Destroyed at return, joining a worker that has already posted this.
    const std::unique_ptr<SearchJob> finished = std::move(it->second);
    searches_.erase(it);

    Responder& responder = finished->responder;
    if (!error.empty()) {
        responder.Error(error_code::kInternalError, error);
        return;
    }
    for (const std::span<const SearchMatch> chunk : ChunkMatches(finished->pending)) {
        responder.Chunk(MatchesToJson(chunk));
    }
    responder.Result(Json::object());
}

void Dispatcher::RunOnWorker(Responder responder, std::function<WorkerAnswer()> work) {
    const std::uint64_t id  = nextJob_++;
    auto                job = std::make_unique<WorkerJob>(std::move(responder));
    job->worker             = std::jthread([this, id, alive = lifetime_.Token(), work = std::move(work)] {
        WorkerAnswer answer = work();
        postQueue_.Post(alive.Bind([this, id, answer = std::move(answer)] {
            if (const auto it = workerJobs_.find(id); it != workerJobs_.end()) {
                answer(it->second->responder);
                workerJobs_.erase(it);
            }
        }));
    });
    workerJobs_.emplace(id, std::move(job));
}

void Dispatcher::FsStat(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = PathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    responder.Result(ToJson(StatPath(path)));
}

void Dispatcher::FsRead(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = PathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    RunOnWorker(std::move(responder), [path = std::move(path)]() -> WorkerAnswer {
        try {
            auto     contents = std::make_shared<const text::FileContents>(ReadPath(path));
            FileStat stat     = StatPath(path);
            stat.size         = contents->bytes.size();
            stat.timestamp    = contents->timestamp;
            return [contents, stat](Responder& answer) {
                // Queued past the peer's credit, so the window paces them.
                const std::string_view bytes = contents->bytes;
                for (std::size_t at = 0; at < bytes.size(); at += kFsChunkBytes) {
                    answer.Chunk(Bytes(bytes.substr(at, kFsChunkBytes)));
                }
                answer.Result(ToJson(stat));
            };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::FsWrite(const Json& params, Responder responder) {
    std::shared_ptr<const FsWriteRequest> request;
    try {
        request = std::make_shared<const FsWriteRequest>(FsWriteRequestFromJson(params));
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    RunOnWorker(std::move(responder), [request]() -> WorkerAnswer {
        try {
            return [stat = WritePath(*request)](Responder& answer) { answer.Result(ToJson(stat)); };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::FsReaddir(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = PathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    RunOnWorker(std::move(responder), [path = std::move(path)]() -> WorkerAnswer {
        try {
            auto entries = std::make_shared<const std::vector<DirEntry>>(ListPath(path));
            return [entries](Responder& answer) {
                const std::span<const DirEntry> all(*entries);
                for (std::size_t at = 0; at < all.size(); at += kFsReaddirChunk) {
                    answer.Chunk(DirEntriesToJson(all.subspan(at, std::min(kFsReaddirChunk, all.size() - at))));
                }
                answer.Result(Json::object());
            };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::FsRealpath(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = RealpathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    try {
        responder.Result(Json{{"path", PathToJson(RealPath(path))}});
    }
    catch (const FsError& e) {
        responder.Error(e.Code(), e.what());
    }
}

void Dispatcher::FsProjectRoot(const Json& params, Responder responder) {
    FsProjectRootRequest request;
    try {
        request = FsProjectRootRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    std::optional<std::filesystem::path> root;
    if (request.detect) {
        // Canonical, as DetectProjectRoot's answer is.
        std::error_code       ec;
        std::filesystem::path start = std::filesystem::weakly_canonical(request.path, ec);
        root                        = FindMarkerRoot(ec ? request.path : start, request.markers);
    }
    responder.Result(Json{{"path", PathToJson(root ? *root : DetectProjectRoot(request.path, request.detect))}});
}

void Dispatcher::FsMkdir(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = PathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    RunOnWorker(std::move(responder), [path = std::move(path)]() -> WorkerAnswer {
        try {
            return [stat = MakeDirectory(path)](Responder& answer) { answer.Result(ToJson(stat)); };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::FsRename(const Json& params, Responder responder) {
    FsRenameRequest request;
    try {
        request = FsRenameRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    RunOnWorker(std::move(responder), [request = std::move(request)]() -> WorkerAnswer {
        try {
            RenamePath(request);
            return [](Responder& answer) { answer.Result(Json::object()); };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::FsRemove(const Json& params, Responder responder) {
    std::filesystem::path path;
    try {
        path = PathParamsFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    // A big tree takes a while; a worker keeps the connection answering.
    RunOnWorker(std::move(responder), [path = std::move(path)]() -> WorkerAnswer {
        try {
            RemovePath(path);
            return [](Responder& answer) { answer.Result(Json::object()); };
        }
        catch (const FsError& e) {
            return [code = e.Code(), message = std::string(e.what())](Responder& answer) { answer.Error(code, message); };
        }
    });
}

void Dispatcher::WatchSet(const Json& params, Responder responder) {
    WatchSetRequest request;
    try {
        request = WatchSetRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    if (!watcher_) {
        if (request.files.empty() && request.directories.empty() && request.trees.empty()) {
            responder.Result(Json::object());
            return;
        }
        auto report =
            lifetime_.Bind([this](const std::vector<FileEvent>& burst, const std::vector<FileMove>& moves) { ReportWatchEvents(burst, moves); });
        watcher_ = std::make_unique<FileWatcher>(
            [this, report] {
                postQueue_.Post([report, burst = std::exchange(watchBurst_, {}), moves = std::exchange(watchMoves_, {})] mutable { report(burst, moves); });
            },
            [this](std::vector<FileMove> moves) { watchMoves_ = std::move(moves); },
            [this](std::vector<FileEvent> events) { watchBurst_ = std::move(events); });
        watcher_->SetReportsDirectoryEntries(true);
    }
    watchRequest_ = std::move(request);
    ApplyWatchSet();
    responder.Result(Json::object());
}

void Dispatcher::ApplyWatchSet() {
    const auto canonical = [](const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
        return ec ? path : resolved;
    };
    watchedFiles_.clear();
    watchedDirectories_.clear();
    for (const std::filesystem::path& file : watchRequest_.files) {
        watchedFiles_[canonical(file)].push_back(file);
    }
    std::vector<std::filesystem::path> named = watchRequest_.directories;
    for (const std::filesystem::path& tree : watchRequest_.trees) {
        named.push_back(tree);
        for (ProjectTreeEntry& entry : BuildProjectTree(tree)) {
            if (entry.isDirectory) {
                named.push_back(std::move(entry.path));
            }
        }
    }
    // The watcher canonicalizes files itself but takes directories as given.
    std::vector<std::filesystem::path> directories;
    for (const std::filesystem::path& directory : named) {
        auto [it, inserted] = watchedDirectories_.try_emplace(canonical(directory));
        if (std::ranges::find(it->second, directory) == it->second.end()) {
            it->second.push_back(directory);
        }
        if (inserted) {
            directories.push_back(it->first);
        }
    }
    watcher_->SetWatchedFiles(watchRequest_.files, directories);
}

void Dispatcher::ReportWatchEvents(const std::vector<FileEvent>& burst, const std::vector<FileMove>& moves) {
    if (watchedFiles_.empty() && watchedDirectories_.empty()) {
        return;
    }
    // A path as watch/set named it: through each name of a watched file,
    // and of the watched directory holding it.
    const auto names = [this](const std::filesystem::path& path) {
        std::vector<std::filesystem::path> aliases;
        if (const auto file = watchedFiles_.find(path); file != watchedFiles_.end()) {
            aliases.insert(aliases.end(), file->second.begin(), file->second.end());
        }
        if (const auto directory = watchedDirectories_.find(path.parent_path()); directory != watchedDirectories_.end()) {
            for (const std::filesystem::path& alias : directory->second) {
                aliases.push_back(alias / path.filename());
            }
        }
        return aliases;
    };
    // A file also inside a watched directory is reported once.
    std::map<std::filesystem::path, FileEvent::Kind> named;
    for (const FileEvent& event : burst) {
        for (const std::filesystem::path& alias : names(event.path)) {
            named.emplace(alias, event.kind);
        }
    }
    WatchEvents report{.events = {}, .moves = {}, .overflow = burst.empty() && moves.empty()};
    for (auto& [path, kind] : named) {
        report.events.push_back(FileEvent{.path = path, .kind = kind});
    }
    // Only a move out of something watched; it lands wherever it went.
    for (const FileMove& move : moves) {
        const std::vector<std::filesystem::path> to = names(move.to);
        for (const std::filesystem::path& from : names(move.from)) {
            if (std::ranges::none_of(report.moves, [&from](const FileMove& known) { return known.from == from; })) {
                report.moves.push_back(FileMove{.from = from, .to = to.empty() ? move.to : to.front()});
            }
        }
    }
    // A directory made inside a tree is walked into, so its entries are watched too.
    if (!watchRequest_.trees.empty() && std::ranges::any_of(report.events, [](const FileEvent& event) {
            std::error_code ec;
            return event.kind == FileEvent::Kind::Created && std::filesystem::is_directory(event.path, ec);
        })) {
        ApplyWatchSet();
    }
    if (report.events.empty() && report.moves.empty() && !report.overflow) {
        return;
    }
    endpoint_.Notify(std::string(kWatchEvent), ToJson(report));
}

namespace {

    // What one output chunk carries at most; a reader gathers what's ready
    // up to this before handing it on.
    constexpr std::size_t kProcChunkBytes = 64 * 1024;
    // How long a released child gets to exit on its stdin's EOF.
    constexpr std::chrono::milliseconds kReleaseGrace{200};

} // namespace

void Dispatcher::ProcSpawn(const Json& params, Responder responder) {
    ProcSpawnRequest request;
    try {
        request = ProcSpawnRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    if (procs_.contains(request.handle)) {
        responder.Error(error_code::kInvalidParams, "ned: process handle " + std::to_string(request.handle) + " is in use");
        return;
    }
    std::optional<process::ChildProcess> child;
    try {
        const process::SpawnOptions options{.stderrMode = request.stderrMode, .cwd = request.cwd, .env = request.env};
        if (request.pty) {
            child.emplace(process::SpawnOnPty(request.argv, *request.pty, options));
        }
        else {
            child.emplace(request.argv, options);
        }
    }
    catch (const std::exception& e) {
        responder.Error(proc_error::kSpawnFailed, e.what());
        return;
    }

    const bool    capturesStderr = request.stderrMode == process::StderrMode::Capture;
    auto          job            = std::make_unique<ProcJob>(std::move(responder), std::move(*child), capturesStderr, request.pty.has_value());
    ProcJob*      raw            = job.get();
    const auto    handle         = request.handle;
    LifetimeToken alive          = lifetime_.Token();

    job->responder.OnDrained(alive.Bind([raw] {
        for (const int fd : {1, 2}) {
            if (raw->waiting[fd]) {
                raw->waiting[fd] = false;
                raw->Permit(fd);
            }
        }
    }));
    job->onCancel.emplace(job->responder.StopToken(), std::function<void()>([raw] { raw->child.Signal(SIGKILL); }));

    const auto read = [this, raw, handle, alive](int fd) {
        while (true) {
            std::string data = fd == 1 ? raw->child.ReadSome() : raw->child.ReadSomeStderr();
            if (data.empty()) {
                break;
            }
            while (fd == 1 && data.size() < kProcChunkBytes) {
                const std::optional<std::string> more = raw->child.ReadSome(std::chrono::milliseconds(0));
                if (!more || more->empty()) {
                    break; // nothing ready, or EOF: the next read says which
                }
                data += *more;
            }
            postQueue_.Post(alive.Bind([this, raw, handle, fd, data = std::move(data)] mutable { OnProcOutput(handle, raw, fd, std::move(data)); }));
            if (!raw->AwaitPermit(fd)) {
                return;
            }
        }
        if (fd == 1) {
            const std::optional<int> exit = raw->child.WaitForExit();
            postQueue_.Post(alive.Bind([this, raw, handle, exit] { OnProcExited(handle, raw, exit); }));
        }
        else {
            postQueue_.Post(alive.Bind([this, raw, handle] { OnProcStderrDone(handle, raw); }));
        }
    };
    job->stdoutReader = std::jthread([read] { read(1); });
    if (capturesStderr) {
        job->stderrReader = std::jthread([read] { read(2); });
    }
    job->writer = std::jthread([this, raw, handle, alive] {
        while (true) {
            ProcJob::Item item;
            {
                std::unique_lock lock(raw->mutex);
                raw->wake.wait(lock, [raw] { return raw->stopping || !raw->items.empty(); });
                if (raw->stopping) {
                    return;
                }
                item = std::move(raw->items.front());
                raw->items.pop_front();
            }
            std::string error;
            switch (item.step) {
                case ProcJob::Step::Write:
                    try {
                        raw->child.WriteAll(item.data, ProtocolWriteStallTimeoutMs());
                    }
                    catch (const std::exception& e) {
                        error = e.what();
                    }
                    break;
                case ProcJob::Step::CloseInput:
                    raw->child.CloseInput();
                    break;
                case ProcJob::Step::Release: {
                    if (raw->onPty) {
                        raw->child.Signal(SIGHUP);
                    }
                    else {
                        raw->child.CloseInput();
                    }
                    std::unique_lock lock(raw->mutex);
                    if (!raw->wake.wait_for(lock, kReleaseGrace, [raw] { return raw->stopping || raw->exited; })) {
                        lock.unlock();
                        raw->child.Signal(SIGKILL);
                    }
                    continue;
                }
            }
            postQueue_.Post(alive.Bind([this, raw, handle, error = std::move(error)] mutable { OnProcWritten(handle, raw, std::move(error)); }));
        }
    });
    procs_.emplace(handle, std::move(job));
}

Dispatcher::ProcJob* Dispatcher::FindProc(const Json& params, Responder& responder) {
    std::uint64_t handle = 0;
    try {
        handle = ProcHandleFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return nullptr;
    }
    const auto it = procs_.find(handle);
    if (it == procs_.end()) {
        responder.Error(proc_error::kNoSuchProcess, "ned: no process " + std::to_string(handle));
        return nullptr;
    }
    return it->second.get();
}

void Dispatcher::ProcWrite(const Json& params, Responder responder) {
    ProcWriteRequest request;
    try {
        request = ProcWriteRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    ProcJob* job = FindProc(params, responder);
    if (job == nullptr) {
        return;
    }
    job->writeAnswers.push_back(std::move(responder));
    job->Push(ProcJob::Item{.step = ProcJob::Step::Write, .data = std::move(request.data)});
}

void Dispatcher::ProcCloseStdin(const Json& params, Responder responder) {
    ProcJob* job = FindProc(params, responder);
    if (job == nullptr) {
        return;
    }
    job->writeAnswers.push_back(std::move(responder));
    job->Push(ProcJob::Item{.step = ProcJob::Step::CloseInput, .data = {}});
}

void Dispatcher::ProcSignal(const Json& params, Responder responder) {
    ProcSignalRequest request;
    try {
        request = ProcSignalRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    ProcJob* job = FindProc(params, responder);
    if (job == nullptr) {
        return;
    }
    job->child.Signal(request.signal);
    responder.Result(Json::object());
}

void Dispatcher::ProcResize(const Json& params, Responder responder) {
    ProcResizeRequest request;
    try {
        request = ProcResizeRequestFromJson(params);
    }
    catch (const protocol::ProtocolError& e) {
        responder.Error(error_code::kInvalidParams, e.what());
        return;
    }
    ProcJob* job = FindProc(params, responder);
    if (job == nullptr) {
        return;
    }
    job->child.ResizeTerminal(request.size);
    responder.Result(Json::object());
}

void Dispatcher::ProcRelease(const Json& params, Responder responder) {
    ProcJob* job = FindProc(params, responder);
    if (job == nullptr) {
        return;
    }
    // Its output has nowhere to go now: let every held chunk through.
    job->released = true;
    for (const int fd : {1, 2}) {
        if (job->waiting[fd]) {
            job->waiting[fd] = false;
            job->Permit(fd);
        }
    }
    job->Push(ProcJob::Item{.step = ProcJob::Step::Release, .data = {}});
    responder.Result(Json::object());
}

void Dispatcher::OnProcOutput(std::uint64_t handle, const ProcJob* job, int fd, std::string data) {
    const auto it = procs_.find(handle);
    if (it == procs_.end() || it->second.get() != job) {
        return;
    }
    ProcJob& proc = *it->second;
    if (!proc.released) {
        proc.responder.Chunk(ToJson(host::ProcOutput{.fd = fd, .data = std::move(data)}));
    }
    if (proc.released || proc.responder.Backlog() == 0) {
        proc.Permit(fd);
    }
    else {
        proc.waiting[fd] = true;
    }
}

void Dispatcher::OnProcWritten(std::uint64_t handle, const ProcJob* job, std::string error) {
    const auto it = procs_.find(handle);
    if (it == procs_.end() || it->second.get() != job || it->second->writeAnswers.empty()) {
        return;
    }
    Responder answer = std::move(it->second->writeAnswers.front());
    it->second->writeAnswers.pop_front();
    if (error.empty()) {
        answer.Result(Json::object());
    }
    else {
        answer.Error(proc_error::kWriteFailed, error);
    }
}

void Dispatcher::OnProcExited(std::uint64_t handle, const ProcJob* job, std::optional<int> exit) {
    const auto it = procs_.find(handle);
    if (it == procs_.end() || it->second.get() != job) {
        return;
    }
    it->second->exit = exit;
    {
        const std::lock_guard lock(it->second->mutex);
        it->second->exited = true;
    }
    it->second->wake.notify_all();
    FinishProcIfDone(handle);
}

void Dispatcher::OnProcStderrDone(std::uint64_t handle, const ProcJob* job) {
    const auto it = procs_.find(handle);
    if (it == procs_.end() || it->second.get() != job) {
        return;
    }
    it->second->stderrDone = true;
    FinishProcIfDone(handle);
}

void Dispatcher::FinishProcIfDone(std::uint64_t handle) {
    const auto it  = procs_.find(handle);
    ProcJob&   job = *it->second;
    if (!job.exit || !job.stderrDone) {
        return;
    }
    job.onCancel.reset();
    job.responder.Result(ProcExitToJson(*job.exit));
    for (Responder& answer : job.writeAnswers) {
        answer.Error(proc_error::kWriteFailed, "ned: the process has ended");
    }
    procs_.erase(it);
}

} // namespace ned::editor::host
