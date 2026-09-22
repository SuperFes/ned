//
// lsp-broker follow-up. The imperative I/O shell around Broker.h's pure
// BrokerRouter -- real sockets, real subprocesses, real threads. This is
// `ned --lsp-broker`'s entire body (see main.cpp's own dispatch: parsed and
// run before ned::ui::EventLoop/Notcurses ever construct, since this process
// is headless).
//

#ifndef NED_EDITOR_LSP_BROKERMAIN_H
#define NED_EDITOR_LSP_BROKERMAIN_H

#include <chrono>

namespace ned::editor::lsp {

// Binds BrokerSocketPath(), accepts connections, relays LSP traffic through
// a BrokerRouter, spawns/tears down real language-server subprocesses as
// directed. Blocks until a ned/broker-shutdown control message (from
// `ned --lsp-broker-stop`), a SIGTERM/SIGINT, or one of the idle timeouts
// ends it (see BrokerDaemon::Run()). Returns a process exit code: 0 on a
// clean shutdown, nonzero on a fatal bind/listen failure (reported to
// stderr before returning).
// maxConcurrentServers: see Broker.h's own header comment on LRU
// eviction; forwarded to BrokerRouter unchanged.
// wholeDaemonIdleTimeout: see BrokerDaemonOptions's own field of the same
// name -- the default here matches its default (1 minute, the ephemeral
// auto-spawn/`--lsp-broker` case); main.cpp's `--foreground` passes zero
// (never self-exit on idle) instead.
// supervised: whether this is the deliberate always-on instance
// (`ned --foreground`) rather than an ephemeral auto-spawn -- reported to
// whoever asks over ned/broker-info, which is how a second --foreground
// knows to refuse rather than take this one over (see
// BrokerDaemonOptions::supervised and main.cpp's RunForegroundBroker).
[[nodiscard]] int RunLspBrokerDaemon(int maxConcurrentServers = 8, std::chrono::milliseconds wholeDaemonIdleTimeout = std::chrono::minutes(1),
                                     bool supervised = false);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_BROKERMAIN_H
