//
// Bundled Janet plugin source, embedded into the binary at CMake configure
// time from each plugin's own checked-in .janet file -- see CMakeLists.txt's
// ned_embed_janet_plugin calls (mirrors ned_embed_treesitter_query's exact
// "generate a .cpp constant from a checked-in source file" approach; this
// codebase has no Resources/-style loose-runtime-file convention).
//

#ifndef NED_JANET_PLUGINS_H
#define NED_JANET_PLUGINS_H

namespace ned::janet::plugins {

// Source/Janet/Plugins/vcs-git.janet -- the reference ned/vcs-register-provider
// implementation for git. See PluginLoader.h for how/when this gets loaded.
extern const char* const kVcsGit;

} // namespace ned::janet::plugins

#endif // NED_JANET_PLUGINS_H
