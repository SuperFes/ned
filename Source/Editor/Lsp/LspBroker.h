//
// lsp-broker follow-up. The pure routing/state-machine core of the LSP
// broker -- a small headless daemon (Source/Editor/Lsp/LspBrokerMain.cpp),
// one per machine (not one per project), that keeps every project's real
// language-server subprocess alive across `ned` restarts, so a fresh `ned`
// launch can attach to an already-warm clangd instead of paying its
// ~10-12s startup/compilation-database-validation cost on every open
// (measured live: this cost is per-*process*, not per-changed-file -- see
// ROADMAP.md's history for the measurement). One daemon multiplexing every
// project also means `ned`'s own exit no longer has to wait on an LSP
// shutdown/exit handshake at all -- the daemon outlives any single `ned`
// process, so closing the terminal is just closing a socket.
//
// This file has zero I/O -- no sockets, no threads, no ChildProcess/
// Transport -- deliberately, so its correctness (id-multiplexing, the
// per-client "fake handshake," crash/disconnect handling, LRU eviction
// under pressure) is unit-testable without spawning a single real process.
// It consumes/produces plain nlohmann::json frames and opaque
// ConnectionIds; LspBrokerMain.cpp is the only place that turns those into
// real socket reads/writes and real subprocess spawns.
//
// Every language-server entry is keyed by (project root, language) rather
// than language alone -- one daemon now serves every project a `ned`
// process attaches from, not just one. The composite key is a single
// string, root + '\x1f' + language, the same convention LspManager.h's own
// activeProgress_ map already uses for a composite string key.
//
// The core protocol problem this solves: LSP forbids re-"initialize"-ing
// an already-initialized server connection, but every `ned` process that
// attaches to the daemon needs to go through a real initialize/initialized
// handshake of its own (its own editor::lsp::LspClient is constructed with
// startHandshakeComplete = false, unchanged from the direct-spawn path --
// see LspManager.cpp's ClientForLanguage). So BrokerRouter performs exactly
// one real handshake with the actual spawned server per (root, language)
// entry (using whichever attaching client's own "initialize" params happen
// to arrive first -- the server doesn't care who asked), caches the
// resulting InitializeResult, and answers every client's own "initialize"
// locally from that cache without ever forwarding it to the real server a
// second time. A client's "initialized" notification is swallowed the same
// way -- it closes that client's own local handshake gate
// (LspClient::handshakeComplete_) but has no real server-facing meaning
// here, since the real "initialized" already went out exactly once.
//
// Beyond the handshake, ordinary traffic is a straightforward N:1 relay:
// a client's outbound *request* gets a fresh broker-generated id (so two
// clients' colliding ids never collide against the one real connection);
// the matching response is rewritten back to the client's own original id
// and routed to just that client. A client's *notification* (didOpen,
// didChange, ...) relays to the server verbatim -- no id to rewrite. A
// server-initiated *notification* (publishDiagnostics, $/progress, ...)
// broadcasts to every client attached to that same entry -- safe even for
// a client that never opened the URI in question, since
// LspManager::HandlePublishDiagnostics already no-ops gracefully for a URI
// it doesn't recognize (BufferList::FindByPath returning null). A
// server-initiated *request* (e.g. window/workDoneProgress/create) is
// auto-acknowledged with a null result directly by the broker rather than
// routed to any one client.
//
// LRU eviction: a maxConcurrentServers cap is checked whenever a brand-new
// (root, language) pair is about to spawn. Over the cap, the entry with
// the oldest last-active timestamp *among entries with zero attached/
// queued clients right now* is torn down first (real LSP shutdown/exit if
// it was Ready) -- pressure only ever falls on something genuinely idle,
// never on a live client. If every current entry is busy, the cap is
// simply exceeded rather than refusing the new project.
//

#ifndef NED_EDITOR_LSP_LSPBROKER_H
#define NED_EDITOR_LSP_LSPBROKER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::lsp {

using Json = nlohmann::json;

// Opaque per-connection handle -- LspBrokerMain.cpp assigns these (e.g. a
// simple incrementing counter as each socket is accept()ed); this file
// never interprets the value.
using ConnectionId = std::uint64_t;

