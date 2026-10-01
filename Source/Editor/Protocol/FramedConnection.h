//
// one-connection-class follow-up. Lsp::Client, Dap::Client and Acp::Client
// each hand-rolled an identical connection machine: a background jthread
// blocking-read loop marshaling frames onto the main thread via
// ned::ui::EventLoop::Post, a second jthread draining the server/agent/
// adapter's stderr, a third jthread draining an async write queue, a
// shared_ptr<bool> alive_ use-after-free guard, and a teardown order that,
// gotten wrong, hangs rather than failing to compile -- this class exists so
// that order lives in exactly one place, ~FramedConnection, instead of being
// re-derived per protocol.
//
// This is the machinery only -- no JSON-RPC, no seq/type envelope, no
// request/response correlation. Each of Lsp::Client/Dap::Client/Acp::Client
// owns one of these as a member and keeps its own envelope/dispatch/
// handshake logic untouched; SetOnFrame's callback is where a Client hands a
// raw frame string to its own DispatchFrame. TransportT is a template
// parameter, not a virtual interface, matching this codebase's own "static
// dispatch by default" convention (CLAUDE.md) -- the concrete transport type
// (lsp::Transport, shared verbatim by LSP and DAP, or acp::Transport) is
// always known at compile time, and owning it by value (rather than a
// type-erased "read/write a frame" callable closing over an externally-owned
// transport) is what keeps the destruction-order invariant above enforced by
// this class's own member layout rather than by whoever else might hold a
// transport alongside a connection.
//
// TransportT must provide:
//   - a constructor from (const std::vector<std::string>& argv, bool captureStderr)
//   - a constructor from (TransportT&&) -- moved into place by the
//     test/pre-built-transport constructor below
//   - std::optional<std::string> ReadFrame(std::chrono::milliseconds stallTimeout) const
//   - void WriteFrame(std::string_view payload, std::chrono::milliseconds stallTimeout) const
//   - int StderrFd() const noexcept
//   - std::string ReadStderr() const -- empty at EOF or once closed
//   - void Close() noexcept -- wakes every read in progress without
//     destroying the transport
//   - const std::string& ProcessLabel() const noexcept
// -- exactly lsp::Transport's existing surface, which dap::Client already
// reuses verbatim; acp::Transport is renamed (ReadMessage/WriteMessage ->
// ReadFrame/WriteFrame, this class's own follow-up) to satisfy the same
// shape rather than being wrapped.
//
// Threading/lifetime contract is unchanged from what Lsp::Client/Dap::Client/
// Acp::Client each documented individually: every public method here, and
// the onFrame_/onDisconnected_ callbacks, only ever run on the main thread.
// alive_ is flipped false as the first statement of ~FramedConnection() and
// captured by value (a second owning reference) in every Post lambda below;
// a lambda that finds it false no-ops without touching `this` or any member
// at all, which is what makes it safe regardless of how a background
// thread's already-posted callback races this object's destruction (see
// lsp-use-after-free follow-up, the bug this pattern was built to fix).
//

#ifndef NED_EDITOR_PROTOCOL_FRAMEDCONNECTION_H
#define NED_EDITOR_PROTOCOL_FRAMEDCONNECTION_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Editor/DiagnosticsLog.h"
#include "UI/EventLoop.h"

namespace ned::editor::protocol {

template <typename TransportT>
class FramedConnection {
  public:
    struct Options {
        LogCategory logCategory{};
        // "server"/"adapter"/"agent" -- formatted into "<noun> exited (EOF)"
        // for the disconnect reason string.
        std::string_view entityNoun;
        // acp-stderr-severity follow-up: a real agent's stderr is routinely
        // just informational startup chatter, so Acp passes Info here;
        // Lsp/Dap keep the original Warning default.
        LogSeverity stderrSeverity = LogSeverity::Warning;
    };

    // Spawns argv as a new subprocess. eventLoop must outlive this object,
    // matching every Client this replaces.
    FramedConnection(std::vector<std::string> argv, bool captureStderr, ned::ui::EventLoop& eventLoop, Options options)
        : options_(std::move(options)), transport_(std::move(argv), captureStderr), eventLoop_(eventLoop) {
        StartReadLoop();
        StartStderrReadLoop();
        StartWriteLoop();
    }

