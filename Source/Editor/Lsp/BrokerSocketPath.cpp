#include "BrokerSocketPath.h"

#include "Editor/RuntimeDir.h"

namespace ned::editor::lsp {

std::filesystem::path BrokerRuntimeDirectory() {
    return RuntimeDirectory();
}

void EnsureBrokerRuntimeDirectory() {
    EnsureRuntimeDirectory();
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
