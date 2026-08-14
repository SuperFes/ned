//
// Highlight query text for each bundled grammar (bundle-remaining-grammars
// follow-up), embedded into the binary at CMake configure time from each
// grammar's own real queries/highlights.scm -- see
// CMake/EmbeddedTreeSitterQuery.cpp.in and CMakeLists.txt's
// ned_embed_treesitter_query calls for how. Consuming each community-
// maintained query file directly, rather than hand-writing highlight rules
// per language, is the whole point of this follow-up.
//

#ifndef NED_EDITOR_TREESITTER_QUERIES_H
#define NED_EDITOR_TREESITTER_QUERIES_H

namespace ned::editor::treesitter::queries {

extern const char* const kJson;
extern const char* const kC;
extern const char* const kCpp;
extern const char* const kPhp;
extern const char* const kJavaScript;
extern const char* const kTypeScript; // shared by both TypeScriptMode and TsxMode -- see CMakeLists.txt's own note
extern const char* const kHtml;
extern const char* const kCss;
extern const char* const kPython;
extern const char* const kBash;
extern const char* const kJanet;
extern const char* const kMarkdown;

} // namespace ned::editor::treesitter::queries

#endif // NED_EDITOR_TREESITTER_QUERIES_H