    // Takes ownership of an already-open TransportT directly -- for tests
    // driving a raw pipe pair with no real subprocess involved, mirroring
    // every Client's own test constructor.
    FramedConnection(TransportT transport, ned::ui::EventLoop& eventLoop, Options options)
        : options_(std::move(options)), transport_(std::move(transport)), eventLoop_(eventLoop) {
        StartReadLoop();
        StartStderrReadLoop(); // no-op unless transport_ was itself constructed with captureStderr
        StartWriteLoop();
    }

    // lsp-use-after-free follow-up: alive_ must be cleared first -- see this
    // file's own header comment. Then the writer, so a graceful-shutdown
    // drain still has a live transport; then the hang-up that wakes both
    // readers; transport_ itself is destroyed only once no thread uses it.
    // Assigning an empty jthread requests a stop and joins.
    ~FramedConnection() {
        *alive_ = false;
        writeThread_ = std::jthread();
        transport_.Close();
        readThread_   = std::jthread();
        stderrThread_ = std::jthread();
    }

    FramedConnection(const FramedConnection&)            = delete;
    FramedConnection& operator=(const FramedConnection&) = delete;
    // Not movable: the background thread lambdas capture `this` directly.
    FramedConnection(FramedConnection&&)            = delete;
    FramedConnection& operator=(FramedConnection&&) = delete;

    // Enqueues frame for writeThread_ to send, returning immediately --
    // never blocks the caller on the transport's own write. Callers run on
    // the main thread only, so enqueue order is call order is on-wire order.
    void SendFrame(std::string frame) {
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            writeQueue_.push_back(std::move(frame));
        }
        writeCv_.notify_one();
    }

    // Invoked on the main thread with one complete frame's text, in arrival
    // order, for every frame the background read loop parses. An empty
    // frame (a bare blank-line keepalive under ACP's newline framing; never
    // produced by LSP/DAP's Content-Length framing in practice) is silently
    // skipped before this is ever called.
    void SetOnFrame(std::function<void(std::string)> handler) {
        onFrame_ = std::move(handler);
    }

    // Invoked exactly once, on the main thread, the moment the background
    // read loop stops running for any reason -- clean EOF (the process
    // exited) or a malformed/stalled frame. reason is a short, human-
    // readable cause. Unset by default, a safe no-op.
    void SetOnDisconnected(std::function<void(std::string reason)> handler) {
        onDisconnected_ = std::move(handler);
    }

    // async-write-queue follow-up: marks this connection for graceful
    // shutdown -- guarantees any currently-queued or subsequently-enqueued
    // frame is actually attempted by writeThread_ before it stops, instead
    // of the destructor's ordinary best-effort/no-drain policy. Call this
    // immediately before a best-effort courtesy request/notification (e.g.
    // "shutdown"+"exit", "disconnect", "session/close") that must actually
    // reach the wire before this object is destroyed. Not meant for ordinary
    // mid-session teardown -- draining a queue against a connection that's
    // already dying/dead is exactly the main-thread stall this whole
    // mechanism exists to avoid.
    void PrepareForGracefulShutdown() {
        drainQueueOnStop_ = true;
    }

  private:
    void StartReadLoop() {
        // closed-connection-never-parks follow-up: the stop token is
        // genuinely consulted rather than ignored, so a read thread the
        // scheduler has not yet run by the time its owner is destroyed exits
        // here instead of entering a read at all.
        readThread_ = std::jthread([this](const std::stop_token& stopToken) {
            while (!stopToken.stop_requested()) {
                std::optional<std::string> frame;
                try {
                    frame = transport_.ReadFrame(); // blocks
                }
                catch (const std::exception& e) {
                    eventLoop_.Post([this, alive = alive_, reason = std::string(e.what())] {
                        if (!*alive) {
                            return; // this FramedConnection is gone
                        }
                        LogMessage(options_.logCategory, LogSeverity::Warning, reason);
                        if (onDisconnected_) {
                            onDisconnected_(reason);
                        }
                    });
                    return;
                }
                if (!frame) {
                    // EOF -- the process exited (or this object is being
                    // destroyed: the destructor's transport_.Close() is
                    // what makes the blocking ReadFrame() call above
                    // finally return). alive_ is what makes that
                    // safe, not an assumption about when this callback runs
                    // relative to destruction.
                    eventLoop_.Post([this, alive = alive_] {
                        if (!*alive) {
                            return;
                        }
                        const std::string reason = std::string(options_.entityNoun) + " exited (EOF)";
                        LogMessage(options_.logCategory, LogSeverity::Warning, reason);
                        if (onDisconnected_) {
                            onDisconnected_(reason);
                        }
                    });
                    return;
                }
                if (frame->empty()) {
                    // A bare blank-line keepalive under ACP's newline
                    // framing -- callers skip it rather than this layer
                    // guessing at protocol-specific keepalive conventions.
                    // Never produced by LSP/DAP's Content-Length framing.
                    continue;
                }
                // ned::ui::EventLoop::Run's own loop drains any Post()ed
                // work unconditionally and that alone earns the next
                // iteration a repaint, so a frame arriving here and updating
                // real state is shown without needing an explicit "force a
                // frame" call.
                eventLoop_.Post([this, alive = alive_, frameText = std::move(*frame)]() mutable {
                    if (!*alive) {
                        return;
                    }
                    if (onFrame_) {
                        onFrame_(std::move(frameText));
                    }
                });
            }
        });
    }

