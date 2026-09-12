#include "Editor/BundledSnippets.h"

#include "Editor/BundledLanguages.h"
#include "Editor/SnippetRegistry.h"

namespace ned::editor {

void RegisterBundledSnippets() {
    // Each language's own definition carries its snippets (language.janet's
    // :snippets) -- which languages have any, and why the rest don't, is a
    // per-language statement there rather than a table here.
    for (const LanguageDefinition& definition : BundledLanguages()) {
        for (const auto& [trigger, body] : definition.snippets) {
            RegisterSnippet(definition.name, trigger, body);
        }
    }
}

} // namespace ned::editor
