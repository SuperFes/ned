#include "Dispatcher.h"

#include <iterator>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>

#include "Editor/Host/SearchCodec.h"

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
}

Dispatcher::~Dispatcher() {
    lifetime_.Revoke();
    searches_.clear();
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
    response.capabilities = Capabilities{.search = 1};
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

} // namespace ned::editor::host
