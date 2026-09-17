#include "PluginLoader.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "Editor/DataDir.h"

namespace ned::janet {

std::string ReadBundledPlugin(std::string_view name) {
    const std::filesystem::path path = ned::editor::DataDir() / "plugins" / (std::string(name) + ".janet");
    std::ifstream               in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read bundled plugin: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void LoadBundledPlugins(Environment& env) {
    for (const char* name : {"vcs-git", "gradients", "languages"}) {
        env.DoString(ReadBundledPlugin(name), std::string(name) + ".janet");
    }
}

} // namespace ned::janet
