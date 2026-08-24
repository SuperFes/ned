//
// The snippet registry: trigger word -> snippet body (see Snippet.h for the
// body syntax), keyed by language key -- the same user-supplied string
// ned/set-lsp-command keys on ("cpp", "python", ...; LanguageKeyForMode in
// Mode.h derives it from a Mode's name on the lookup side), with "" meaning
// "every mode". Process-wide, mutex-guarded (OrgCapture.h's exact registry
// pattern); Janet-only surface (ned/register-snippet) -- no bundled
// defaults, matching the LSP/DAP/task-config "nothing bundled or
// auto-detected" posture.
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
