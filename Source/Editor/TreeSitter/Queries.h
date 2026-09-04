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
extern const char* const kHtmlInjections; // Injection.h's generic engine -- <script>/<style> -> javascript/css
extern const char* const kCss;
extern const char* const kPython;
extern const char* const kBash;
extern const char* const kJanet;
extern const char* const kMarkdown;
extern const char* const kMarkdownInline; // Injection.h's tier-2 markdown-inline resolution -- see Injection.cpp
// Injection.h's generic engine -- fenced code blocks, inline formatting,
// html_block, and frontmatter, superseding Mode.cpp's own former hand-rolled
// CollectMarkdownFencedCodeSpans/CollectMarkdownInlineSpans.
extern const char* const kMarkdownInjections;
// Org-mode syntax-highlighting follow-up: hand-written against Ned's own
// forked grammar (Source/Editor/TreeSitter/OrgHighlights.scm), not fetched
// from any repository -- see that file's own header comment for why.
extern const char* const kOrg;
// Injection.h's generic engine -- #+BEGIN_SRC/#+BEGIN_EXPORT block bodies.
// A real injections.scm in Ned's own tree-sitter-ned-org fork itself
// (CMakeLists.txt's ned_add_treesitter_grammar(tree-sitter-org ...) pin),
// not vendored locally -- see that file's own header comment.
extern const char* const kOrgInjections;
extern const char* const kYaml;
extern const char* const kToml;
// Vendored from nvim-treesitter rather than sogaiu/tree-sitter-clojure's own
// queries/highlights.scm (281 bytes -- literals and comments only); shared by
// both ClojureMode and JankMode, mirroring kTypeScript's sharing above -- see
// queries/clojure.scm's own header comment.
extern const char* const kClojure;
extern const char* const kFish;
extern const char* const kXml;
extern const char* const kRust; // tree-sitter/tree-sitter-rust's own real queries/highlights.scm, unmodified

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
extern const char* const kRustFolds;

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
// go-to-file-at-point resolver gaps follow-up: Rust's own bodyless "mod
// foo;" file-per-module declaration only -- see rust-imports.scm's own
// header comment for why a real "use" path isn't matched here at all.
extern const char* const kRustImports;

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
extern const char* const kRustTags; // tree-sitter/tree-sitter-rust's own real queries/tags.scm, unmodified

// test-runner integration: repo-local test-discovery queries
// (Source/Editor/TreeSitter/queries/*-tests.scm) using the ned-local
// "@test.definition"/"@test.name" capture convention -- see
// Mode::testDiscovery's doc comment in Mode.h and cpp-tests.scm's own
// header comment (no upstream tests.scm convention exists to vendor).
// Only languages with a bundled mode *and* a mainstream test framework
// whose definitions are query-recognizable get one: C++ (Catch2/gtest),
// Python (pytest/unittest), JavaScript/TypeScript (jest/vitest/mocha --
// kTypeScriptTests shared by TsxMode, the standing sharing convention),
// PHP (PHPUnit), Rust (`#[test]`/`#[<framework>::test]`, kRustTests). Go has
// no bundled mode at all (its test *output* still parses -- see
// Editor/TestRun/TestOutputParser.h -- only discovery is absent); C has no
// dominant query-recognizable framework convention.
extern const char* const kCppTests;
extern const char* const kPhpTests;
extern const char* const kJavaScriptTests;
extern const char* const kTypeScriptTests;
extern const char* const kPythonTests;
extern const char* const kRustTests; // #[test]/#[<framework>::test], rust-tests.scm's own header comment

// smart-indentation follow-up: hand-written "indent"/"dedent" queries, one
// per in-scope language (Source/Editor/TreeSitter/queries/*-indents.scm),
// borrowing nvim-treesitter/Helix's own capture-NAME convention only -- no
// #set!-based priority/scope directives, since Query::Captures() never
// evaluates those (see Query.h's own doc comment). Proven against three
// deliberately different indent models first (Editor/Indent.h's own header
// comment): the bracket tier here (C/C++/JSON); the indentation-sensitive
// tier (Python, reusing tree-sitter-python's own block-node scoping, no
// bespoke code needed); Markdown's own hand-rolled closure (MarkdownMode())
// covers the prose/structural tier without a query file at all. Every other
// bundled grammar is a documented, unscoped-for-now follow-up (ROADMAP.md) --
// their Mode::indentColumn simply stays empty, same "empty means not
// configured" convention as fold/importTarget/symbolKind/testDiscovery.
extern const char* const kCIndents;
extern const char* const kCppIndents;
extern const char* const kJsonIndents;
extern const char* const kPythonIndents;
// bundle-remaining-indents follow-up: the rest of the bundled grammars, each
// a fast, mechanical addition once the engine itself was proven above -- no
// engine changes, just a new query file per language (see each *-indents.scm
// file's own header comment). kTypeScriptIndents is shared by TypeScriptMode
// and TsxMode; kClojureIndents is shared by ClojureMode and JankMode -- same
// sharing every other query constant in this file already uses. Janet/
// Clojure are deliberately bracket-depth only, not real per-form Lisp
// indent -- see janet-indents.scm's own header comment.
extern const char* const kJavaScriptIndents;
extern const char* const kTypeScriptIndents;
extern const char* const kPhpIndents;
extern const char* const kCssIndents;
extern const char* const kHtmlIndents;
extern const char* const kXmlIndents;
extern const char* const kBashIndents;
extern const char* const kFishIndents;
extern const char* const kJanetIndents;
extern const char* const kClojureIndents;
extern const char* const kYamlIndents;
extern const char* const kTomlIndents;
extern const char* const kRustIndents;

} // namespace ned::editor::treesitter::queries

#endif // NED_EDITOR_TREESITTER_QUERIES_H
