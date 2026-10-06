//
// The host protocol's message layer (Docs/ClientServerProtocol.md, section
// 3): request/response correlation, streamed chunks under credit-based flow
// control, and cancellation. Both ends run one; nothing here knows what a
// host is, or how messages travel. Outgoing messages go to a sink and
// incoming ones arrive through Receive, so an in-process pair and a framed
// connection drive the same code.
//
//   request       {id, method, params}
//   response      {id, result} | {id, error: {code, message, data?}}
//   notification  {method, params}
//   $/chunk       {id, seq, data}     before the response that ends the stream
//   $/credit      {id, n}             receiver -> sender
//   $/cancel      {id}
//
// Every request gets exactly one response. A handler that drops its
// Responder unanswered sends InternalError; a cancel answers Cancelled at
// once and stops the handler's work through its stop token.
//
// Single-threaded: every call, and every callback, runs on the owner's
// thread. A Responder may be carried to a worker, but only for its stop
// token; answering happens back on the owner's thread.
//

#ifndef NED_EDITOR_PROTOCOL_ENDPOINT_H
#define NED_EDITOR_PROTOCOL_ENDPOINT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>

#include <nlohmann/json.hpp>

namespace ned::editor::protocol {

namespace error_code {
    inline constexpr int kInvalidRequest = -32600;
    inline constexpr int kMethodNotFound = -32601;
    inline constexpr int kInvalidParams  = -32602;
    inline constexpr int kInternalError  = -32603;
    inline constexpr int kCancelled      = -32800;
    // Reported locally when the connection ends first; never on the wire.
    inline constexpr int kConnectionClosed = -32099;
} // namespace error_code

// Chunks a receiver lets a stream send ahead before granting more.
inline constexpr std::size_t kDefaultWindow = 16;

// A message that breaks the protocol. The owner ends the connection: the
// peer is either broken or hostile.
class ProtocolError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct RemoteError {
    int            code{};
    std::string    message;
    nlohmann::json data;
};

struct ResponseHandlers {
    std::function<void(nlohmann::json result)> onResult;
    std::function<void(RemoteError error)>     onError;
    // Each $/chunk's data, in order, before the result.
    std::function<void(nlohmann::json data)> onChunk;
};

namespace detail {
    struct EndpointCore;
} // namespace detail

// One incoming request's answer. Move-only; answering twice, or after the
// endpoint is gone or the request was cancelled, does nothing.
class Responder {
  public:
    Responder(Responder&&) noexcept;
    Responder& operator=(Responder&&) noexcept;
    Responder(const Responder&)            = delete;
    Responder& operator=(const Responder&) = delete;
    // Answers InternalError if nothing else did.
    ~Responder();

    // Sends one stream chunk, or queues it until the peer grants credit.
    void Chunk(nlohmann::json data);
    // Ends the request. Queued chunks go first.
    void Result(nlohmann::json result);
    void Error(int code, std::string message, nlohmann::json data = nullptr);

    // Chunks waiting on credit, for a producer that wants to slow down.
    [[nodiscard]] std::size_t Backlog() const;
    // Runs each time the peer's credit clears a backlog, for a producer that
    // holds work back while Backlog() is nonzero.
    void OnDrained(std::function<void()> callback);
    // Set when the request is cancelled or the connection closes.
    [[nodiscard]] std::stop_token StopToken() const;
    [[nodiscard]] bool            Finished() const;

  private:
    friend struct detail::EndpointCore;
    Responder(std::weak_ptr<detail::EndpointCore> core, std::uint64_t id, std::stop_token stop);

    void Answer(nlohmann::json response);

    std::weak_ptr<detail::EndpointCore> core_;
    std::uint64_t                       id_{};
    std::stop_token                     stop_;
    bool                                answered_ = false;
};

class Endpoint {
  public:
    using Sink                = std::function<void(nlohmann::json message)>;
    using RequestHandler      = std::function<void(nlohmann::json params, Responder responder)>;
    using NotificationHandler = std::function<void(nlohmann::json params)>;

    // window is how many chunks this side accepts ahead per stream; the
    // peer learns it from hello.
    explicit Endpoint(Sink sink, std::size_t window = kDefaultWindow);
    // Stops every incoming request's work. Pending outgoing requests are
    // dropped without calling their handlers.
    ~Endpoint();

    Endpoint(const Endpoint&)            = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    // How many chunks the peer accepts ahead per stream.
    void SetPeerWindow(std::size_t window);
    // Numbers later requests past id, which something else already sent on
    // this connection: a handshake made before this endpoint took over.
    void ReserveRequestIds(std::uint64_t id);

    // Throws ProtocolError for a message that breaks the protocol: wrong
    // shape, an unknown response id, or a chunk out of sequence. Credit is
    // granted as chunks are consumed here, so it bounds a cooperating
    // sender; it can't detect one that ignores it.
    void Receive(nlohmann::json message);

    // Returns the request's id, for Cancel.
    std::uint64_t Request(std::string method, nlohmann::json params, ResponseHandlers handlers);
    void          Notify(std::string method, nlohmann::json params);
    // Asks the peer to stop. Chunks after this are dropped; the one
    // response still arrives, as Cancelled or a partial result.
    void Cancel(std::uint64_t id);

    void Handle(std::string method, RequestHandler handler);
    void HandleNotification(std::string method, NotificationHandler handler);

    // The connection is gone: every pending request fails with
    // kConnectionClosed, every incoming one is stopped, and nothing more is
    // sent.
    void Close(const std::string& reason);

    [[nodiscard]] std::size_t PendingRequests() const;

  private:
    std::shared_ptr<detail::EndpointCore> core_;
};

} // namespace ned::editor::protocol

#endif // NED_EDITOR_PROTOCOL_ENDPOINT_H
