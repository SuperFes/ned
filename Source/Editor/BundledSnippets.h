//
// ned's own curated default snippet set (Editor/SnippetRegistry.h) for the
// bundled languages where one earns its keep -- common control-flow/
// definition skeletons, not an exhaustive library. A deliberate departure
// from the LSP/DAP/task-config "nothing bundled or auto-detected" posture
// (see SnippetRegistry.h's own header comment for why a snippet body is a
// different kind of thing than an executable command/argv).
//

#ifndef NED_EDITOR_BUNDLEDSNIPPETS_H
#define NED_EDITOR_BUNDLEDSNIPPETS_H

namespace ned::editor {

// Registers every entry in this file's bundled table via RegisterSnippet.
// Called once from main.cpp, after InstallEditorBindings/LoadBundledPlugins
// and before LoadInitFile -- PluginLoader's own bundled-Janet-plugin
// placement precedent -- so a user's own ned/register-snippet call for the
// same (languageKey, trigger) in init.janet overrides a bundled entry
// outright (SnippetRegistry's "re-registering overwrites" rule), and an
// explicit empty-body call there erases one.
void RegisterBundledSnippets();

} // namespace ned::editor

#endif // NED_EDITOR_BUNDLEDSNIPPETS_H
