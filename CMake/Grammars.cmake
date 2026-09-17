# Every bundled grammar's generated tables. Included from the root
# CMakeLists.txt before add_subdirectory(Source), since Source/CMakeLists.txt
# links ned_lib against the per-language targets this file creates. The
# tree-sitter runtime itself is not built anywhere: ned's own engine
# (Source/Editor/Parse/) interprets these tables, and its tests hold it to
# the corpora under Source/Languages/<name>/corpus/.

#--- Bundled tree-sitter grammars -----------------------------------------------
# Every grammar's generated parser.c (+ scanner.c, when it has an external
# scanner) is vendored under ThirdParty/tree-sitter-grammars/ rather than
# fetched via CMake's FetchContent at configure time -- a normal build touches
# no network at all. Tools/vendor-grammars.py is the one place that does: run
# it by hand (its own header has the details) when adding a language or
# bumping a pinned grammar version, and its MANIFEST table is the new source
# of truth for which repository/tag/subdir each grammar comes from -- this
# file only names where the vendored result lives.
function(ned_add_treesitter_grammar_target target_name grammar_dir)
    if (NOT EXISTS "${grammar_dir}/src/parser.c")
        message(FATAL_ERROR "ned_add_treesitter_grammar_target: ${grammar_dir}/src/parser.c not found -- "
                "run Tools/vendor-grammars.py to populate ThirdParty/tree-sitter-grammars/")
    endif()
    add_library(${target_name} STATIC "${grammar_dir}/src/parser.c")
    if (EXISTS "${grammar_dir}/src/scanner.c")
        target_sources(${target_name} PRIVATE "${grammar_dir}/src/scanner.c")
    endif()
    target_include_directories(${target_name} PRIVATE "${grammar_dir}/src")
    set_target_properties(${target_name} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endfunction()

# Convenience wrapper for the common case: a vendored directory whose name is
# exactly the CMake target name (optionally with one subdir inside it, for a
# grammar repo that bundles more than one grammar -- e.g. tree-sitter-php's
# "php" subdir). typescript/tsx and markdown/markdown-inline share one vendored
# directory between two *different*-named targets, so those four go straight
# through ned_add_treesitter_grammar_target instead -- see below.
function(ned_add_bundled_grammar target_name)
    set(grammar_subdir "${ARGN}") # optional trailing positional arg
    set(grammar_dir "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/${target_name}")
    if (grammar_subdir)
        set(grammar_dir "${grammar_dir}/${grammar_subdir}")
    endif()
    ned_add_treesitter_grammar_target(${target_name} "${grammar_dir}")
endfunction()

ned_add_bundled_grammar(tree-sitter-json)


ned_add_bundled_grammar(tree-sitter-c)

# C and C++'s tags are ned's own files (Source/Languages/{c,cpp}/tags.janet),
# not upstream's -- see their own header comments for the "most vexing
# parse" ambiguity that forced the vendoring.

ned_add_bundled_grammar(tree-sitter-cpp)

ned_add_bundled_grammar(tree-sitter-php php)

ned_add_bundled_grammar(tree-sitter-javascript)

ned_add_treesitter_grammar_target(tree-sitter-typescript "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/tree-sitter-typescript-src/typescript")
ned_add_treesitter_grammar_target(tree-sitter-tsx "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/tree-sitter-typescript-src/tsx")

ned_add_bundled_grammar(tree-sitter-html)

ned_add_bundled_grammar(tree-sitter-css)


ned_add_bundled_grammar(tree-sitter-python)

# gutter-symbol-kind follow-up: see PhpTags' own comment above.

ned_add_bundled_grammar(tree-sitter-bash)


ned_add_bundled_grammar(tree-sitter-janet-simple)


ned_add_treesitter_grammar_target(tree-sitter-markdown "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown")

ned_add_treesitter_grammar_target(tree-sitter-markdown-inline "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown-inline")


ned_add_bundled_grammar(tree-sitter-org)

# The fork carries its own queries/injections.scm (the commit pin above) --
# vendored like every upstream query, see Source/Languages/org/upstream/.

ned_add_bundled_grammar(tree-sitter-yaml)


ned_add_bundled_grammar(tree-sitter-toml)


# sogaiu/tree-sitter-clojure (same author as tree-sitter-janet-simple above);
# one grammar serving both ClojureMode and JankMode -- jank is a Clojure
# dialect with no tree-sitter grammar of its own anywhere (checked the
# jank-lang GitHub org and a repo search directly, not assumed). The embedded
# highlight query is ned's own file (Source/Languages/clojure/
# highlights.janet) -- upstream's is 281 bytes (literals/comments only);
# see that file's header comment.
ned_add_bundled_grammar(tree-sitter-clojure)


# ram02z/tree-sitter-fish -- the grammar every real consumer (nvim-treesitter)
# builds on, ships a pre-generated src/parser.c+scanner.c and a real
# queries/highlights.scm, the same bar every other bundled grammar here meets.
ned_add_bundled_grammar(tree-sitter-fish)


# tree-sitter-grammars/tree-sitter-xml -- the community-maintained XML+DTD
# grammar (same org as yaml/toml above), ships a real, well-authored
# queries/xml/highlights.scm covering elements/attributes/entities/CDATA/
# DOCTYPE/processing instructions, not just tags -- unlike tree-sitter-html's
# grammar, which is HTML-tag-specific (a fixed void-element list, embedded
# <script>/<style> injection) and can't parse arbitrary/namespaced XML
# correctly. The repo hosts two grammars (xml/ and dtd/) in one clone, same
# multi-grammar-repo shape as tree-sitter-typescript's typescript/tsx split --
# only the xml/ subdirectory is built, DTD content inside an XML document is
# parsed by the xml grammar's own embedded DTD rules.
ned_add_bundled_grammar(tree-sitter-xml xml)


# go-to-file-at-point resolver gaps follow-up: tree-sitter/tree-sitter-rust,
# the tree-sitter org's own official grammar (same provenance bar as c/cpp/
# python/javascript above) -- ships real queries/highlights.scm and
# queries/tags.scm, consumed unmodified like PHP/JS/Python's own (no
# vendoring needed, unlike C/C++ tags). folds/imports/indents/
# tests are, as with every other bundled language, hand-written locally --
# no upstream grammar repo ships those ned-local capture conventions.
ned_add_bundled_grammar(tree-sitter-rust)


# Go bundled language support (2026-09-06 audit finding): tree-sitter/
# tree-sitter-go, the tree-sitter org's own official grammar (same provenance
# bar as c/cpp/python/javascript/rust above) -- ships real
# queries/highlights.scm and queries/tags.scm, both consumed unmodified (no
# c/cpp-style tags vendoring needed -- checked directly against
# both files, no double-tag/ambiguity issue found the way rust-bundled-
# language's own tags.scm had). folds/indents/tests are, as with every other
# bundled language, hand-written locally -- no upstream grammar repo ships
# those ned-local capture conventions. No go-imports.scm: unlike Rust's
# bodyless "mod foo;" (a single-file declaration a syntax-only query can
# resolve), Go's own `import "some/module/path"` always names a whole
# PACKAGE (a directory), never a single file, and mapping that path to a
# real directory needs go.mod's module-path/replace-directive knowledge a
# syntax-only query has no way to reconstruct -- exactly gopls's own
# textDocument/definition job (go.mod is this language's own RootMarkers
# entry, see RootResolver.cpp). Go's own basename-only file:line
# resolution gap for multi-directory modules is tracked separately under
# ROADMAP.md's Test-runner gaps.
ned_add_bundled_grammar(tree-sitter-go)


# C# bundled language support (raised 2026-09-06 -- no system tree-sitter-
# csharp install available; the same fix as Java/Kotlin's own ROADMAP entry
# applies here too: a system-installed grammar .so never carries queries/*.scm
# regardless, so a system package wouldn't have saved the real work anyway.
# Every bundled grammar in this file is vendored under ThirdParty/ (see
# Tools/vendor-grammars.py), never a system package -- this is the existing,
# already-general policy, not a new one). tree-sitter/tree-sitter-c-sharp,
# the tree-sitter org's own official
# grammar (same provenance bar as c/cpp/python/javascript/rust/go above) --
# ships real queries/highlights.scm and queries/tags.scm, both consumed
# unmodified (checked directly: class/interface/method are distinct node
# types with no overlapping match, so no double-tag dedup risk the way
# rust-bundled-language's own tags.scm had). folds/indents/tests are, as with
# every other bundled language, hand-written locally. No csharp-imports.scm:
# a `using Some.Namespace;` directive names a whole NAMESPACE, potentially
# spanning many files across several assemblies, never a single file -- the
# same "package/namespace path, not a file path" reasoning go-imports.scm's
# own absence documents for Go. `csproj`/`sln`'s own lack of a fixed
# filename (unlike every other bundled language's root marker) needed a real
# RootResolver.cpp change -- see MarkerExistsInDirectory's own comment
# there for the "*.<ext>" glob-marker mechanism this language is the first
# to need.
ned_add_bundled_grammar(tree-sitter-c-sharp)

# Java & Kotlin bundled language support (raised 2026-09-03 -- user wants
# broad, general-purpose Java support with Kotlin alongside it, and Android
# development not to be painful). A system-installed libtree-sitter-java.so
# is present on this machine but is not a shortcut: a compiled grammar .so
# never carries queries/*.scm, which live as separate text files in the
# grammar's own repo -- the same reasoning csharp/go's own comments above
# already record, and the reason every bundled grammar in this file is
# vendored under ThirdParty/ rather than taken from a system package.
#
# tree-sitter/tree-sitter-java is the tree-sitter org's own official grammar
# (same provenance bar as c/cpp/python/javascript/rust/go/c-sharp above) and
# ships a real queries/highlights.scm and queries/tags.scm, both consumed
# unmodified (checked directly against both files: class/interface/method
# declarations are distinct node types with no overlapping @definition match,
# so no C/C++-style tags vendoring is needed). folds/indents/
# tests are hand-written locally as they are for every bundled language.
# No java-imports.scm: `import some.package.Class;` names a class within a
# PACKAGE whose on-disk location depends on the build's own source roots and
# classpath (Maven's src/main/java, Gradle's sourceSets, a jar) -- knowledge
# a syntax-only query has no way to reconstruct, the same "package path, not
# a file path" reasoning go-imports.scm's and csharp-imports.scm's own
# absences document. That is jdtls's textDocument/definition job.
ned_add_bundled_grammar(tree-sitter-java)

# class-file-sync follow-up: upstream plus ned's delta (enum/record) -- see
# Source/Languages/java/tags.janet's own header.

# Kotlin is the one bundled language with no obvious single upstream, so the
# choice is recorded rather than left implicit. Two real candidates exist and
# both are effectively dormant: fwcd/tree-sitter-kotlin (0.3.8, last commit
# Aug 2024 -- the original, and what nvim-treesitter tracked for years) and
# tree-sitter-grammars/tree-sitter-kotlin (v1.1.0, last commit Jan 2025 -- a
# fork under the same community org that supplies this file's markdown/yaml/
# toml/xml grammars, which would otherwise be the better provenance bar).
# fwcd wins on the one thing that actually decides it: it ships a real
# queries/highlights.scm and the fork ships no queries at all, and the fork
# is not a drop-in for the original's -- diffing the two node-types.json
# files shows ~50 renamed node types on each side (simple_identifier,
# multiline_comment and the whole *_expression family on one side vs.
# binary_expression/block/block_comment on the other), so pairing the fork
# with fwcd's query would silently highlight almost nothing. Taking the fork
# instead would mean hand-writing a ~380-line highlights query against a
# grammar nobody upstream writes queries for. Revisit if either repo wakes
# up. Its highlights.scm is itself derived from nvim-treesitter's (stated in
# that file's own header), with the #lua-match?-based patterns already
# removed upstream -- which matters here, since Query.h treats an
# unrecognized predicate as inert rather than as a filter, so a surviving
# #lua-match? would over-match rather than fail loudly.
#
# No upstream tags.scm either, so kotlin/tags.janet is ned-authored -- the only
# bundled language whose symbol-kind query is hand-written for absence rather
# than (like C/C++'s tags) to correct an ambiguous upstream one.
ned_add_bundled_grammar(tree-sitter-kotlin)

# Release-0.6 grammar batch (admission policy: org repo over personal fork,
# pinned by tag; ABI/scanner/corpus facts recorded at admission in
# Docs/LanguageCoverage.md).
ned_add_bundled_grammar(tree-sitter-lua)
ned_add_bundled_grammar(tree-sitter-cmake)
ned_add_bundled_grammar(tree-sitter-diff)

# SQL (D0 core only -- see ROADMAP's parsing-engine section for the deferred
# per-dialect trait-delta work). DerekStride/tree-sitter-sql v0.3.11, ABI 15,
# 188-LOC scanner, 31-file corpus -- but its tags/main don't commit generated
# parser.c/grammar.json (only grammar.js/scanner.c; generated output lives on
# a gh-pages branch or, more traceably, as a release asset). Tools/vendor-
# grammars.py pulls the release tarball for src/; its corpus, which the
# tarball omits, was imported into Source/Languages/sql/corpus/ like every
# other language's.
ned_add_bundled_grammar(tree-sitter-sql)

# 2026-09-13 batch: Dockerfile, Make, HCL, Nix, Ruby, gitcommit, gitrebase
# (admission policy: org repo over personal fork where one exists, pinned by
# tag; ABI/scanner/corpus facts recorded at admission in
# Docs/LanguageCoverage.md). HCL's upstream repo splits core HCL (root src/)
# from a separate Terraform-dialect grammar (dialects/terraform); this fetches
# core only, matching SQL's "one core, dialect deltas deferred" precedent --
# no subdir argument, so ned_add_bundled_grammar reads
# ThirdParty/tree-sitter-grammars/tree-sitter-hcl/src unmodified. Zig was researched and
# deliberately NOT admitted here: its only actively maintained grammar
# (tree-sitter-grammars/tree-sitter-zig) ships no test/corpus at all, failing
# admission policy item 8, and the one alternative with real history
# (maxxnino/tree-sitter-zig) is archived -- see Docs/LanguageCoverage.md's
# Tier D entry for the revisit trigger.
ned_add_bundled_grammar(tree-sitter-dockerfile)
ned_add_bundled_grammar(tree-sitter-make)
ned_add_bundled_grammar(tree-sitter-hcl)
ned_add_bundled_grammar(tree-sitter-nix)
ned_add_bundled_grammar(tree-sitter-ruby)
ned_add_bundled_grammar(tree-sitter-gitcommit)
ned_add_bundled_grammar(tree-sitter-gitrebase)

# R (Tier B, dynamic/scripting -- see Docs/LanguageCoverage.md). r-lib/tree-sitter-r
# v1.3.0 (the posit/RStudio-maintained official grammar, 155 stars, pushed
# 2026-06-22), ABI 14, 31KB scanner, 4-file corpus. Unlike SQL/perl this repo commits
# generated parser.c/node-types.json directly on its tag, so it takes the ordinary
# ned_add_treesitter_grammar path with no release-tarball fetch. Upstream ships
# highlights.scm, tags.scm and locals.scm; highlights+tags are vendored for D0
# highlighting and D1 symbol gutter/go-to-definition, locals.scm deliberately not --
# scope-aware rename's query set stays a closed, individually-vetted list rather than
# growing by default whenever an upstream file happens to exist (see LocalScopes.h's
# own doc comment in CLAUDE.md).
ned_add_bundled_grammar(tree-sitter-r)

#------------------------------------------------------------------------------
