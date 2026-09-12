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

// Source/Janet/Plugins/gradients.janet -- the array sugar over
// ned/theme-gradient's one-line spec string, plus the bundled preset paints
// (see Docs/Translucency.md). Loaded before init.janet, so a user
// redefining a preset by name wins.
extern const char* const kGradients;

// Source/Janet/Plugins/languages.janet -- the bundled capture classifiers
// (Org headline level, TODO-vs-DONE), registered before the user's
// init.janet so a re-registration there replaces one.
extern const char* const kLanguages;

} // namespace ned::janet::plugins

#endif // NED_JANET_PLUGINS_H
