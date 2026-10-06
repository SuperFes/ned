#include "Endpoint.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace ned::editor::protocol {

using Json = nlohmann::json;

namespace {

    constexpr std::string_view kChunk  = "$/chunk";
    constexpr std::string_view kCredit = "$/credit";
    constexpr std::string_view kCancel = "$/cancel";

    std::optional<std::uint64_t> AsUnsigned(const Json& value) {
        if (value.is_number_unsigned()) {
            return value.get<std::uint64_t>();
        }
        if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
            return static_cast<std::uint64_t>(value.get<std::int64_t>());
        }
        return std::nullopt;
    }

    std::uint64_t RequiredUnsigned(const Json& object, const char* field) {
        const auto it = object.find(field);
        if (it == object.end()) {
            throw ProtocolError(std::string("ned: protocol message lacks '") + field + "'");
        }
        const std::optional<std::uint64_t> value = AsUnsigned(*it);
        if (!value) {
            throw ProtocolError(std::string("ned: protocol field '") + field + "' is not an unsigned integer");
        }
        return *value;
    }

    Json ErrorResponse(std::uint64_t id, int code, std::string message, Json data = nullptr) {
        Json error{{"code", code}, {"message", std::move(message)}};
        if (!data.is_null()) {
            error["data"] = std::move(data);
        }
        return Json{{"id", id}, {"error", std::move(error)}};
    }

} // namespace

namespace detail {

    struct Outgoing {
        ResponseHandlers handlers;
        bool             cancelled = false;
        std::uint64_t    nextSeq   = 0;
        std::size_t      consumed  = 0; // chunks received since the last grant
    };

    struct Incoming {
        std::stop_source    stop;
        std::size_t         credit{};
        std::uint64_t       nextSeq = 0;
        std::deque<Json>    queued;
        std::optional<Json>   response; // held until queued finishes draining
        std::function<void()> onDrained;
    };

    struct EndpointCore : std::enable_shared_from_this<EndpointCore> {
        EndpointCore(Endpoint::Sink sinkIn, std::size_t windowIn) : sink(std::move(sinkIn)), window(std::max<std::size_t>(windowIn, 1)) {
        }

        Endpoint::Sink sink;
        std::size_t    window;
        std::size_t    peerWindow = kDefaultWindow;
        std::uint64_t  nextId     = 1;
        std::uint64_t  lastPeerId = 0;
        bool           closed     = false;

        std::unordered_map<std::uint64_t, Outgoing>                    outgoing;
        std::unordered_map<std::uint64_t, Incoming>                    incoming;
        std::unordered_map<std::string, Endpoint::RequestHandler>      requestHandlers;
        std::unordered_map<std::string, Endpoint::NotificationHandler> notificationHandlers;

        void Send(Json message) {
            if (!closed) {
                sink(std::move(message));
            }
        }

        void SendChunk(std::uint64_t id, Incoming& entry, Json data) {
            Send(Json{{"method", kChunk}, {"params", {{"id", id}, {"seq", entry.nextSeq++}, {"data", std::move(data)}}}});
            --entry.credit;
        }

        void Chunk(std::uint64_t id, Json data) {
            const auto it = incoming.find(id);
            if (it == incoming.end()) {
                return;
            }
            if (it->second.credit > 0 && it->second.queued.empty()) {
                SendChunk(id, it->second, std::move(data));
            }
            else {
                it->second.queued.push_back(std::move(data));
            }
        }

        void Answer(std::uint64_t id, Json response) {
            const auto it = incoming.find(id);
            if (it == incoming.end()) {
                return;
            }
            if (it->second.queued.empty()) {
                incoming.erase(it);
                Send(std::move(response));
            }
            else {
                it->second.response = std::move(response);
            }
        }

        // Sends what credit allows, then the held response once nothing is
        // left ahead of it, or else tells the producer its backlog cleared.
        void Flush(std::uint64_t id) {
            const auto it = incoming.find(id);
            if (it == incoming.end()) {
                return;
            }
            Incoming&  entry      = it->second;
            const bool hadBacklog = !entry.queued.empty();
            while (entry.credit > 0 && !entry.queued.empty()) {
                Json data = std::move(entry.queued.front());
                entry.queued.pop_front();
                SendChunk(id, entry, std::move(data));
            }
            if (!entry.queued.empty()) {
                return;
            }
            if (entry.response) {
                Json response = std::move(*entry.response);
                incoming.erase(it);
                Send(std::move(response));
            }
            else if (hadBacklog && entry.onDrained) {
                // A copy: the callback may answer, erasing the entry.
                const std::function<void()> onDrained = entry.onDrained;
                onDrained();
            }
        }

        void Receive(Json message) {
            if (!message.is_object()) {
                throw ProtocolError("ned: protocol message is not a map");
            }
            const auto method = message.find("method");
            if (method != message.end()) {
                if (!method->is_string()) {
                    throw ProtocolError("ned: protocol method is not a string");
                }
                std::string name   = method->get<std::string>();
                Json        params = message.contains("params") ? std::move(message["params"]) : Json();
                if (message.contains("id")) {
                    ReceiveRequest(RequiredUnsigned(message, "id"), name, std::move(params));
                }
                else {
                    ReceiveNotification(name, std::move(params));
                }
                return;
            }
            if (message.contains("id")) {
                ReceiveResponse(RequiredUnsigned(message, "id"), std::move(message));
                return;
            }
            throw ProtocolError("ned: protocol message is neither a request, a response nor a notification");
        }

