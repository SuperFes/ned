//
// lsp-broker follow-up. The imperative I/O shell around LspBroker.h's pure
// BrokerRouter -- real sockets, real subprocesses, real threads. This is
// `ned --lsp-broker`'s entire body (see main.cpp's own dispatch, mirroring
// RunDetectTheme's early-return shape: parsed and run before
// ned::ui::EventLoop/Notcurses ever construct, since this process is
// headless).
//

#ifndef NED_EDITOR_LSP_LSPBROKERMAIN_H
#define NED_EDITOR_LSP_LSPBROKERMAIN_H

namespace ned::editor::lsp {

// Binds BrokerSocketPath(), accepts connections, relays LSP traffic through
// a BrokerRouter, spawns/tears down real language-server subprocesses as
// directed. Blocks until a ned/broker-shutdown control message (from
// `ned --lsp-broker-stop`) or the whole-daemon idle safety net ends it.
// Returns a process exit code: 0 on a clean shutdown, nonzero on a fatal
// bind/listen failure (reported to stderr before returning).
// maxConcurrentServers: see LspBroker.h's own header comment on LRU
// eviction; forwarded to BrokerRouter unchanged.
[[nodiscard]] int RunLspBrokerDaemon(int maxConcurrentServers = 8);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPBROKERMAIN_H
