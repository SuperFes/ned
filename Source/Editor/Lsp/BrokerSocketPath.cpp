#include "BrokerSocketPath.h"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace ned::editor::lsp {

std::filesystem::path BrokerRuntimeDirectory() {
    if (const char* xdgRuntimeHome = std::getenv("XDG_RUNTIME_DIR"); xdgRuntimeHome && *xdgRuntimeHome) {
        return std::filesystem::path(xdgRuntimeHome) / "ned";
    }
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "run";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "run";
    }
    throw std::runtime_error("ned: cannot determine a runtime directory for the LSP broker (none of XDG_RUNTIME_DIR, XDG_STATE_HOME, HOME is set)");
}

void EnsureBrokerRuntimeDirectory() {
    const std::filesystem::path dir = BrokerRuntimeDirectory();
    std::error_code              ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        throw std::runtime_error("ned: failed to create LSP broker runtime directory " + dir.string() + ": " + ec.message());
    }
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    if (ec) {
        throw std::runtime_error("ned: failed to restrict permissions on LSP broker runtime directory " + dir.string() + ": " + ec.message());
    }
}

std::filesystem::path BrokerSocketPath() {
    return BrokerRuntimeDirectory() / "broker.sock";
}

std::filesystem::path BrokerLockPath() {
    return BrokerRuntimeDirectory() / "broker.lock";
}

std::filesystem::path BrokerLogPath() {
    return BrokerRuntimeDirectory() / "broker.log";
}

} // namespace ned::editor::lsp
