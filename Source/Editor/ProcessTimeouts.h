//
// ChildProcess-hang-protection round 2 follow-up. The original audit (see
// ROADMAP.md's "Subprocess hang/timeout protection" entry) hardcoded every
// timeout it introduced as a single compile-time constant per mechanism --
// this is the Janet-configurable surface that left open. Four settings,
// matched to the four distinct mechanisms that audit and its own follow-up
// built (a fifth, TaskProcess's own read loop, deliberately has no timeout
// at all -- silence isn't a hang signal for a legitimately slow build/test,
// and Cancel() already provides user-triggered recovery -- unchanged here):
//
//  - SubprocessReadTimeoutMs: how long a *main-thread*, blocking subprocess
//    read (system-clipboard paste, the first toolchain-include-path query
//    for a language) waits before killing the child and failing gracefully
//    rather than freezing the whole editor. Default 5000ms.
//  - SubprocessWriteTimeoutMs (write-side-hang-protection follow-up): the
//    write-side twin -- how long a blocking write to a subprocess's stdin
//    (system-clipboard copy, an LSP/ACP frame, a terminal keystroke) waits
//    for the child to keep draining before giving up, rather than blocking
//    forever the moment its pipe buffer fills and it stops reading (a real,
//    reproduced full-editor lockup: a wedged LSP server left a queued
//    full-document textDocument/didChange write with nothing on the other
//    end to drain it). Default 5000ms.
//  - ProtocolReadStallTimeoutMs/ProtocolWriteStallTimeoutMs
//    (protocol-stall-timeout-split follow-up): how long silence *after* an
//    LSP/DAP/ACP frame/message has started arriving (read) or after a write
//    to a server's stdin stops draining (write) is tolerated before the
//    connection is treated as stalled and disconnected -- idle time
//    *between* messages stays unbounded regardless of either setting (that's
//    the normal case). Originally one shared ProtocolStallTimeoutMs value;
//    split so a legitimately slow read (a large workspace-wide rename) can be
//    given more tolerance than a write, which a healthy server should never
//    take long just to accept. Both default 30000ms, matching the original
//    shared value byte-for-byte until deliberately retuned.
//  - ProtocolRequestTimeoutMs: how long a sent LSP/DAP/ACP request is kept
//    pending before ExpireStaleRequests resolves it with a synthetic
//    timeout failure. Default 30000ms.
//
// One process-wide setting each, mutex-guarded static state mirroring
// TabWidth.h's exact shape (DiffRefreshSettings.h's own recent copy of it).
// Read from both the main thread and a protocol client's background read
// thread (Transport::ReadFrame/ReadMessage's own stall-timeout default
// argument is evaluated wherever it's called from) -- the mutex is what
// makes that safe, same as every other setting here.
//

#ifndef NED_EDITOR_PROCESSTIMEOUTS_H
#define NED_EDITOR_PROCESSTIMEOUTS_H

#include <chrono>

namespace ned::editor {

// Non-positive values are clamped to 1ms rather than rejected, same
// convention as TabWidth::SetTabWidth/DiffRefreshSettings.h.
void                                    SetSubprocessReadTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds SubprocessReadTimeoutMs(); // default 5000ms

void                                    SetSubprocessWriteTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds SubprocessWriteTimeoutMs(); // default 5000ms

void                                    SetProtocolReadStallTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds ProtocolReadStallTimeoutMs(); // default 30000ms

void                                    SetProtocolWriteStallTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds ProtocolWriteStallTimeoutMs(); // default 30000ms

void                                    SetProtocolRequestTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds ProtocolRequestTimeoutMs(); // default 30000ms

} // namespace ned::editor

#endif // NED_EDITOR_PROCESSTIMEOUTS_H
