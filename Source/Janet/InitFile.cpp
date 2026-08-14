#include "InitFile.h"

#include <cstdlib>
#include <stdexcept>

namespace ned::janet {

std::filesystem::path InitFilePath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome && *xdgConfigHome) {
        return std::filesystem::path(xdgConfigHome) / "ned" / "init.janet";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "ned" / "init.janet";
    }

    throw std::runtime_error("ned: cannot determine config directory (neither XDG_CONFIG_HOME nor HOME is set)");
}

void LoadInitFile(Environment& env) {
    const std::filesystem::path path = InitFilePath();
    if (!std::filesystem::exists(path)) {
        return;
    }
    env.DoFile(path);
}

} // namespace ned::janet
