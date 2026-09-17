//
// Loads ned's bundled Janet plugins -- `DataDir()/plugins/*.janet`
// (Editor/DataDir.h; Source/Janet/Plugins/ in the source tree) -- into env.
//

#ifndef NED_JANET_PLUGINLOADER_H
#define NED_JANET_PLUGINLOADER_H

#include <string>
#include <string_view>

#include "Environment.h"

namespace ned::janet {

// Source text of the bundled plugin `<name>.janet`. Throws
// std::runtime_error naming the path when it cannot be read.
[[nodiscard]] std::string ReadBundledPlugin(std::string_view name);

// Evaluates every bundled plugin in env: vcs-git (the reference
// ned/vcs-register-provider implementation for git), gradients (the array
// sugar over ned/theme-gradient plus the preset paints, Docs/Translucency.md)
// and languages (the bundled capture classifiers). Called from main.cpp
// right after InstallEditorBindings and before LoadInitFile, so a user's
// own init.janet can override or unregister a bundled registration
// afterward. Propagates Environment::DoString's exception on a Janet-level
// error -- a bundled plugin failing to load is a real bug, not something to
// silently swallow.
void LoadBundledPlugins(Environment& env);

} // namespace ned::janet

#endif // NED_JANET_PLUGINLOADER_H
