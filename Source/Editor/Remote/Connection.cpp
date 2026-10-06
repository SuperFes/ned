#include "Connection.h"

#include <utility>
#include <vector>

#include "Editor/ProcessTimeouts.h"
#include "Editor/Protocol/Cbor.h"

namespace ned::editor::remote {

using Json = nlohmann::json;

namespace {

    // The Connection's Endpoint numbers its requests past this one.
    constexpr std::uint64_t kHelloId     = 1;
    constexpr int           kHelloPollMs = 50;
    constexpr std::size_t   kMaxStderrBytes{16 * 1024};

    // What an agent that exited said on its way out.
    std::string ExitReason(const protocol::FrameTransport& transport) {
        std::string errors;
        while (errors.size() < kMaxStderrBytes) {
            const std::string chunk = transport.ReadStderr();
            if (chunk.empty()) {
                break;
            }
            errors += chunk;
        }
        const auto first = errors.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "the agent exited";
        }
        return errors.substr(first, errors.find('\n', first) - first);
    }

} // namespace

host::HelloResponse SayHello(protocol::FrameTransport& transport, const host::HelloRequest& request, std::chrono::milliseconds timeout,
                             const std::stop_token& stop) {
    try {
        transport.WriteFrame(protocol::EncodeCbor(Json{{"id", kHelloId}, {"method", host::kHello}, {"params", host::ToJson(request)}}));
    }
    catch (const std::runtime_error&) {
        throw ConnectError(ExitReason(transport));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!transport.WaitReadable(std::chrono::milliseconds(kHelloPollMs))) {
        if (stop.stop_requested()) {
            throw ConnectError("cancelled waiting for the agent's hello");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw ConnectError("the agent didn't answer hello within " + std::to_string(timeout.count()) + " ms");
        }
    }

    Json answer;
    try {
        const std::optional<std::string> frame = transport.ReadFrame();
        if (!frame) {
            throw ConnectError(ExitReason(transport));
        }
        answer = protocol::DecodeCbor(*frame);
    }
    catch (const ConnectError&) {
        throw;
    }
    catch (const std::exception& e) {
        throw ConnectError(std::string("the agent's answer to hello is unreadable: ") + e.what());
    }
    if (!answer.is_object() || answer.value("id", Json()) != kHelloId) {
        throw ConnectError("the agent answered something other than hello");
    }
    if (const auto error = answer.find("error"); error != answer.end()) {
        throw ConnectError("the agent refused hello: " + error->value("message", std::string("no message")));
    }
    host::HelloResponse hello;
    try {
        hello = host::HelloResponseFromJson(answer.value("result", Json()));
    }
    catch (const protocol::ProtocolError& e) {
        throw ConnectError(std::string("the agent's hello is malformed: ") + e.what());
    }
    transport.SetCompression(hello.codec == host::kCodecZstd);
    return hello;
}

Handshake Connect(const ConnectOptions& options, const std::stop_token& stop) {
    auto master = std::make_unique<SshMaster>(options.destination, options.ssh);
    master->Start(stop);

    std::string reported;
    for (const bool reinstall : {false, true}) {
        DeployedAgent            agent = EnsureAgent(*master, options.agents, reinstall, stop);
        const host::HelloRequest request{.codecs = {std::string(host::kCodecZstd), std::string(host::kCodecNone)}, .window = protocol::kDefaultWindow};
        protocol::FrameTransport transport(master->SessionArgv(std::vector<std::string>{agent.remotePath.string(), "--stdio"}), true);
        host::HelloResponse      hello;
        try {
            hello = SayHello(transport, request, ProtocolRequestTimeoutMs(), stop);
        }
        catch (const ConnectError& e) {
            throw ConnectError(options.destination + ": " + e.what());
        }
        if (hello.build == agent.build) {
            if (hello.major != request.major) {
                throw ConnectError("the agent on " + options.destination + " speaks protocol " + std::to_string(hello.major) + ", the editor " +
                                   std::to_string(request.major));
            }
            return Handshake{.master = std::move(master), .transport = std::move(transport), .hello = std::move(hello), .agent = std::move(agent)};
        }
        reported = hello.build;
    }
    throw ConnectError("the agent on " + options.destination + " still reports build " + reported + " after reinstalling");
}

Connection::Connection(Handshake handshake, PostQueue& postQueue) : master_(std::move(handshake.master)), hello_(std::move(handshake.hello)), agent_(std::move(handshake.agent)),
                                                                    framed_(std::move(handshake.transport), postQueue, Framed::Options{.logCategory = LogCategory::General, .entityNoun = "agent"}),
                                                                    endpoint_([this](const Json& message) { framed_.SendFrame(protocol::EncodeCbor(message)); }) {
    endpoint_.SetPeerWindow(hello_.window);
    endpoint_.ReserveRequestIds(kHelloId);
    framed_.SetOnFrame([this](const std::string& frame) {
        try {
            endpoint_.Receive(protocol::DecodeCbor(frame));
        }
        catch (const std::exception& e) {
            // A peer that breaks the protocol is either broken or hostile.
            framed_.Transport().Close();
            Disconnect(e.what());
        }
    });
    framed_.SetOnDisconnected([this](const std::string& reason) { Disconnect(reason); });
}

Connection::~Connection() = default;

protocol::Endpoint& Connection::Endpoint() noexcept {
    return endpoint_;
}

const host::HelloResponse& Connection::Hello() const noexcept {
    return hello_;
}

const DeployedAgent& Connection::Agent() const noexcept {
    return agent_;
}

const std::string& Connection::Destination() const noexcept {
    return master_->Destination();
}

bool Connection::Connected() const noexcept {
    return connected_;
}

void Connection::SetOnDisconnected(std::function<void(const std::string& reason)> handler) {
    onDisconnected_ = std::move(handler);
}

void Connection::Close(const std::string& reason) {
    framed_.Transport().Close();
    Disconnect(reason);
}

void Connection::Disconnect(const std::string& reason) {
    if (!connected_) {
        return;
    }
    connected_ = false;
    endpoint_.Close(reason);
    if (onDisconnected_) {
        onDisconnected_(reason);
    }
}

Connector::Connector(ConnectOptions options, PostQueue& postQueue, std::function<void(Result)> done) : postQueue_(postQueue), done_(std::move(done)) {
    worker_ = std::jthread([this, options = std::move(options), token = lifetime_.Token()](const std::stop_token& stop) {
        // Shared, since a posted callback must be copyable; whatever the
        // main thread doesn't take closes with the last copy.
        auto        handshake = std::make_shared<std::optional<Handshake>>();
        std::string error;
        try {
            handshake->emplace(Connect(options, stop));
        }
        catch (const std::exception& e) {
            error = e.what();
        }
        postQueue_.Post(token.Bind([this, handshake, error = std::move(error)] {
            if (!*handshake) {
                done_(std::unexpected(error));
                return;
            }
            done_(std::make_unique<Connection>(std::move(**handshake), postQueue_));
        }));
    });
}

Connector::~Connector() {
    lifetime_.Revoke();
    worker_ = std::jthread();
}

void Connector::Cancel() {
    worker_.request_stop();
}

} // namespace ned::editor::remote
