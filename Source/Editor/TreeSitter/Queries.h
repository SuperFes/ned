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
extern const char* const kRust;   // tree-sitter/tree-sitter-rust's own real queries/highlights.scm, unmodified
extern const char* const kGo;     // tree-sitter/tree-sitter-go's own real queries/highlights.scm, unmodified
extern const char* const kCSharp; // tree-sitter/tree-sitter-c-sharp's own real queries/highlights.scm, unmodified
extern const char* const kJava;   // tree-sitter/tree-sitter-java's own real queries/highlights.scm, unmodified
// fwcd/tree-sitter-kotlin's own real queries/highlights.scm, unmodified -- itself
// derived from nvim-treesitter's, with the #lua-match? patterns already removed
// upstream (Query.h treats an unrecognized predicate as inert, so one that
// survived would over-match rather than fail loudly). See CMakeLists.txt for why
// this repo rather than the tree-sitter-grammars fork.
extern const char* const kKotlin;

// No fold queries are declared here, and that is the point.
//
// There were eleven `*-folds.scm` files, one per bracket language, and every
// one of them is gone. `Editor/ImprintFold.h` derives the same folds from the
// delimiter imprint the grammar itself declares, so nothing had to be
// authored per language and nothing downstream could tell the difference:
// measured over 66 real files across those eleven languages, the queries
// produced ZERO fold ranges the imprint did not. See ROADMAP.md and
// `Tests/ImprintTest.cpp`, which keeps the deleted node lists as a pin so a
// grammar renaming `compound_statement` still fails the build.
//
// `TreeSitterQuerySources::folds` itself stays: a runtime-`dlopen`'d grammar
// has no imprint table compiled in, so `RegisterDynamicMode`'s discovered
// `folds.scm` remains its only fold source.

// import-target-tree-sitter follow-up: hand-written "@import.target"/
// "@import.module"/"@import.statement" queries, one per in-scope language
// (Source/Editor/TreeSitter/queries/*-imports.scm) -- no upstream grammar
// repo or nvim-treesitter/Neovim-core query set ships one of these for any
// language (same "checked directly, not assumed" convention the now-deleted
// fold queries above established). kCImports is shared by CMode and
// CppMode; kTypeScriptImports is shared by TypeScriptMode and TsxMode;
// kClojureImports is shared by ClojureMode and JankMode -- same sharing each
// language's own highlight query already uses. Languages with no
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
// Go bundled language support follow-up: deliberately no kGoImports at all --
// unlike Rust's bodyless "mod foo;" (a single-file declaration), Go's own
// `import "some/module/path"` always names a whole PACKAGE (a directory),
// never a single file, and resolving that path needs go.mod's module-path/
// replace-directive knowledge a syntax-only query has no way to reconstruct;
// see CMakeLists.txt's own comment beside ned_add_treesitter_grammar(
// tree-sitter-go ...) for the full reasoning. GoMode::importTarget stays
// empty, same "not configured" convention every other optional Mode
// capability already uses. Deliberately no kCSharpImports either, same
// reasoning: a `using Some.Namespace;` directive names a whole namespace,
// not a file.

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
// TsxMode, same sharing kTypeScript/kTypeScriptImports
// already use. kCTags/kCppTags are the one exception to "unmodified" --
// upstream's own @definition.function pattern is ambiguous with C/C++'s
// "most vexing parse" (confirmed live against a real false positive, a local
// variable declared with constructor-call-style syntax getting the function
// glyph -- see BufferViewSymbolGutterTest.cpp), so these two are repo-local
// vendored files (Source/Editor/TreeSitter/queries/c-tags.scm/cpp-tags.scm)
// instead of the grammar's own; see c-tags.scm's own header comment for the
// full story.
//
// class-file-sync follow-up: four of these are now a CONCATENATION rather
// than one file (CMakeLists.txt's ned_embed_treesitter_query_concat) --
// upstream's own tags.scm still consumed whole, with a small repo-local
// delta appended. Vendoring a whole corrected copy would have worked too and
// was rejected: it stops inheriting upstream's own fixes on the next grammar
// bump, which is the entire reason these are fetched rather than written.
// Each delta file's own header says what it adds and why; the short version:
// PHP/Java/C# predate language constructs that are ordinary today (8.1
// enums, records, structs, file-scoped namespaces), and TypeScript's
// upstream file is a delta on JavaScript's rather than a standalone query --
// it carries no class_declaration at all, so kTypeScriptTags embedded alone
// (as it was) left every TypeScript class and function unmarked.
//
extern const char* const kCTags;
extern const char* const kCppTags;
extern const char* const kPhpTags; // upstream + queries/php-tags.scm (enum)
extern const char* const kJavaScriptTags;
extern const char* const kTypeScriptTags; // javascript's + typescript's + queries/typescript-tags.scm
extern const char* const kPythonTags;
extern const char* const kRustTags;   // tree-sitter/tree-sitter-rust's own real queries/tags.scm, unmodified
extern const char* const kGoTags;     // tree-sitter/tree-sitter-go's own real queries/tags.scm, unmodified
extern const char* const kCSharpTags; // upstream + queries/csharp-tags.scm (enum/struct/record/file-scoped namespace)
extern const char* const kJavaTags;   // upstream + queries/java-tags.scm (enum/record)
// Repo-local (Source/Editor/TreeSitter/queries/kotlin-tags.scm) -- fwcd/
// tree-sitter-kotlin ships no tags.scm at all, so unlike kCTags/kCppTags (which
// exist to correct an ambiguous upstream file) this one substitutes for an
// absent one.
extern const char* const kKotlinTags;