    void StartStderrReadLoop() {
        if (transport_.StderrFd() < 0) {
            return; // not captured
        }

        // Captured by value: the label is read here, on the main thread.
        std::string label = transport_.ProcessLabel();

        stderrThread_ = std::jthread([this, label = std::move(label)](std::stop_token) {
            std::string buffered;
            while (true) {
                std::string chunk;
                try {
                    chunk = transport_.ReadStderr();
                }
                catch (const std::exception&) {
                    return; // a genuine read error here is rare and non-actionable -- the stdout loop's own EOF/malformed-frame path is what reports the real disconnect
                }
                if (chunk.empty()) {
                    return; // EOF -- process exited, or this object is being destroyed
                }
                buffered += chunk;

                std::size_t newline;
                while ((newline = buffered.find('\n')) != std::string::npos) {
                    std::string line = buffered.substr(0, newline);
                    buffered.erase(0, newline + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty()) {
                        continue; // a blank line between real diagnostic output isn't worth a log entry
                    }
                    // By value, not through this: the line can still be
                    // queued after the connection is destroyed.
                    eventLoop_.Post([category = options_.logCategory, severity = options_.stderrSeverity, label,
                                     line = std::move(line)] {
                        // diagnostics-log-rollup follow-up: LogMessage
                        // itself coalesces this against the immediately
                        // preceding entry when it repeats verbatim.
                        LogMessage(category, severity, label.empty() ? line : label + ": " + line);
                    });
                }
            }
        });
    }

    void StartWriteLoop() {
        writeThread_ = std::jthread([this](const std::stop_token& stopToken) {
            while (true) {
                std::string frame;
                {
                    std::unique_lock<std::mutex> lock(writeMutex_);
                    writeCv_.wait(lock, stopToken, [&] { return !writeQueue_.empty() || stopToken.stop_requested(); });
                    if (writeQueue_.empty()) {
                        return; // nothing left -- clean stop
                    }
                    if (stopToken.stop_requested() && !drainQueueOnStop_.load()) {
                        return; // ordinary teardown: don't attempt stale writes against a dying/dead connection
                    }
                    frame = std::move(writeQueue_.front());
                    writeQueue_.pop_front();
                }
                try {
                    transport_.WriteFrame(frame);
                }
                catch (const std::exception&) {
                    return; // pipe's gone -- the read loop's own EOF/error path already reports this
                }
            }
        });
    }

    Options options_;

    // lsp-use-after-free follow-up: see this file's own header comment.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    // The threads are stopped and joined in ~FramedConnection's body, before
    // any member is destroyed.
    std::jthread readThread_;
    std::jthread stderrThread_;
    TransportT   transport_;

    ned::ui::EventLoop& eventLoop_;

    std::mutex writeMutex_;
    // condition_variable_any, not condition_variable: plain
    // condition_variable::wait never wakes on request_stop() alone --
    // condition_variable_any's stop_token-aware wait(lock, stopToken,
    // predicate) overload registers its own internal stop_callback that
    // does the notifying, which is what makes ~FramedConnection()'s
    // join of writeThread_ return promptly instead of hanging
    // whenever the writer is idly waiting on an empty queue at destruction
    // time.
    std::condition_variable_any writeCv_;
    std::deque<std::string>     writeQueue_;
    std::atomic<bool>           drainQueueOnStop_ = false; // see PrepareForGracefulShutdown
    std::jthread                writeThread_;

    std::function<void(std::string)>        onFrame_;
    std::function<void(std::string reason)> onDisconnected_;
};

} // namespace ned::editor::protocol

#endif // NED_EDITOR_PROTOCOL_FRAMEDCONNECTION_H