        void ReceiveRequest(std::uint64_t id, const std::string& method, Json params) {
            // Ids only grow, so this also catches one reused.
            if (id <= lastPeerId) {
                throw ProtocolError("ned: protocol request id did not increase");
            }
            lastPeerId = id;

            const auto handler = requestHandlers.find(method);
            if (handler == requestHandlers.end()) {
                Send(ErrorResponse(id, error_code::kMethodNotFound, "unknown method: " + method));
                return;
            }
            Incoming& entry = incoming[id];
            entry.credit    = peerWindow;
            Responder responder(weak_from_this(), id, entry.stop.get_token());
            // A copy: the handler may replace itself.
            const Endpoint::RequestHandler run = handler->second;
            try {
                run(std::move(params), std::move(responder));
            }
            catch (const std::exception& e) {
                Answer(id, ErrorResponse(id, error_code::kInternalError, e.what()));
            }
        }

        void ReceiveNotification(const std::string& method, Json params) {
            if (method == kChunk) {
                ReceiveChunk(std::move(params));
                return;
            }
            if (method == kCredit) {
                const std::uint64_t id = RequiredUnsigned(params, "id");
                const std::uint64_t n  = RequiredUnsigned(params, "n");
                const auto          it = incoming.find(id);
                if (it != incoming.end()) { // a grant can cross the final response in flight
                    it->second.credit = n > std::numeric_limits<std::size_t>::max() - it->second.credit
                                            ? std::numeric_limits<std::size_t>::max()
                                            : it->second.credit + n;
                    Flush(id);
                }
                return;
            }
            if (method == kCancel) {
                const std::uint64_t id = RequiredUnsigned(params, "id");
                const auto          it = incoming.find(id);
                if (it != incoming.end()) { // as can a cancel
                    it->second.stop.request_stop();
                    incoming.erase(it);
                    Send(ErrorResponse(id, error_code::kCancelled, "cancelled"));
                }
                return;
            }
            const auto handler = notificationHandlers.find(method);
            if (handler != notificationHandlers.end()) {
                const Endpoint::NotificationHandler run = handler->second;
                run(std::move(params));
            }
        }

        void ReceiveChunk(Json params) {
            if (!params.is_object()) {
                throw ProtocolError("ned: protocol chunk params are not a map");
            }
            const std::uint64_t id  = RequiredUnsigned(params, "id");
            const std::uint64_t seq = RequiredUnsigned(params, "seq");
            const auto          it  = outgoing.find(id);
            if (it == outgoing.end()) {
                throw ProtocolError("ned: protocol chunk for an unknown request");
            }
            Outgoing& entry = it->second;
            if (seq != entry.nextSeq) {
                throw ProtocolError("ned: protocol chunk out of sequence");
            }
            ++entry.nextSeq;
            ++entry.consumed;
            if (!entry.cancelled && entry.handlers.onChunk) {
                const auto onChunk = entry.handlers.onChunk;
                onChunk(params.contains("data") ? std::move(params["data"]) : Json());
            }

            // The handler may have closed the endpoint or cancelled.
            const auto again = outgoing.find(id);
            if (again == outgoing.end() || again->second.cancelled) {
                return;
            }
            if (again->second.consumed >= std::max<std::size_t>(window / 2, 1)) {
                const std::size_t n    = again->second.consumed;
                again->second.consumed = 0;
                Send(Json{{"method", kCredit}, {"params", {{"id", id}, {"n", n}}}});
            }
        }

        void ReceiveResponse(std::uint64_t id, Json message) {
            const auto it = outgoing.find(id);
            if (it == outgoing.end()) {
                throw ProtocolError("ned: protocol response for an unknown request");
            }
            const ResponseHandlers handlers = std::move(it->second.handlers);
            outgoing.erase(it);

            if (message.contains("result")) {
                if (handlers.onResult) {
                    handlers.onResult(std::move(message["result"]));
                }
                return;
            }
            const auto error = message.find("error");
            if (error == message.end() || !error->is_object() || !error->contains("code") || !(*error)["code"].is_number_integer() ||
                !error->contains("message") || !(*error)["message"].is_string()) {
                throw ProtocolError("ned: protocol response has neither a result nor a well-formed error");
            }
            if (handlers.onError) {
                handlers.onError(RemoteError{(*error)["code"].get<int>(), (*error)["message"].get<std::string>(),
                                             error->contains("data") ? (*error)["data"] : Json()});
            }
        }

        void StopIncoming() {
            for (auto& [id, entry] : incoming) {
                entry.stop.request_stop();
            }
            incoming.clear();
        }
    };

} // namespace detail