// test-runner integration: repo-local test-discovery queries
// (Source/Editor/TreeSitter/queries/*-tests.scm) using the ned-local
// "@test.definition"/"@test.name" capture convention -- see
// Mode::testDiscovery's doc comment in Mode.h and cpp-tests.scm's own
// header comment (no upstream tests.scm convention exists to vendor).
// Only languages with a bundled mode *and* a mainstream test framework
// whose definitions are query-recognizable get one: C++ (Catch2/gtest),
// Python (pytest/unittest), JavaScript/TypeScript (jest/vitest/mocha --
// kTypeScriptTests shared by TsxMode, the standing sharing convention),
// PHP (PHPUnit), Rust (`#[test]`/`#[<framework>::test]`, kRustTests), Go
// (`func TestXxx(t *testing.T)`/Benchmark/Fuzz/Example, kGoTests). C has no
// dominant query-recognizable framework convention.
extern const char* const kCppTests;
extern const char* const kPhpTests;
extern const char* const kJavaScriptTests;
extern const char* const kTypeScriptTests;
extern const char* const kPythonTests;
extern const char* const kRustTests;   // #[test]/#[<framework>::test], rust-tests.scm's own header comment
extern const char* const kGoTests;     // TestXxx/BenchmarkXxx/FuzzXxx/ExampleXxx, go-tests.scm's own header comment
extern const char* const kCSharpTests; // [Fact]/[Theory]/[Test]/[TestMethod]/etc, csharp-tests.scm's own header comment
extern const char* const kJavaTests;   // JUnit 4/5's @Test and its variants, java-tests.scm's own header comment
// kotlin.test is a facade over JUnit on the JVM, so KotlinTests matches the same
// annotation set JavaTests does -- see kotlin-tests.scm's own header comment.
extern const char* const kKotlinTests;

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
extern const char* const kPythonIndents;
// bundle-remaining-indents follow-up: the rest of the bundled grammars, each
// a fast, mechanical addition once the engine itself was proven above -- no
// engine changes, just a new query file per language (see each *-indents.scm
// file's own header comment). kClojureIndents is shared by ClojureMode and
// JankMode -- same sharing every other query constant in this file already
// uses. kTypeScriptIndents was shared with TsxMode the same way until JSX
// needed indent rules: the tsx dialect's parser knows `jsx_element` and the
// typescript dialect's does not, and a query naming an unknown node type
// fails to compile, so kTsxIndents is its own constant. Janet/
// Clojure are deliberately bracket-depth only, not real per-form Lisp
// indent -- see janet-indents.scm's own header comment.
extern const char* const kJavaScriptIndents;
extern const char* const kTypeScriptIndents;
extern const char* const kTsxIndents;
extern const char* const kHtmlIndents;
extern const char* const kXmlIndents;
extern const char* const kBashIndents;
extern const char* const kFishIndents;
extern const char* const kJanetIndents;
extern const char* const kClojureIndents;
extern const char* const kYamlIndents;
extern const char* const kRustIndents;
extern const char* const kGoIndents;
extern const char* const kCSharpIndents;
extern const char* const kJavaIndents;
extern const char* const kKotlinIndents;

// scope-aware-rename follow-up: hand-written "@local.scope"/
// "@local.definition*"/"@local.reference" queries, one per language whose
// scoping is structural enough for a query to express
// (Source/Editor/TreeSitter/queries/*-locals.scm). Unlike the indents/tests
// queries beside them, this capture vocabulary IS an upstream tree-sitter/
// Neovim convention -- but the queries themselves are ned's own: the two
// bundled grammars that ship a locals.scm at all (javascript, typescript)
// ship four patterns and two patterns respectively, neither enough to
// resolve a binding. See c-locals.scm's header for the three rules every
// file here follows, and Editor/LocalScopes.h for what consumes them.
// kTypeScriptLocals is shared by TypeScriptMode and TsxMode, and
// kClojureLocals by ClojureMode and JankMode -- the same sharing every other
// query constant for those pairs already uses.
//
// The languages with no locals query at all are a deliberate list, not a
// backlog: json, yaml, toml and xml have no binding construct to resolve,
// and html and css have one whose scoping is not lexical -- a CSS custom
// property is scoped to matching elements AND THEIR DESCENDANTS, which is
// DOM containment, so the byte containment this model resolves by would
// produce a rename that silently missed every descendant use.
extern const char* const kCLocals;
extern const char* const kCppLocals;
extern const char* const kPythonLocals;
extern const char* const kJavaScriptLocals;
extern const char* const kTypeScriptLocals;
extern const char* const kRustLocals;
extern const char* const kGoLocals;
extern const char* const kJavaLocals;
extern const char* const kCSharpLocals;
extern const char* const kKotlinLocals;
extern const char* const kPhpLocals;
extern const char* const kBashLocals;
extern const char* const kFishLocals;
extern const char* const kJanetLocals;
extern const char* const kClojureLocals;

} // namespace ned::editor::treesitter::queries

#endif // NED_EDITOR_TREESITTER_QUERIES_H
