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
extern const char* const kMarkdownInline; // MarkdownMode()'s own hand-rolled injection pass -- see Mode.cpp
// Org-mode syntax-highlighting follow-up: hand-written against Ned's own
// forked grammar (Source/Editor/TreeSitter/OrgHighlights.scm), not fetched
// from any repository -- see that file's own header comment for why.
extern const char* const kOrg;
extern const char* const kYaml;
extern const char* const kToml;
// Vendored from nvim-treesitter rather than sogaiu/tree-sitter-clojure's own
// queries/highlights.scm (281 bytes -- literals and comments only); shared by
// both ClojureMode and JankMode, mirroring kTypeScript's sharing above -- see
// queries/clojure.scm's own header comment.
extern const char* const kClojure;

// generic-code-folding follow-up: hand-written "@fold" queries, one per
// in-scope language (Source/Editor/TreeSitter/queries/*-folds.scm) -- no
// upstream grammar repo or nvim-treesitter/Neovim-core query set ships a
// folds.scm for any of these (checked directly, not assumed; see
// ROADMAP.md). kTypeScriptFolds is shared by TypeScriptMode and TsxMode,
// mirroring kTypeScript's own sharing above. Languages with no fold query
// yet (PHP/HTML/CSS/Bash/Janet/Markdown) have no corresponding constant
// here -- their Mode::fold simply stays empty.
extern const char* const kCFolds;
extern const char* const kCppFolds;
extern const char* const kJsonFolds;
extern const char* const kPythonFolds;
extern const char* const kJavaScriptFolds;
extern const char* const kTypeScriptFolds;
extern const char* const kClojureFolds; // shared by ClojureMode and JankMode, same as kClojure above

} // namespace ned::editor::treesitter::queries

#endif // NED_EDITOR_TREESITTER_QUERIES_H
