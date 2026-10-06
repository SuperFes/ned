#include "Connection.h"

#include <optional>
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
    // More than a login banner, less than a runaway script.
    constexpr std::size_t kMaxPreambleBytes{64 * 1024};

    // Runs the first of its arguments, <build>-<arch>, that matches this host
    // and is installed. An argument to `sh -c`, so it stays on one line and
    // free of backslashes for a non-POSIX login shell's quoting to pass
    // through untouched; hence printf, then echo for the newline.
    constexpr std::string_view kStartScript =
        R"(r="${XDG_DATA_HOME:-$HOME/.local/share}/ned/agent"; a=$(uname -m); for d in "$@"; do if [ "$d" = "${d%-*}-$a" ] && [ -x "$r/$d/ned-agent" ]; then )"
        R"(printf 'ned-agent-stdio-1 %s' "$r/$d/ned-agent"; echo; exec "$r/$d/ned-agent" --stdio; fi; done; echo ned-agent-missing-1)";

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

std::vector<std::string> AgentSessionCommand(std::span<const std::string> directories) {
    std::vector<std::string> argv{"sh", "-c", std::string(kStartScript), "sh"};
    argv.insert(argv.end(), directories.begin(), directories.end());
    return argv;
}

std::optional<std::filesystem::path> AwaitAgentStart(protocol::FrameTransport& transport, std::chrono::milliseconds timeout, const std::stop_token& stop) {
    const std::string started  = std::string(kAgentStartMarker) + " ";
    const std::string missing  = std::string(kAgentMissingMarker) + "\n";
    const auto        deadline = std::chrono::steady_clock::now() + timeout;
    std::string       preamble;
    while (true) {
        while (!transport.WaitReadable(std::chrono::milliseconds(kHelloPollMs))) {
            if (stop.stop_requested()) {
                throw ConnectError("cancelled waiting for the agent to start");
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw ConnectError("the agent didn't start within " + std::to_string(timeout.count()) + " ms");
            }
        }
        std::string chunk;
        try {
            chunk = transport.ReadUnframed();
        }
        catch (const std::runtime_error&) {
            // Ends the session as EOF would.
        }
        if (chunk.empty()) {
            throw ConnectError(ExitReason(transport));
        }
        preamble += chunk;
        if (const auto at = preamble.find(started); at != std::string::npos) {
            const auto path = at + started.size();
            if (const auto end = preamble.find('\n', path); end != std::string::npos) {
                return preamble.substr(path, end - path);
            }
        }
        else if (preamble.find(missing) != std::string::npos) {
            return std::nullopt;
        }
        if (preamble.size() > kMaxPreambleBytes) {
            throw ConnectError("the host printed more than " + std::to_string(kMaxPreambleBytes / 1024) +
                               " KiB before the agent started; does a login script write to stdout? It began: " + preamble.substr(0, preamble.find('\n')).substr(0, 80));
        }
    }
}

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

    const std::vector<std::string> directories = LocalAgentDirectories(options.agents);
    const host::HelloRequest       request{.codecs = {std::string(host::kCodecZstd), std::string(host::kCodecNone)}, .window = protocol::kDefaultWindow};
    std::optional<DeployedAgent>   installed;
    bool                           reinstalled = false;
    while (true) {
        protocol::FrameTransport             transport(master->SessionArgv(AgentSessionCommand(directories)), true);
        std::optional<std::filesystem::path> started;
        host::HelloResponse                  hello;
        try {
            started = AwaitAgentStart(transport, ProtocolRequestTimeoutMs(), stop);
            if (started) {
                hello = SayHello(transport, request, ProtocolRequestTimeoutMs(), stop);
            }
        }
        catch (const ConnectError& e) {
            throw ConnectError(options.destination + ": " + e.what());
        }
        if (!started) {
            if (installed) {
                throw ConnectError("the agent installed on " + options.destination + " at " + installed->remotePath.string() + " isn't found to start");
            }
            installed = EnsureAgent(*master, options.agents, false, stop);
            continue;
        }

        // <build>-<arch>
        const std::string directory = started->parent_path().filename().string();
        const auto        dash      = directory.rfind('-');
        DeployedAgent     agent{.remotePath = *started,
                                .build      = directory.substr(0, dash),
                                .arch       = dash == std::string::npos ? std::string() : directory.substr(dash + 1),
                                .uploaded   = installed && installed->uploaded};
        if (hello.build == agent.build) {
            if (hello.major != request.major) {
                throw ConnectError("the agent on " + options.destination + " speaks protocol " + std::to_string(hello.major) + ", the editor " +
                                   std::to_string(request.major));
            }
            return Handshake{.master = std::move(master), .transport = std::move(transport), .hello = std::move(hello), .agent = std::move(agent)};
        }
        if (reinstalled) {
            throw ConnectError("the agent on " + options.destination + " still reports build " + hello.build + " after reinstalling");
        }
        transport.Close();
        installed   = EnsureAgent(*master, options.agents, true, stop);
        reinstalled = true;
    }
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
