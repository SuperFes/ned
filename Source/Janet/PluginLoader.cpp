#include "PluginLoader.h"

#include "Plugins.h"

namespace ned::janet {

void LoadBundledPlugins(Environment& env) {
    env.DoString(plugins::kVcsGit, "vcs-git.janet");
}

} // namespace ned::janet
