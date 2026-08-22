//
// Session persistence gap follow-up: a project-local Janet plugin autoload
// directory, `<root>/.ned/plugins/*.janet` -- the project-scoped counterpart
// to Janet/PluginLoader.h's bundled plugins (vcs-git.janet and friends).
// Lets a project ship its own VCS-provider/task/mode registrations alongside
// `.ned/init.janet` instead of cramming everything into that one file.
//
// Discovery only -- this file has no opinion about *loading* a discovered
// path. Each one is arbitrary code, exactly like `.ned/init.janet` itself
// (see ProjectTrust.h's header comment), so main.cpp runs every discovered
// file through the same per-file trust check/prompt before ever calling
// Environment::DoFile on it.
//

#ifndef NED_EDITOR_PROJECTPLUGINS_H
#define NED_EDITOR_PROJECTPLUGINS_H

#include <filesystem>
#include <vector>

namespace ned::editor {

// Every `*.janet` file directly inside `<root>/.ned/plugins/` (not
// recursive), sorted lexicographically by filename for a deterministic,
// repeatable load order. Empty if the directory doesn't exist or holds no
// `.janet` files.
[[nodiscard]] std::vector<std::filesystem::path> ProjectPluginFiles(const std::filesystem::path& root);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTPLUGINS_H
