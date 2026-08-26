//
// lsp-broker follow-up. The editor-side (as opposed to LspBrokerMain.cpp's
// daemon-side) half of attaching to the LSP broker -- a small, testable
// seam kept out of LspManager.cpp so that file's own ClientForLanguage
// stays focused on "which LspClient do I have for this language," not raw
// socket plumbing.
//

#ifndef NED_EDITOR_LSP_LSPBROKERCONNECT_H
#define NED_EDITOR_LSP_LSPBROKERCONNECT_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LspClient.h"

namespace ned::ui {
class EventLoop;
} // namespace ned::ui

namespace ned::editor::lsp {

// Attempts to attach to an already-running LSP broker daemon (see
// LspBrokerMain.h) for (projectRoot, language) over socketPath -- connects,
// writes the ned/broker-attach control frame (argv is only actually
// honored by the daemon if this is the first attach ever seen for this
// exact (projectRoot, language) pair; otherwise it's silently ignored, see
// LspBroker.h's own header comment), and hands back a ready-to-use
// LspClient built on that socket (the Transport-taking constructor,
// startHandshakeComplete = false -- LspManager::ClientForLanguage's own
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
// LspManager::ClientForLanguage's own try-broker-first branch; resolving
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
[[nodiscard]] std::unique_ptr<LspClient> TryConnectToBroker(const std::filesystem::path& projectRoot, const std::string& language,
                                                             const std::vector<std::string>& argv, ned::ui::EventLoop& eventLoop,
                                                             std::optional<std::filesystem::path> socketPathOverride = std::nullopt);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPBROKERCONNECT_H
