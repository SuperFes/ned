//
// Loads ned's bundled Janet plugins (Source/Janet/Plugins/*.janet, embedded
// via CMakeLists.txt's ned_embed_janet_plugin -- see Plugins.h) into env.
//

#ifndef NED_JANET_PLUGINLOADER_H
#define NED_JANET_PLUGINLOADER_H

#include "Environment.h"

namespace ned::janet {

// Evaluates every bundled plugin's embedded source in env. Called from
// main.cpp right after InstallEditorBindings and before LoadInitFile, so a
// user's own init.janet can override or unregister (via
// ned/vcs-register-provider under the same name) a bundled plugin's
// registration afterward. Propagates Environment::DoString's exception on a
// Janet-level error -- a bundled plugin failing to load is a real bug, not
// something to silently swallow.
void LoadBundledPlugins(Environment& env);

} // namespace ned::janet

#endif // NED_JANET_PLUGINLOADER_H
