//
// lsp-broker follow-up. The editor-side (as opposed to BrokerMain.cpp's
// daemon-side) half of attaching to the LSP broker -- a small, testable
// seam kept out of Manager.cpp so that file's own ClientForLanguage
// stays focused on "which Client do I have for this language," not raw
// socket plumbing.
//

#ifndef NED_EDITOR_LSP_BROKERCONNECT_H
#define NED_EDITOR_LSP_BROKERCONNECT_H

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Client.h"

namespace ned::ui {
class EventLoop;
} // namespace ned::ui

namespace ned::editor::lsp {

// foreground-takeover follow-up. What is listening on the broker socket
// right now, as far as a would-be daemon needs to care.
//
// Unidentified is a running daemon that didn't answer ned/broker-info --
// in practice one built before that control message existed. Treated as
// takeable rather than left alone: an always-on instance is a deliberate,
// supervised thing that says so, and refusing to start because something
// unrecognizable holds the socket would strand the user with no way
// forward but a manual kill.
struct BrokerProbe {
    enum class State {
        NotRunning,   // nothing is listening
        Unidentified, // something is, but it didn't answer
        Ephemeral,    // an ordinary auto-spawned/`--lsp-broker` daemon
        Supervised,   // a deliberate `--foreground` instance
    };

    State state = State::NotRunning;
    int   pid   = 0; // 0 unless the daemon named itself
};

// Connects, asks ned/broker-info, and reads the one-frame answer. Never
// throws and never blocks past timeout -- every failure (no socket, a
// refused connection, a daemon that says nothing) is an ordinary answer
// here, not an error. socketPathOverride is the same test-only seam
// TryConnectToBroker carries, resolved internally for the same reason.
[[nodiscard]] BrokerProbe ProbeBroker(std::optional<std::filesystem::path> socketPathOverride = std::nullopt,
                                      std::chrono::milliseconds            timeout            = std::chrono::seconds(2));

// Sends ned/broker-shutdown and then waits for the socket to actually stop
// accepting connections -- the daemon gives every language server it holds
// a real LSP shutdown/exit first (Broker.h's Shutdown()), so "sent" and
// "gone" can be seconds apart, and binding in that window would fail or,
// worse, race. True once nothing is listening, including when nothing was
// listening to begin with; false if the daemon was still there when
// timeout ran out.
[[nodiscard]] bool ShutDownBrokerAndWait(std::optional<std::filesystem::path> socketPathOverride = std::nullopt,
                                         std::chrono::milliseconds            timeout            = std::chrono::seconds(20));

// Attempts to attach to an already-running LSP broker daemon (see
// BrokerMain.h) for (projectRoot, language) over socketPath -- connects,
// writes the ned/broker-attach control frame (argv is only actually
// honored by the daemon if this is the first attach ever seen for this
// exact (projectRoot, language) pair; otherwise it's silently ignored, see
// Broker.h's own header comment), and hands back a ready-to-use
// Client built on that socket (the Transport-taking constructor,
// startHandshakeComplete = false -- Manager::ClientForLanguage's own
// initialize/initialized sequence, WireNotificationHandlers, and every
// downstream request all run completely unchanged from here on, just
// talking to the broker instead of a directly-spawned subprocess).
//
// socketPathOverride is a test-only seam (BrokerSocketPathTest.cpp's own
// convention doesn't exist yet, but this mirrors e.g. BackupFileBeforeSave's
// injectable nowSeconds) -- nullopt (the real caller's default) resolves
// BrokerSocketPath() internally. Deliberately resolved *inside* this
// function rather than as a throwing default argument
// (BrokerRuntimeDirectory() throws if no XDG_RUNTIME_DIR/XDG_STATE_HOME/
// HOME is set) -- a default-argument expression evaluates at the call
// site, which would let that exception escape uncaught from
// Manager::ClientForLanguage's own try-broker-first branch; resolving
// it here instead keeps this function's "never throws" contract airtight.
//
// Returns nullptr -- never throws -- for any failure: no resolvable socket
// path, no broker socket there, connection refused, or a write failure
// once connected. Every one of these is a legitimate, expected outcome (no
// broker running yet is the common case before the auto-spawn path
// exists, and remains a normal outcome afterward too, e.g. the broker
// crashed moments ago) -- the caller's own contract is to fall back to
// spawning a server directly on nullptr, exactly like today, never to
// treat this as an error worth surfacing on its own.
[[nodiscard]] std::unique_ptr<Client> TryConnectToBroker(const std::filesystem::path& projectRoot, const std::string& language,
                                                            const std::vector<std::string>& argv, ned::ui::EventLoop& eventLoop,
                                                            std::optional<std::filesystem::path> socketPathOverride = std::nullopt);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_BROKERCONNECT_H
