#include "Dispatcher.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>

#include "Editor/Host/Codec.h"
#include "Editor/Host/Fs.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/Host/WatchCodec.h"

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
    endpoint_.Handle(std::string(kWatchSet), lifetime_.Bind([this](Json params, Responder responder) { WatchSet(params, std::move(responder)); }));
}

Dispatcher::~Dispatcher() {
    lifetime_.Revoke();
    watcher_.reset();
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
    response.capabilities = Capabilities{.fs = 3, .search = 1, .watch = 1};
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
        if (request.files.empty() && request.directories.empty()) {
            responder.Result(Json::object());
            return;
        }
        auto report = lifetime_.Bind([this](const std::vector<FileEvent>& burst) { ReportWatchEvents(burst); });
        watcher_    = std::make_unique<FileWatcher>(
            [this, report] {
                postQueue_.Post([report, burst = std::exchange(watchBurst_, {})] mutable { report(burst); });
            },
            nullptr, [this](std::vector<FileEvent> events) { watchBurst_ = std::move(events); });
        watcher_->SetReportsDirectoryEntries(true);
    }

    const auto canonical = [](const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
        return ec ? path : resolved;
    };
    watchedFiles_.clear();
    watchedDirectories_.clear();
    for (const std::filesystem::path& file : request.files) {
        watchedFiles_[canonical(file)].push_back(file);
    }
    // The watcher canonicalizes files itself but takes directories as given.
    std::vector<std::filesystem::path> directories;
    for (const std::filesystem::path& directory : request.directories) {
        auto [it, inserted] = watchedDirectories_.try_emplace(canonical(directory));
        it->second.push_back(directory);
        if (inserted) {
            directories.push_back(it->first);
        }
    }
    watcher_->SetWatchedFiles(request.files, directories);
    responder.Result(Json::object());
}

void Dispatcher::ReportWatchEvents(const std::vector<FileEvent>& burst) {
    if (watchedFiles_.empty() && watchedDirectories_.empty()) {
        return;
    }
    // A file also inside a watched directory is reported once.
    std::map<std::filesystem::path, FileEvent::Kind> named;
    for (const FileEvent& event : burst) {
        if (const auto file = watchedFiles_.find(event.path); file != watchedFiles_.end()) {
            for (const std::filesystem::path& alias : file->second) {
                named.emplace(alias, event.kind);
            }
        }
        if (const auto directory = watchedDirectories_.find(event.path.parent_path()); directory != watchedDirectories_.end()) {
            for (const std::filesystem::path& alias : directory->second) {
                named.emplace(alias / event.path.filename(), event.kind);
            }
        }
    }
    WatchEvents report{.events = {}, .overflow = burst.empty()};
    for (auto& [path, kind] : named) {
        report.events.push_back(FileEvent{.path = path, .kind = kind});
    }
    if (report.events.empty() && !report.overflow) {
        return;
    }
    endpoint_.Notify(std::string(kWatchEvent), ToJson(report));
}

} // namespace ned::editor::host
