#include "LspBrokerMain.h"

#include <csignal>

#include "LspBrokerDaemon.h"

namespace ned::editor::lsp {

int RunLspBrokerDaemon(int maxConcurrentServers) {
    // The daemon writes to many sockets/pipes that routinely close out from
    // under it (an evicted/crashed/disconnected peer), and an unhandled
    // SIGPIPE's default action is to terminate the *entire* daemon over one
    // bad write, taking down every other project's warm session with it.
    // write()/send() already return EPIPE instead, which
    // Transport::WriteFrame already surfaces as a caught std::runtime_error.
    // async-write-queue follow-up: main.cpp's own main() now sets the exact
    // same disposition, for the same reason -- an earlier version of this
    // comment claimed the interactive editor didn't need it (Notcurses'
    // terminal setup supposedly shielding it); that was never verified and
    // turned out false, so don't assume this process is a special case
    // either.
    std::signal(SIGPIPE, SIG_IGN);

    // broker-reader-deadlock follow-up: the daemon itself now lives in
    // LspBrokerDaemon.h/.cpp as a real declared type (it was an anonymous-
    // namespace class in this file), so its threading/lifetime paths are
    // reachable from ned_tests and therefore covered by the ASan/UBSan
    // build. This function is the production entry point and nothing else.
    BrokerDaemon daemon(maxConcurrentServers);
    return daemon.Run();
}

} // namespace ned::editor::lsp
