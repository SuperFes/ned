//
// The snippet registry: trigger word -> snippet body (see Snippet.h for the
// body syntax), keyed by language key -- the same user-supplied string
// ned/set-lsp-command keys on ("cpp", "python", ...; LanguageKeyForMode in
// Mode.h derives it from a Mode's name on the lookup side), with "" meaning
// "every mode". Process-wide, mutex-guarded (OrgCapture.h's exact registry
// pattern); ned/register-snippet is the Janet-facing surface, and
// Editor/BundledSnippets.h registers a curated default set through this
// same C++ function (called from main.cpp before LoadInitFile) -- a
// deliberate departure from the LSP/DAP/task-config "nothing bundled or
// auto-detected" posture those subsystems keep, since a snippet body is
// inert user-facing content, not an executable command/argv the way a
// language server or debug adapter is. A user's own ned/register-snippet
// call for the same (languageKey, trigger) still overrides a bundled entry
// outright (this file's own "re-registering overwrites" rule), and an
// explicit empty-body call erases it.
//

#ifndef NED_EDITOR_SNIPPETREGISTRY_H
#define NED_EDITOR_SNIPPETREGISTRY_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor {

// Re-registering an existing (languageKey, trigger) overwrites it (matching
// CommandRegistry's "redefining is expected use" precedent); an empty body
// erases the entry (the ned/set-* "empty clears" convention). An empty
// trigger throws std::runtime_error.
void RegisterSnippet(const std::string& languageKey, const std::string& trigger, const std::string& body);

// Exact languageKey match first, then the "" global tier; nullopt when
// neither has the trigger.
[[nodiscard]] std::optional<std::string> SnippetBodyForTrigger(const std::string& languageKey,
                                                               const std::string& trigger);

// Every trigger visible to languageKey (its own tier merged with the ""
// global tier, deduplicated), sorted -- for introspection (ned/snippet-triggers).
[[nodiscard]] std::vector<std::string> SnippetTriggers(const std::string& languageKey);

// Test isolation only (the registry is process-wide static state).
void ClearAllSnippets();

} // namespace ned::editor

#endif // NED_EDITOR_SNIPPETREGISTRY_H