// One (root, language) entry's real-server connection lifecycle.
// NotStarted: nothing spawned, no client has attached yet (or the entry was
// reset after a crash/disconnect/eviction -- see ServerDisconnected).
// SpawningProcess: a SpawnServer action was emitted, waiting for
// LspBrokerMain.cpp to report ServerSpawned/ServerSpawnFailed.
// AwaitingRealHandshake: the process is running and the broker's own
// synthetic "initialize" has been sent to it, waiting for its response.
// Ready: the real handshake completed successfully -- ordinary traffic
// relays normally, and this entry is now a valid LRU-eviction candidate
// once idle. Failed: the process failed to spawn or the real handshake
// itself errored -- terminal for this entry's lifetime (mirrors
// LspServerConfig.h's own "no auto-retry beyond that" precedent); the next
// successful attach for the same (root, language) only happens once this
// entry is reset via ServerDisconnected/eviction or the daemon restarts.
enum class BrokerLanguageStatus { NotStarted, SpawningProcess, AwaitingRealHandshake, Ready, Failed };

// One instruction for LspBrokerMain.cpp's imperative layer to carry out --
// BrokerRouter never performs I/O itself, only describes what should
// happen. Not every field is populated for every Kind; see each Kind's own
// comment below for which ones matter.
struct BrokerAction {
    enum class Kind {
        SpawnServer,     // root, language, argv: spawn the real language-server subprocess.
        SendToServer,    // root, language, frame: write frame to that entry's real server.
        SendToClient,    // connection, frame: write frame to one attached client socket.
        CloseClient,     // connection: close and forget one client socket.
        CloseServer,     // root, language: close and forget that entry's real server connection/subprocess (after any shutdown/exit frames, if sent).
        ShutdownProcess, // no fields: every entry has been told to shut down -- exit the daemon process once pending writes flush.
    };

    Kind                      kind;
    std::string               root;
    std::string               language;
    std::vector<std::string>  argv;
    Json                      frame;
    ConnectionId              connection = 0;
};

// The pure routing core -- one instance for the whole daemon process (every
// project/language pair shares it; ordinary map lookups keyed by a
// composite string are cheap and this avoids one mutex per entry in the
// real I/O layer). Not thread-safe on its own; LspBrokerMain.cpp is
// expected to serialize every call behind one mutex (see this file's own
// header comment for why thread-per-connection, not a shared poll() loop,
// is the right I/O shape here).
class BrokerRouter {
  public:
    // maxConcurrentServers: see this file's own header comment on LRU
    // eviction. Zero or negative disables the cap entirely (never evicts).
    explicit BrokerRouter(int maxConcurrentServers = 8);
    ~BrokerRouter() = default;

    BrokerRouter(const BrokerRouter&)            = delete;
    BrokerRouter& operator=(const BrokerRouter&) = delete;

