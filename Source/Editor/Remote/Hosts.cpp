#include "Hosts.h"

#include <utility>

namespace ned::editor::remote {

Hosts::Hosts(PostQueue& postQueue, OptionsFactory options, HostLookupOptions lookup) : postQueue_(postQueue), options_(std::move(options)), lookup_(postQueue, std::move(lookup)) {
}

Hosts::~Hosts() {
    lifetime_.Revoke();
    attempts_.clear();
    cancelled_.clear();
    connections_.clear();
}

ConnectOptions Hosts::DefaultOptions(const std::string& destination) {
    return ConnectOptions{.destination = destination, .ssh = {}, .agents = DefaultAgentDirCandidates()};
}

void Hosts::Connect(const std::string& destination, Connected done) {
    if (Connection* live = Find(destination)) {
        done(live);
        return;
    }
    if (const auto attempt = attempts_.find(destination); attempt != attempts_.end()) {
        attempt->second.waiters.push_back(std::move(done));
        return;
    }
    Attempt& attempt = attempts_[destination];
    attempt.id       = ++nextAttemptId_;
    attempt.waiters.push_back(std::move(done));
    attempt.connector = std::make_unique<Connector>(options_(destination), postQueue_, [this, destination, id = attempt.id](Connector::Result result) {
        Finish(destination, id, std::move(result));
    });
    NoteChanged(destination);
}

void Hosts::Finish(const std::string& destination, std::uint64_t id, Connector::Result result) {
    // This runs inside the Connector's own callback, so it goes afterwards.
    const auto retire = [this](std::unique_ptr<Connector> connector) { postQueue_.Post([connector = std::shared_ptr<Connector>(std::move(connector))] {}); };
    if (auto cancelled = cancelled_.extract(id); !cancelled.empty()) {
        // Its callers already have the cancellation; a connection that got
        // this far closes along with result.
        retire(std::move(cancelled.mapped()));
        return;
    }
    auto attempt = attempts_.extract(destination);
    retire(std::move(attempt.mapped().connector));
    NoteChanged(destination);

    if (!result) {
        for (Connected& waiter : attempt.mapped().waiters) {
            waiter(std::unexpected(result.error()));
        }
        return;
    }
    Connection* connection = result->get();
    connection->SetOnDisconnected([this, destination, connection](const std::string& reason) {
        postQueue_.Post(lifetime_.Bind([this, destination, connection] {
            if (const auto live = connections_.find(destination); live != connections_.end() && live->second.get() == connection) {
                connections_.erase(live);
                NoteChanged(destination);
            }
        }));
        if (onDropped_) {
            onDropped_(destination, reason);
        }
    });
    connections_[destination] = std::move(*result);
    for (Connected& waiter : attempt.mapped().waiters) {
        waiter(connection);
    }
}

bool Hosts::Disconnect(const std::string& destination) {
    if (auto attempt = attempts_.extract(destination); !attempt.empty()) {
        attempt.mapped().connector->Cancel();
        cancelled_.emplace(attempt.mapped().id, std::move(attempt.mapped().connector));
        for (Connected& waiter : attempt.mapped().waiters) {
            waiter(std::unexpected(std::string("cancelled")));
        }
        NoteChanged(destination);
        return true;
    }
    auto live = connections_.extract(destination);
    if (live.empty()) {
        return false;
    }
    live.mapped()->SetOnDisconnected({});
    live.mapped()->Close("disconnected from " + destination);
    NoteChanged(destination);
    return true;
}

void Hosts::ExpireIdleRequests() {
    // Looked up afresh each time, as a failed request's handler may end a
    // connection.
    for (const std::string& destination : Destinations()) {
        if (Connection* connection = Find(destination)) {
            connection->Endpoint().ExpireIdle();
        }
    }
}

Connection* Hosts::Find(const std::string& destination) const {
    const auto live = connections_.find(destination);
    return live != connections_.end() && live->second->Connected() ? live->second.get() : nullptr;
}

bool Hosts::Connecting(const std::string& destination) const {
    return attempts_.contains(destination);
}

std::vector<std::string> Hosts::Destinations() const {
    std::vector<std::string> destinations;
    for (const auto& [destination, connection] : connections_) {
        if (connection->Connected()) {
            destinations.push_back(destination);
        }
    }
    return destinations;
}

void Hosts::SetOnDropped(Dropped handler) {
    onDropped_ = std::move(handler);
}

void Hosts::SetOnChanged(Changed handler) {
    onChanged_ = std::move(handler);
}

void Hosts::NoteChanged(const std::string& destination) {
    postQueue_.Post(lifetime_.Bind([this, destination] {
        if (onChanged_) {
            onChanged_(destination);
        }
    }));
}

HostLookup& Hosts::Lookup() noexcept {
    return lookup_;
}

} // namespace ned::editor::remote
