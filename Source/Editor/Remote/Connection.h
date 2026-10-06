//
// The editor's connection to a remote host's agent (Docs/ClientServerProtocol.md,
// sections 4 and 5): an ssh master, the agent EnsureAgent installed, a
// session running it, and hello. An agent whose hello reports another build
// than the one deployed is reinstalled once; a second mismatch is an error,
// never something to work around.
//
// Connect blocks, so Connector runs it on a worker and hands the result to
// the main thread, where a Connection drives the protocol.
//

#ifndef NED_EDITOR_REMOTE_CONNECTION_H
#define NED_EDITOR_REMOTE_CONNECTION_H

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

#include "Editor/Host/Hello.h"
#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/Endpoint.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Protocol/FramedConnection.h"
#include "Editor/Remote/Deploy.h"
#include "Editor/Remote/Ssh.h"

namespace ned::editor::remote {

class ConnectError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct ConnectOptions {
    std::string        destination;
    SshOptions         ssh;
    AgentDirCandidates agents = DefaultAgentDirCandidates();
};

// A connected agent before anything drives its protocol. The transport is
// declared after the master so it closes first.
struct Handshake {
    std::unique_ptr<SshMaster> master;
    protocol::FrameTransport   transport;
    host::HelloResponse        hello;
    DeployedAgent              agent;
};

// Says hello over a transport no Endpoint drives yet, and applies the codec
// it settles. Throws ConnectError when the agent errs, exits, or doesn't
// answer within timeout, and once stop is requested.
[[nodiscard]] host::HelloResponse SayHello(protocol::FrameTransport& transport, const host::HelloRequest& request,
                                           std::chrono::milliseconds timeout, const std::stop_token& stop = {});

// Logs in, deploys and says hello. Throws SshError, DeployError or
// ConnectError.
[[nodiscard]] Handshake Connect(const ConnectOptions& options, const std::stop_token& stop = {});

// A live agent connection. Main thread only, like the Endpoint it owns.
class Connection {
  public:
    Connection(Handshake handshake, PostQueue& postQueue);
    // Closes the agent session, then stops the master.
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] protocol::Endpoint&        Endpoint() noexcept;
    [[nodiscard]] const host::HelloResponse& Hello() const noexcept;
    [[nodiscard]] const DeployedAgent&       Agent() const noexcept;
    [[nodiscard]] const std::string&         Destination() const noexcept;
    [[nodiscard]] bool                       Connected() const noexcept;

    // Runs once, on the main thread, when the agent session ends. Every
    // pending request has already failed with kConnectionClosed. The
    // handler mustn't destroy the Connection; post that instead.
    void SetOnDisconnected(std::function<void(const std::string& reason)> handler);
    // Ends the session as a hang-up would, with reason as the cause.
    void Close(const std::string& reason);

  private:
    using Framed = protocol::FramedConnection<protocol::FrameTransport>;

    void Disconnect(const std::string& reason);

    std::unique_ptr<SshMaster>                     master_;
    host::HelloResponse                            hello_;
    DeployedAgent                                  agent_;
    Framed                                         framed_;
    protocol::Endpoint                             endpoint_;
    bool                                           connected_ = true;
    std::function<void(const std::string& reason)> onDisconnected_;
};

// Runs Connect on a worker. done runs once on the main thread with the
// connection or the error's message, unless the Connector is destroyed
// first; destroying it cancels the attempt and waits for the worker.
class Connector {
  public:
    using Result = std::expected<std::unique_ptr<Connection>, std::string>;

    Connector(ConnectOptions options, PostQueue& postQueue, std::function<void(Result)> done);
    ~Connector();

    Connector(const Connector&)            = delete;
    Connector& operator=(const Connector&) = delete;

    // done still runs, with the cancellation as its error.
    void Cancel();

  private:
    PostQueue&                  postQueue_;
    std::function<void(Result)> done_;
    LifetimeGuard               lifetime_;
    std::jthread                worker_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_CONNECTION_H
