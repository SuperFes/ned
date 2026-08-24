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
extern const char* const kFish;

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

// import-target-tree-sitter follow-up: hand-written "@import.target"/
// "@import.module"/"@import.statement" queries, one per in-scope language
// (Source/Editor/TreeSitter/queries/*-imports.scm) -- no upstream grammar
// repo or nvim-treesitter/Neovim-core query set ships one of these for any
// language (same "checked directly, not assumed" convention the fold
// queries above already established). kCImports is shared by CMode and
// CppMode; kTypeScriptImports is shared by TypeScriptMode and TsxMode;
// kClojureImports is shared by ClojureMode and JankMode -- same sharing each
// language's own highlight/fold query already uses. Languages with no
// import query (JSON/HTML/YAML/TOML/Markdown/Org/fundamental-mode -- no real
// per-language import *statement* to key off of, or already otherwise
// covered, see ROADMAP.md) have no corresponding constant here; their
// Mode::importTarget simply stays empty.
extern const char* const kCImports;
extern const char* const kPhpImports;
extern const char* const kJavaScriptImports;
extern const char* const kTypeScriptImports;
extern const char* const kPythonImports;
extern const char* const kBashImports;
extern const char* const kCssImports;
extern const char* const kClojureImports;
extern const char* const kJanetImports;

// gutter-symbol-kind follow-up: each bundled grammar's own real
// queries/tags.scm, consumed directly and unmodified -- the ctags/
// nvim-treesitter "@definition.function"/"@definition.class"/etc. convention
// (see Mode.h's SymbolKindFromCaptureName for the capture-name -> SymbolKind
// mapping). Only languages whose upstream grammar repo actually ships a
// tags.scm get a constant here (checked directly against the fetched
// sources, not assumed) -- JSON/HTML/CSS/YAML/TOML/Bash/Janet/Clojure/
// Markdown/Org have no meaningful "function/class definition" concept, or
// their grammar simply doesn't ship one; their Mode::symbolKind stays empty,
// same "empty means not configured" convention every other optional Mode
// capability already uses. kTypeScriptTags is shared by TypeScriptMode and
// TsxMode, same sharing kTypeScript/kTypeScriptFolds/kTypeScriptImports
// already use. kCTags/kCppTags are the one exception to "unmodified" --
// upstream's own @definition.function pattern is ambiguous with C/C++'s
// "most vexing parse" (confirmed live against a real false positive, a local
// variable declared with constructor-call-style syntax getting the function
// glyph -- see BufferViewSymbolGutterTest.cpp), so these two are repo-local
// vendored files (Source/Editor/TreeSitter/queries/c-tags.scm/cpp-tags.scm)
// instead of the grammar's own; see c-tags.scm's own header comment for the
// full story.
extern const char* const kCTags;
extern const char* const kCppTags;
extern const char* const kPhpTags;
extern const char* const kJavaScriptTags;
extern const char* const kTypeScriptTags;
extern const char* const kPythonTags;

// test-runner integration: repo-local test-discovery queries
// (Source/Editor/TreeSitter/queries/*-tests.scm) using the ned-local
// "@test.definition"/"@test.name" capture convention -- see
// Mode::testDiscovery's doc comment in Mode.h and cpp-tests.scm's own
// header comment (no upstream tests.scm convention exists to vendor).
// Only languages with a bundled mode *and* a mainstream test framework
// whose definitions are query-recognizable get one: C++ (Catch2/gtest),
// Python (pytest/unittest), JavaScript/TypeScript (jest/vitest/mocha --
// kTypeScriptTests shared by TsxMode, the standing sharing convention),
// PHP (PHPUnit). Go/Rust have no bundled mode at all (their test *output*
// still parses -- see Editor/TestRun/TestOutputParser.h -- only discovery
// is absent); C has no dominant query-recognizable framework convention.
extern const char* const kCppTests;
extern const char* const kPhpTests;
extern const char* const kJavaScriptTests;
extern const char* const kTypeScriptTests;
extern const char* const kPythonTests;

} // namespace ned::editor::treesitter::queries

#endif // NED_EDITOR_TREESITTER_QUERIES_H