    // A client socket sent its ned/broker-attach control frame (the very
    // first frame on any new connection, before any real LSP traffic).
    // Registers conn -> (root, language). If this is the first attach ever
    // seen for this (root, language) pair (status == NotStarted), argv is
    // adopted; if adopting it would exceed maxConcurrentServers, the
    // least-recently-active idle entry is evicted first (see
    // MaybeEvictForCapacity), then a SpawnServer action is emitted. A later
    // attach's argv for an already-started pair is silently ignored
    // (documented v1 limitation: changing ned/set-lsp-command mid-session
    // doesn't affect an already-warm entry until it's evicted or
    // idle-exits). now is injectable for tests only, defaulting to the
    // real clock.
    [[nodiscard]] std::vector<BrokerAction> ClientAttached(ConnectionId conn, std::string root, std::string language,
                                                            std::vector<std::string> argv,
                                                            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // An already-attached client sent an ordinary JSON-RPC frame -- may be
    // that client's own "initialize"/"initialized" (handled specially, see
    // this file's own header comment), or real post-handshake traffic
    // (relayed/id-rewritten). A frame from a connection that never attached
    // (no matching ClientAttached) is ignored. now is injectable for tests
    // only, defaulting to the real clock.
    [[nodiscard]] std::vector<BrokerAction> ClientFrame(ConnectionId conn, const Json& frame,
                                                         std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // conn's socket closed for any reason. Forgets every trace of conn:
    // its queued-but-unanswered "initialize" (if any), its entry in the
    // owning (root, language) pair's attached set, and any in-flight
    // pendingByBrokerId entries whose response would have routed to it
    // (simply dropped, uninvoked -- matches LspClient's own "abandoned at
    // shutdown" convention). Never touches the real server connection --
    // one client leaving doesn't affect anyone else. A no-op if conn was
    // never attached.
    [[nodiscard]] std::vector<BrokerAction> ClientDisconnected(ConnectionId conn);

    // LspBrokerMain.cpp successfully spawned the real subprocess for
    // (root, language). If a client's own "initialize" frame is already
    // queued (the common case -- SyncBuffer sends it immediately after
    // construction, so it typically races ahead of the subprocess actually
    // finishing exec), this immediately sends the broker's own synthetic
    // initialize using that queued frame's params and transitions to
    // AwaitingRealHandshake. Otherwise just marks the process running,
    // deferring the real handshake until a client's initialize frame does
    // arrive (see ClientFrame's own "initialize" handling).
    [[nodiscard]] std::vector<BrokerAction> ServerSpawned(const std::string& root, const std::string& language);

    // The real subprocess for (root, language) failed to spawn at all
    // (missing binary, posix_spawn failure, ...). Transitions to Failed and
    // answers every queued client "initialize" with a JSON-RPC error
    // carrying reason.
    [[nodiscard]] std::vector<BrokerAction> ServerSpawnFailed(const std::string& root, const std::string& language, std::string reason);

    // A frame arrived from the real server subprocess for (root, language).
    // Dispatches on shape: the response to the broker's own in-flight
    // synthetic initialize (AwaitingRealHandshake -> Ready or Failed,
    // flushing every queued client "initialize"); an ordinary response
    // matching a rewritten id in pendingByBrokerId (routed back to its
    // owning client, id restored); a server-initiated request (has both
    // "id" and "method" -- auto-acknowledged with a null result, never
    // routed to any client); or a notification (broadcast to every client
    // attached to this entry). now is injectable for tests only, defaulting
    // to the real clock.
    [[nodiscard]] std::vector<BrokerAction> ServerFrame(const std::string& root, const std::string& language, const Json& frame,
                                                         std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // The real server subprocess for (root, language) exited/crashed for
    // any reason. Every client currently attached (or queued
    // mid-handshake) to this entry is closed outright (CloseClient) rather
    // than transparently reattached to a freshly-respawned server -- this
    // deliberately reuses LspManager's own already-correct crash-recovery
    // path (ClientDisconnected there erases bufferState_ and lets the next
    // SyncBuffer call redo a real spawn-and-didOpen sequence from scratch)
    // instead of duplicating equivalent logic inside the broker. The
    // entry's entire state is dropped -- the next attach for it starts
    // completely fresh, as if the daemon had just started. Also the
    // mechanism LRU eviction and per-entry idle-timeout both use
    // internally to tear an entry down.
    [[nodiscard]] std::vector<BrokerAction> ServerDisconnected(const std::string& root, const std::string& language);

    // Sweeps every entry with zero attached/queued clients whose
    // last-active timestamp is older than now - idleTimeout, tearing each
    // down the same way ServerDisconnected does (real LSP shutdown/exit
    // first if Ready). What LspBrokerMain.cpp's periodic timer calls for
    // the per-entry idle tier (see this file's own header comment on the
    // two idle tiers) -- distinct from Shutdown(), which is unconditional
    // and ends the whole daemon.
    [[nodiscard]] std::vector<BrokerAction> IdleSweep(std::chrono::steady_clock::time_point now, std::chrono::milliseconds idleTimeout);

    // ned/broker-shutdown control message (from `ned --lsp-broker-stop`)
    // or the whole-daemon safety-net timeout decided it's time to exit.
    // Called directly by LspBrokerMain.cpp's imperative layer the moment it
    // sees ned/broker-shutdown as a connection's very first frame -- that
    // control message never goes through ClientAttached/ClientFrame at
    // all, since a stop request has no project/language of its own to
    // attach as. For every entry currently Ready, sends a real LSP "shutdown" request
    // immediately followed by "exit" (broker-generated ids; v1
    // deliberately doesn't wait for the shutdown response before also
    // queuing exit -- LspBrokerMain.cpp's own process teardown terminates
    // the subprocess shortly after regardless, so this is strictly better
    // than a bare kill even without a strict two-phase wait). Every
    // attached client, across every entry, is closed. Always ends with a
    // single ShutdownProcess action.
    [[nodiscard]] std::vector<BrokerAction> Shutdown();

    // Number of connections currently attached to any entry (post
    // handshake) or still queued mid-handshake -- what LspBrokerMain.cpp's
    // whole-daemon safety-net timeout checks against "any activity at all"
    // before exiting the process on its own.
    [[nodiscard]] std::size_t ConnectionCount() const noexcept;

    // Test/introspection seam -- NotStarted for a (root, language) pair
    // never seen.
    [[nodiscard]] BrokerLanguageStatus StatusFor(const std::string& root, const std::string& language) const;

  private:
    struct PendingRequest {
        ConnectionId connection;
        Json         originalId;
    };

    struct QueuedInitialize {
        ConnectionId connection;
        Json         originalId;
        Json         params; // this client's own "initialize" request params
    };

    struct LanguageState {
        std::string                    root;
        std::string                    language;
        BrokerLanguageStatus           status = BrokerLanguageStatus::NotStarted;
        std::vector<std::string>       argv;
        bool                           processRunning = false; // ServerSpawned fired, still true even once AwaitingRealHandshake/Ready
        std::vector<QueuedInitialize>  pendingInitializeRequests;
        std::optional<Json>            cachedInitializeResult;
        std::string                    failureReason;
        int                            nextBrokerId          = 1;
        int                            handshakeBrokerId     = 0; // the broker's own in-flight synthetic "initialize" id, while AwaitingRealHandshake
        std::unordered_map<int, PendingRequest> pendingByBrokerId;
        std::unordered_set<ConnectionId>        attached; // completed their own initialize -> initialized round trip
        std::chrono::steady_clock::time_point   lastActive;
    };

    static std::string MakeKey(const std::string& root, const std::string& language);

    // Shared by ServerSpawned and ClientFrame's "initialize" handling (see
    // both methods' own doc comments for why either can be the one that
    // actually triggers the real handshake, depending on which arrives
    // first). Idempotent: a no-op unless status == SpawningProcess,
    // processRunning is true, and at least one client initialize is
    // queued.
    void MaybeStartRealHandshake(LanguageState& state, std::vector<BrokerAction>& actions);

    // Answers every entry in state.pendingInitializeRequests (success from
    // cachedInitializeResult, or an error built from failureReason) and
    // clears the queue. Used by both the Ready and Failed transitions in
    // ServerFrame/ServerSpawnFailed.
    void FlushPendingInitializeRequests(LanguageState& state, std::vector<BrokerAction>& actions) const;

    // The actual "tear this one entry down" body ServerDisconnected,
    // IdleSweep, MaybeEvictForCapacity, and Shutdown all funnel through:
    // closes every attached/queued client (CloseClient), sends real LSP
    // shutdown/exit first if the entry was Ready, and erases the entry
    // from languages_.
    void TearDownEntry(const std::string& key, std::vector<BrokerAction>& actions);

    // Called from ClientAttached's NotStarted -> SpawningProcess
    // transition, before emitting SpawnServer. A no-op if
    // maxConcurrentServers_ <= 0 or the current entry count is under the
    // cap. Otherwise evicts the single oldest-last-active entry among
    // those with zero attached/queued clients; if none qualifies (every
    // entry is busy), does nothing -- the cap is exceeded rather than
    // refusing the new spawn.
    void MaybeEvictForCapacity(std::vector<BrokerAction>& actions);

    int                                              maxConcurrentServers_;
    std::unordered_map<std::string, LanguageState>   languages_; // keyed by MakeKey(root, language)
    std::unordered_map<ConnectionId, std::string>    connectionKey_; // which (root, language) key a connection attached to
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPBROKER_H