Responder::Responder(std::weak_ptr<detail::EndpointCore> core, std::uint64_t id, std::stop_token stop) : core_(std::move(core)), id_(id), stop_(std::move(stop)) {
}

Responder::Responder(Responder&& other) noexcept : core_(std::move(other.core_)), id_(other.id_), stop_(std::move(other.stop_)), answered_(std::exchange(other.answered_, true)) {
}

Responder& Responder::operator=(Responder&& other) noexcept {
    if (this != &other) {
        if (!answered_) {
            try {
                Error(error_code::kInternalError, "request dropped without a response");
            }
            catch (...) { // NOLINT(bugprone-empty-catch) the connection is already failing
            }
        }
        core_     = std::move(other.core_);
        id_       = other.id_;
        stop_     = std::move(other.stop_);
        answered_ = std::exchange(other.answered_, true);
    }
    return *this;
}

Responder::~Responder() {
    if (!answered_) {
        try {
            Error(error_code::kInternalError, "request dropped without a response");
        }
        catch (...) { // NOLINT(bugprone-empty-catch) the connection is already failing
        }
    }
}

void Responder::Chunk(Json data) {
    if (answered_) {
        return;
    }
    if (const auto core = core_.lock()) {
        core->Chunk(id_, std::move(data));
    }
}

void Responder::Result(Json result) {
    Answer(Json{{"id", id_}, {"result", std::move(result)}});
}

void Responder::Error(int code, std::string message, Json data) {
    Answer(ErrorResponse(id_, code, std::move(message), std::move(data)));
}

void Responder::Answer(Json response) {
    if (answered_) {
        return;
    }
    answered_ = true;
    if (const auto core = core_.lock()) {
        core->Answer(id_, std::move(response));
    }
}

std::size_t Responder::Backlog() const {
    const auto core = core_.lock();
    if (!core) {
        return 0;
    }
    const auto it = core->incoming.find(id_);
    return it == core->incoming.end() ? 0 : it->second.queued.size();
}

void Responder::OnDrained(std::function<void()> callback) {
    const auto core = core_.lock();
    if (!core || answered_) {
        return;
    }
    if (const auto it = core->incoming.find(id_); it != core->incoming.end()) {
        it->second.onDrained = std::move(callback);
    }
}

std::stop_token Responder::StopToken() const {
    return stop_;
}

bool Responder::Finished() const {
    if (answered_) {
        return true;
    }
    const auto core = core_.lock();
    return !core || !core->incoming.contains(id_);
}

Endpoint::Endpoint(Sink sink, std::size_t window) : core_(std::make_shared<detail::EndpointCore>(std::move(sink), window)) {
}

Endpoint::~Endpoint() {
    core_->closed = true;
    core_->StopIncoming();
    core_->outgoing.clear();
}

void Endpoint::SetPeerWindow(std::size_t window) {
    core_->peerWindow = std::max<std::size_t>(window, 1);
}

void Endpoint::Receive(Json message) {
    // Held so a callback that destroys this Endpoint doesn't pull the core
    // out from under the rest of the dispatch.
    const std::shared_ptr<detail::EndpointCore> core = core_;
    if (!core->closed) {
        core->Receive(std::move(message));
    }
}

std::uint64_t Endpoint::Request(std::string method, Json params, ResponseHandlers handlers) {
    const std::uint64_t id = core_->nextId++;
    if (core_->closed) {
        if (handlers.onError) {
            handlers.onError(RemoteError{error_code::kConnectionClosed, "connection closed", nullptr});
        }
        return id;
    }
    core_->outgoing.emplace(id, detail::Outgoing{std::move(handlers), false, 0, 0});
    core_->Send(Json{{"id", id}, {"method", std::move(method)}, {"params", std::move(params)}});
    return id;
}

void Endpoint::Notify(std::string method, Json params) {
    core_->Send(Json{{"method", std::move(method)}, {"params", std::move(params)}});
}

void Endpoint::Cancel(std::uint64_t id) {
    const auto it = core_->outgoing.find(id);
    if (it == core_->outgoing.end() || it->second.cancelled) {
        return;
    }
    it->second.cancelled = true;
    core_->Send(Json{{"method", kCancel}, {"params", {{"id", id}}}});
}

void Endpoint::Handle(std::string method, RequestHandler handler) {
    core_->requestHandlers[std::move(method)] = std::move(handler);
}

void Endpoint::HandleNotification(std::string method, NotificationHandler handler) {
    core_->notificationHandlers[std::move(method)] = std::move(handler);
}

void Endpoint::Close(const std::string& reason) {
    const std::shared_ptr<detail::EndpointCore> core = core_;
    if (core->closed) {
        return;
    }
    core->closed = true;
    core->StopIncoming();
    auto pending = std::move(core->outgoing);
    core->outgoing.clear();
    for (auto& [id, entry] : pending) {
        if (entry.handlers.onError) {
            entry.handlers.onError(RemoteError{error_code::kConnectionClosed, reason, nullptr});
        }
    }
}

std::size_t Endpoint::PendingRequests() const {
    return core_->outgoing.size();
}

} // namespace ned::editor::protocol
