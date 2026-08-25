//
// ChildProcess-hang-protection round 2 follow-up. The original audit (see
// ROADMAP.md's "Subprocess hang/timeout protection" entry) hardcoded every
// timeout it introduced as a single compile-time constant per mechanism --
// this is the Janet-configurable surface that left open. Three settings,
// matched to the three distinct mechanisms that audit built (a fourth,
// TaskProcess's own read loop, deliberately has no timeout at all -- silence
// isn't a hang signal for a legitimately slow build/test, and Cancel()
// already provides user-triggered recovery -- unchanged here):
//
//  - SubprocessReadTimeoutMs: how long a *main-thread*, blocking subprocess
//    read (system-clipboard paste, the first toolchain-include-path query
//    for a language) waits before killing the child and failing gracefully
//    rather than freezing the whole editor. Default 5000ms.
//  - ProtocolStallTimeoutMs: how long silence *after* an LSP/DAP/ACP
//    frame/message has started arriving is tolerated before the connection
//    is treated as stalled and disconnected -- idle time *between* messages
//    stays unbounded regardless of this setting (that's the normal case).
//    Default 30000ms.
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
void SetSubprocessReadTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds SubprocessReadTimeoutMs(); // default 5000ms

void SetProtocolStallTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds ProtocolStallTimeoutMs(); // default 30000ms

void SetProtocolRequestTimeoutMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds ProtocolRequestTimeoutMs(); // default 30000ms

} // namespace ned::editor

#endif // NED_EDITOR_PROCESSTIMEOUTS_H
