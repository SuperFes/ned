#include "Hosts.h"

#include <utility>

namespace ned::editor::remote {

Hosts::Hosts(PostQueue& postQueue, OptionsFactory options) : postQueue_(postQueue), options_(std::move(options)) {
}

Hosts::~Hosts() {
    lifetime_.Revoke();
    attempts_.clear();
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
    attempt.waiters.push_back(std::move(done));
    attempt.connector = std::make_unique<Connector>(options_(destination), postQueue_,
                                                    [this, destination](Connector::Result result) { Finish(destination, std::move(result)); });
}

void Hosts::Finish(const std::string& destination, Connector::Result result) {
    auto attempt = attempts_.extract(destination);
    // This runs inside the Connector's own callback, so it goes afterwards.
    postQueue_.Post([connector = std::shared_ptr<Connector>(std::move(attempt.mapped().connector))] {});

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
    if (const auto attempt = attempts_.find(destination); attempt != attempts_.end()) {
        attempt->second.connector->Cancel();
        return true;
    }
    auto live = connections_.extract(destination);
    if (live.empty()) {
        return false;
    }
    live.mapped()->SetOnDisconnected({});
    live.mapped()->Close("disconnected from " + destination);
    return true;
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

} // namespace ned::editor::remote
