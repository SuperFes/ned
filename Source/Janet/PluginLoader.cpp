#include "PluginLoader.h"

#include "Plugins.h"

namespace ned::janet {

void LoadBundledPlugins(Environment& env) {
    env.DoString(plugins::kVcsGit, "vcs-git.janet");
    env.DoString(plugins::kGradients, "gradients.janet");
}

} // namespace ned::janet
