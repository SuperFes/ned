cmake_minimum_required(VERSION 3.29)
project(Ned VERSION 0.5.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# project(Ned VERSION ...) above is the single source of truth for ned's
# version; this is what makes it reachable from C++ (`ned --version`). Without
# it the CMake version was metadata nothing consumed, which is how it came to
# read 0.5.0 while the only git tag read v0.1.0 and the binary could report
# neither. Generated into ned_generated/ alongside the embedded-query sources,
# and added to ned_lib's PUBLIC include path further down so the `ned`
# executable picks it up through the link. configure_file (rather than the
# file(WRITE) the query embedders use) leaves the output's timestamp alone when
# nothing changed, so a reconfigure doesn't rebuild every translation unit that
# includes it.
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Version.h.in"
        "${CMAKE_BINARY_DIR}/ned_generated/NedVersion.h"
        @ONLY)

option(NED_BUILD_TESTS "Build the ned_tests target" ON)
option(NED_ENABLE_SANITIZERS "Build with AddressSanitizer/UndefinedBehaviorSanitizer" OFF)
option(NED_TEST_TERMINAL_INTEGRATION "Include PtyProcessTest.cpp, which forkpty's real shells/processes, in ned_tests" OFF)

# Use ccache automatically when it's on PATH -- this project's bundled
# tree-sitter grammars include some very large generated parser.c
# translation units (tree-sitter-cpp's alone is ~17MB of generated tables),
# and ccache is what keeps a clean rebuild from recompiling them every time.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

include(FindPkgConfig)

# System Notcurses (Gentoo's dev-cpp/notcurses) rather than FetchContent: it
# now matches the exact pinned tag (v3.0.17) and its ebuild carries the same
# three input-handling patches this project used to apply itself at
# FetchContent-patch time -- legacy-terminal Ctrl+Space (a raw NUL byte
# otherwise swallowed, id 0 colliding with notcurses_get's "no input" return),
# wheel-right (SGR Cb=67, same swallowing), and bracketed-paste marker
# recognition (\x1b[200~/\x1b[201~ as two new NCKEY_* keys, which Notcurses
# has no native support for otherwise). CMake/PatchNotcurses*.cmake are kept,
# unused, as the exact record of what a notcurses upgrade -- system package or
# otherwise -- needs to keep providing; see each one's own header comment.
# Pinned to notcurses-core specifically, matching this project's previous
# from-source USE_MULTIMEDIA=none/USE_DOCTEST=OFF/etc. trim -- notcurses-core.pc
# already excludes the multimedia/doctest/ffi extras those forced off.
# real-pixel-graphics-investigation follow-up: this project was once bitten by
# a real ABI mismatch against a stale system notcurses (enum member order in
# ncblitter_e changed between 3.0.8 and the version this project pins), which
# is exactly why the tag match above matters and isn't a formality.
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses-core)
add_library(notcurses-core ALIAS PkgConfig::NOTCURSES)
#------------------------------------------------------------------------------

#--- System utf8proc -----------------------------------------------------------
pkg_check_modules(UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
add_library(utf8proc ALIAS PkgConfig::UTF8PROC)
#------------------------------------------------------------------------------

#--- CLI11 (command-line parameter handling follow-up) ------------------------
# System package (Gentoo's dev-cpp/cli11) ships a real CLI11Config.cmake, so
# find_package hands back the same CLI11::CLI11 target FetchContent used to.
find_package(CLI11 REQUIRED)
#------------------------------------------------------------------------------

#--- nlohmann/json (LSP client follow-up) --------------------------------------
# Same as CLI11 above: the system package ships nlohmann_jsonConfig.cmake,
# so find_package hands back the same nlohmann_json::nlohmann_json target.
find_package(nlohmann_json REQUIRED)
#------------------------------------------------------------------------------

#--- RE2 (internal-project-search follow-up) ------------------------------------
# The regex engine behind the internal, multi-threaded project-search backend
# that replaces the `rg` shell-out (Editor/Project/Search.cpp) -- linear-time,
# no-backtracking matching, the same engine-model philosophy `rg` itself uses.
# This used to pin FetchContent to the last tag before RE2's CMake build
# started hard-requiring Abseil, specifically to avoid pulling all of Abseil
# in as source just for regex matching. That tradeoff doesn't apply to a
# system package: Gentoo's dev-libs/re2 already depends on a prebuilt
# dev-cpp/abseil-cpp, so find_package costs nothing extra here and hands back
# the same re2::re2 target the FetchContent build produced.
find_package(re2 REQUIRED)
#------------------------------------------------------------------------------

#--- PCRE2 (in-file-regex follow-up) -------------------------------------------
# The regex engine behind the two user-facing "type your own regex" surfaces
# that edit buffer/file content: query-replace-regexp (Editor/QueryReplace.cpp)
# and project-replace's rewrite step (Editor/ProjectReplace.cpp), both through
# Editor/RegexPattern.h. Deliberately a second engine alongside RE2: RE2's
# linear-time model is right for project *search* (untrusted-ish patterns fanned
# out across every file) but can't do lookaround or backreferences at all --
# PCRE2 provides those plus named groups and real Unicode classes, with JIT for
# speed and a match limit as the backtracking safety net (see RegexPattern.cpp).
# System package (Gentoo's dev-libs/libpcre2) ships no CMake config, only
# pkg-config; confirmed its libpcre2-8.so still exports the pcre2_jit_* symbols
# RegexPattern.cpp relies on. Aliased to the same pcre2-8-static name the
# FetchContent build produced so the link line below needs no change.
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
add_library(pcre2-8-static ALIAS PkgConfig::PCRE2)
#------------------------------------------------------------------------------

#--- tree-sitter (core) ---------------------------------------------------------
# Only ned_tests links this (see target_link_libraries(ned_tests ...) below) --
# it is Phase 4b's differential conformance reference for ned's own parsing
# engine (Source/Editor/Parse/), never linked into ned_lib/ned itself. Used to
# be FetchContent'd at configure time (v0.25.10 pinned); now vendored under
# ThirdParty/tree-sitter-grammars/tree-sitter-core/ by Tools/vendor-grammars.py
# -- see that script's header for why grammar sources moved out of
# FetchContent entirely. Re-vendor with
# `Tools/vendor-grammars.py tree-sitter-core` to bump the pinned version.
set(NED_TREE_SITTER_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tree-sitter-grammars/tree-sitter-core")
file(GLOB NED_TREE_SITTER_CORE_SOURCES "${NED_TREE_SITTER_CORE_DIR}/src/*.c")
add_library(tree-sitter STATIC ${NED_TREE_SITTER_CORE_SOURCES})
target_include_directories(tree-sitter
        PUBLIC ${NED_TREE_SITTER_CORE_DIR}/include
        PRIVATE ${NED_TREE_SITTER_CORE_DIR}/src)
set_target_properties(tree-sitter PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
#------------------------------------------------------------------------------

#--- System libvterm (terminal-panel follow-up) --------------------------------
# libvterm (the VT100/xterm emulator behind Neovim's :terminal and
# emacs-vterm) ships a plain Makefile, not CMake, so this used to be a
# fetch-but-don't-configure + hand-built-static-target arrangement, including
# hand-generating the src/encoding/*.inc tables upstream's Makefile normally
# produces via a Perl script. None of that is needed against the system
# package (Gentoo's dev-libs/libvterm), which ships those tables already
# compiled into libvterm.so; pkg-config's own module name is "vterm", not
# "libvterm".
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
add_library(vterm ALIAS PkgConfig::VTERM)
#------------------------------------------------------------------------------

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


# Same "generate a .cpp constant from a checked-in source file" approach as
# ned_embed_treesitter_query above, applied to bundled Janet plugin source
# (see Source/Janet/Plugins/vcs-git.janet) -- this codebase has no
# Resources/-style loose-runtime-file convention, so bundled Janet plugin
# code is compiled in the same way bundled tree-sitter queries already are.
function(ned_embed_janet_plugin plugin_file symbol)
    if (NOT EXISTS "${plugin_file}")
        message(FATAL_ERROR "ned_embed_janet_plugin: ${plugin_file} not found")
    endif()
    # Same configure-time-read staleness trap as ned_embed_treesitter_query
    # above -- see the comment there.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${plugin_file}")
    file(READ "${plugin_file}" plugin_content)
    string(CONCAT generated_content
        "// Auto-generated by CMake's ned_embed_janet_plugin (see CMakeLists.txt)\n"
        "// from ${plugin_file} -- do not edit by hand, changes will be\n"
        "// overwritten on the next configure.\n"
        "namespace ned::janet::plugins {\n"
        "extern const char* const k${symbol} = R\"NED_PLUGIN_EOF(${plugin_content})NED_PLUGIN_EOF\";\n"
        "}\n")
    set(generated_file "${CMAKE_BINARY_DIR}/ned_generated/EmbeddedPlugin_${symbol}.cpp")
    file(WRITE "${generated_file}" "${generated_content}")
    set(NED_GENERATED_QUERY_SOURCES "${NED_GENERATED_QUERY_SOURCES}" "${generated_file}" PARENT_SCOPE)
endfunction()

ned_embed_janet_plugin("${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins/vcs-git.janet" VcsGit)
ned_embed_janet_plugin("${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins/gradients.janet" Gradients)
ned_embed_janet_plugin("${CMAKE_CURRENT_SOURCE_DIR}/Source/Janet/Plugins/languages.janet" Languages)

# Every file under Source/Languages/ -- a language's query data and, once it
# exists, its language.janet -- compiled into one path -> content table
# (Editor/LanguageFiles.h's EmbeddedLanguageFiles). One glob rather than one
# embed call per file: adding a language or a query kind is adding a file,
# not a CMake line, a Queries.h extern and a factory initializer. The glob is
# CONFIGURE_DEPENDS so a new file triggers a reconfigure, and each file is a
# configure dependency so an edited one does -- the same file(READ) staleness
# trap ned_embed_treesitter_query documents.
function(ned_embed_language_files)
    file(GLOB_RECURSE language_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages/*.janet")
    list(SORT language_files)
    string(CONCAT generated_content
        "// Auto-generated by CMake's ned_embed_language_files (see CMakeLists.txt)\n"
        "// from Source/Languages/ -- do not edit by hand, changes will be\n"
        "// overwritten on the next configure.\n"
        "#include \"Editor/LanguageFiles.h\"\n"
        "namespace ned::editor {\n"
        "namespace {\n"
        "const EmbeddedLanguageFile kFiles[] = {\n")
    foreach(language_file IN LISTS language_files)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${language_file}")
        file(RELATIVE_PATH relative_path "${CMAKE_CURRENT_SOURCE_DIR}/Source/Languages" "${language_file}")
        file(READ "${language_file}" file_content)
        string(APPEND generated_content
            "    {\"${relative_path}\", R\"NEDLANGFILE(${file_content})NEDLANGFILE\"},\n")
    endforeach()
    # A zero-length array is not C++; an empty Source/Languages/ (never the
    # case once the bundled files exist) gets an empty span instead.
    string(APPEND generated_content
        "    {\"\", \"\"},\n"
        "};\n"
        "} // namespace\n"
        "std::span<const EmbeddedLanguageFile> EmbeddedLanguageFiles() {\n"
        "    return {kFiles, sizeof(kFiles) / sizeof(kFiles[0]) - 1};\n"
        "}\n"
        "} // namespace ned::editor\n")
    set(generated_file "${CMAKE_BINARY_DIR}/ned_generated/EmbeddedLanguageFiles.cpp")
    file(WRITE "${generated_file}" "${generated_content}")
    set(NED_GENERATED_QUERY_SOURCES "${NED_GENERATED_QUERY_SOURCES}" "${generated_file}" PARENT_SCOPE)
endfunction()

ned_embed_language_files()

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
# grammars.py pulls the release tarball for src/ and a second, ordinary tag
# fetch for test/corpus (vendored separately as tree-sitter-sql-corpus/,
# read directly by ParseConformanceTest.cpp -- never a CMake build input),
# since the release tarball omits it.
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

#--- Catch2 ---------------------------------------------------------------------
if (NED_BUILD_TESTS)
    # System package (Gentoo's dev-cpp/catch, v3.15.3) ships Catch2Config.cmake,
    # which appends its own directory to CMAKE_MODULE_PATH itself -- so
    # include(Catch) below finds its Catch.cmake module with no extra wiring,
    # the same way FetchContent's own extras/ directory used to be added by hand.
    find_package(Catch2 3 REQUIRED)
endif()
#------------------------------------------------------------------------------

pkg_check_modules(Janet REQUIRED janet)

# ned_lib holds everything except main()
add_library(ned_lib STATIC
        ${NED_GENERATED_QUERY_SOURCES}
        Source/Application.cpp
        Source/Text/Rope.cpp
        Source/Text/RopeStorage.cpp
        Source/Text/MappedFile.cpp
        Source/Text/OffsetRemap.cpp
        Source/Text/PieceTable.cpp
        Source/Text/PieceTableStorage.cpp
        Source/Text/DiskSpace.cpp
        Source/Text/FilePreservation.cpp
        Source/Text/Grapheme.cpp
        Source/Text/UndoTree.cpp
        Source/Text/KillRing.cpp
        Source/Text/Buffer.cpp
        Source/Text/BufferList.cpp
        Source/Text/BinaryDetect.cpp
        Source/Text/LineDiff.cpp
        Source/Text/ThreeWayMerge.cpp
        Source/Text/ConflictHunk.cpp
        Source/Text/Utf8.cpp
        Source/Text/LineEnding.cpp
        Source/Editor/Key.cpp
        Source/Editor/Command.cpp
        Source/Editor/FuzzyMatch.cpp
        Source/Editor/Keymap.cpp
        Source/Editor/Dispatcher.cpp
        Source/Editor/Commands.cpp
        Source/Editor/ScriptingSession.cpp
        Source/Editor/IncrementalSearch.cpp
        Source/Editor/LineListSearch.cpp
        Source/Editor/PrefixArgument.cpp
        Source/Editor/QueryReplace.cpp
        Source/Editor/RegexPattern.cpp
        Source/Editor/HugeRegexScan.cpp
        Source/Editor/Register.cpp
        Source/Editor/Rectangle.cpp
        Source/Editor/ConflictResolution.cpp
        Source/Editor/AutoPair.cpp
        Source/Editor/CompletionSession.cpp
        Source/Editor/DabbrevComplete.cpp
        Source/Editor/JanetSymbolComplete.cpp
        Source/Editor/MinibufferPrompt.cpp
        Source/Editor/PromptHistory.cpp
        Source/Editor/Mode.cpp
        Source/Editor/LanguageDefinition.cpp
        Source/Editor/LanguageFiles.cpp
        Source/Editor/JanetData.cpp
        Source/Editor/LanguageParse.cpp
        Source/Editor/LanguageRegistry.cpp
        Source/Editor/CaptureClassifiers.cpp
        Source/Editor/QueryData.cpp
        Source/Editor/BundledLanguages.cpp
        Source/Editor/Languages/CLike.cpp
        Source/Editor/Languages/Markdown.cpp
        Source/Editor/Languages/Org.cpp
        Source/Editor/Injection.cpp
        Source/Editor/EmbeddedDocuments.cpp
        Source/Editor/SyntaxTheme.cpp
        Source/Editor/ModeOverrides.cpp
        Source/Editor/ModePrewarm.cpp
        Source/Editor/Multibuffer.cpp
        Source/Editor/MultibufferFoldSettings.cpp
        Source/Editor/MultibufferLimits.cpp
        Source/Editor/MultibufferSearchSettings.cpp
        Source/Editor/NextError.cpp
        Source/Editor/WrapOverrides.cpp
        Source/Editor/CodeFold.cpp
        Source/Editor/LocalScopes.cpp
        Source/Editor/RenameReview.cpp
        Source/Editor/ClassFileSync.cpp
        Source/Editor/ImportFixup.cpp
        Source/Editor/ImportFixupSettings.cpp
        Source/Editor/RenameReviewSettings.cpp
        Source/Editor/ClassFileSyncSettings.cpp
        Source/Editor/CodeFoldSettings.cpp
        Source/Editor/StickyScroll.cpp
        Source/Editor/StickyScrollSettings.cpp
        Source/Editor/RelativeLineNumberSettings.cpp
        Source/Editor/WhichKeySettings.cpp
        Source/Editor/Org.cpp
        Source/Editor/OrgCapture.cpp
        Source/Editor/Snippet.cpp
        Source/Editor/SnippetRegistry.cpp
        Source/Editor/BundledSnippets.cpp
        Source/Editor/LinkedEditingSession.cpp
        Source/Editor/Table.cpp
        Source/Editor/Markdown.cpp
        Source/Editor/Link.cpp
        Source/Editor/Php.cpp
        Source/Editor/HeaderSource.cpp
        Source/Editor/FinalNewline.cpp
        Source/Editor/TrimOnSave.cpp
        Source/Editor/LineEndingPolicy.cpp
        Source/Editor/FormatOnSave.cpp
        Source/Editor/Clipboard.cpp
        Source/Editor/TabWidth.cpp
        Source/Editor/IndentStyle.cpp
        Source/Editor/Indent.cpp
        Source/Editor/BlankLineCleanup.cpp
        Source/Editor/FillColumn.cpp
        Source/Editor/Fill.cpp
        Source/Editor/PageScroll.cpp
        Source/Editor/DiffRefreshSettings.cpp
        Source/Editor/ProcessTimeouts.cpp
        Source/Editor/ThemeSetting.cpp
        Source/Editor/Variables.cpp
        Source/Editor/HighlightSettings.cpp
        Source/Editor/HugeStructuralWindow.cpp
        Source/Editor/MinimapSettings.cpp
        Source/Editor/WhitespaceSettings.cpp
        Source/Editor/GitIgnore.cpp
        Source/Editor/SearchSettings.cpp
        Source/Editor/Project/Search.cpp
        Source/Editor/Project/Agenda.cpp
        Source/Editor/Project/Replace.cpp
        Source/Editor/Project/Tree.cpp
        Source/Editor/Project/Root.cpp
        Source/Editor/Project/Settings.cpp
        Source/Editor/ToolchainIncludePaths.cpp
        Source/Editor/NodeModules.cpp
        Source/Editor/ImportResolutionConfig.cpp
        Source/Editor/ImportResolve.cpp
        Source/Editor/Project/FileOps.cpp
        Source/Editor/AutoRevert.cpp
        Source/Editor/AutoMerge.cpp
        Source/Editor/FileWatch.cpp
        Source/Editor/BackgroundActivity.cpp
        Source/Editor/HighlightCache.cpp
        Source/Editor/ChunkedHighlight.cpp
        Source/Editor/InlineDiagnostics.cpp
        Source/Editor/RecencyGlow.cpp
        Source/Editor/ScratchPad.cpp
        Source/Editor/Backup.cpp
        Source/Editor/BufferSave.cpp
        Source/Editor/PersistentUndo.cpp
        Source/Editor/Project/Undo.cpp
        Source/Editor/DiagnosticsLog.cpp
        Source/Editor/SanitizerOutputParser.cpp
        Source/Editor/ValgrindOutputParser.cpp
        Source/Editor/MassifOutputParser.cpp
        Source/Editor/MassifReportBuffer.cpp
        Source/Editor/Session.cpp
        Source/Editor/RecentFiles.cpp
        Source/Editor/Bookmark.cpp
        Source/Editor/Project/Session.cpp
        Source/Editor/Project/Trust.cpp
        Source/Editor/Project/Registry.cpp
        Source/Editor/PendingReExec.cpp
        Source/Editor/TerminalTabLauncher.cpp
        Source/Editor/Project/Switch.cpp
        Source/Editor/Project/Plugins.cpp
        Source/Editor/Vim/Settings.cpp
        Source/Editor/Vim/Motion.cpp
        Source/Editor/Vim/TextObject.cpp
        Source/Editor/Vim/Surround.cpp
        Source/Editor/Vim/Registers.cpp
        Source/Editor/Vim/ExCommand.cpp
        Source/Editor/Vim/Engine.cpp
        Source/Editor/Vim/GlobalMarks.cpp
        Source/Editor/Vim/Magic.cpp
        Source/Editor/Vcs/DiffPatch.cpp
        Source/Editor/Vcs/ProviderRegistry.cpp
        Source/Editor/Vcs/RowStatus.cpp
        Source/Editor/Vcs/Runner.cpp
        Source/Editor/Process/ChildProcess.cpp
        Source/Editor/Acp/Transport.cpp
        Source/Editor/Acp/Client.cpp
        Source/Editor/Acp/Config.cpp
        Source/Editor/Acp/PanelConfig.cpp
        Source/Editor/Acp/Manager.cpp
        Source/Editor/Lsp/Transport.cpp
        Source/Editor/Lsp/Client.cpp
        Source/Editor/Lsp/ServerConfig.cpp
        Source/Editor/Lsp/RootResolver.cpp
        Source/Editor/Lsp/ProseChecker.cpp
        Source/Editor/Lsp/Position.cpp
        Source/Editor/Lsp/Content.cpp
        Source/Editor/Lsp/EditApply.cpp
        Source/Editor/Lsp/Manager.cpp
        Source/Editor/Lsp/BackgroundSync.cpp
        Source/Editor/Lsp/Broker.cpp
        Source/Editor/Lsp/BrokerSocketPath.cpp
        Source/Editor/Lsp/BrokerMain.cpp
        Source/Editor/Lsp/BrokerDaemon.cpp
        Source/Editor/Lsp/BrokerConnect.cpp
        Source/Editor/Mcp/SocketPath.cpp
        Source/Editor/Mcp/Transport.cpp
        Source/Editor/Mcp/ToolRegistry.cpp
        Source/Editor/Mcp/BridgeServer.cpp
        Source/Editor/Mcp/BridgeSetting.cpp
        Source/Editor/Tasks/TaskConfig.cpp
        Source/Editor/Tasks/TaskProcess.cpp
        Source/Editor/Tasks/TaskRunner.cpp
        Source/Editor/Repl/Config.cpp
        Source/Editor/TestRun/TestOutputParser.cpp
        Source/Editor/TestRun/TestResult.cpp
        Source/Editor/TestRun/TestResultsBuffer.cpp
        Source/Editor/TestRun/Config.cpp
        Source/Editor/TestRun/TestRunner.cpp
        Source/Editor/TestRun/TestSourceResolver.cpp
        Source/Editor/Coverage/Report.cpp
        Source/Editor/Coverage/OutputParser.cpp
        Source/Editor/Coverage/Config.cpp
        Source/Editor/Dap/Config.cpp
        Source/Editor/Dap/Client.cpp
        Source/Editor/Dap/Manager.cpp
        Source/Editor/PointerGraphNode.cpp
        Source/Editor/Sparkline.cpp
        Source/Editor/MemoryImage.cpp
        Source/Editor/Terminal/Config.cpp
        Source/Editor/Terminal/Emulator.cpp
        Source/Editor/Terminal/PtyProcess.cpp
        Source/Editor/TreeSitter/Node.cpp
        Source/Editor/Imprint.cpp
        Source/Editor/ImprintTables.cpp
        Source/Editor/ImprintFold.cpp
        Source/Editor/ImprintBracket.cpp
        Source/Editor/ImprintIndent.cpp
        Source/Editor/TreeSitter/GrammarImprint.cpp
        Source/Editor/TreeSitter/Tree.cpp
        Source/Editor/TreeSitter/Parser.cpp
        Source/Editor/TreeSitter/IncrementalParse.cpp
        Source/Editor/TreeSitter/QueryPredicates.cpp
        Source/Editor/TreeSitter/QueryMatcher.cpp
        Source/Editor/TreeSitter/MatchCache.cpp
        Source/Editor/TreeSitter/Languages.cpp
        Source/Editor/TreeSitter/DynamicGrammar.cpp
        Source/Editor/Parse/Green.cpp
        Source/Editor/Parse/Lexer.cpp
        Source/Editor/Parse/Stack.cpp
        Source/Editor/Parse/Parser.cpp
        Source/Editor/Parse/Tree.cpp
        Source/Editor/Parse/Node.cpp
        Source/Editor/Parse/Cursor.cpp
        Source/Editor/Parse/Sexp.cpp
        Source/Janet/Value.cpp
        Source/Janet/Environment.cpp
        Source/Janet/EditorBindings.cpp
        Source/Janet/JanetVcsProvider.cpp
        Source/Janet/PluginLoader.cpp
        Source/Janet/InitFile.cpp
        Source/UI/KeyTranslation.cpp
        Source/UI/Border.cpp
        Source/UI/Widget.cpp
        Source/UI/Compositing.cpp
        Source/UI/Paint.cpp
        Source/UI/PaintParse.cpp
        Source/UI/ThemePaints.cpp
        Source/UI/EventLoop.cpp
        Source/UI/Layout.cpp
        Source/UI/ScrollBar.cpp
        Source/UI/Minimap.cpp
        Source/UI/Overlay.cpp
        Source/UI/PanelDock.cpp
        Source/UI/TerminalPanel.cpp
        Source/UI/AcpPanel.cpp
        Source/UI/DebugConsolePanel.cpp
        Source/UI/JanetReplPanel.cpp
        Source/UI/ListPopup.cpp
        Source/UI/TreeView.cpp
        Source/UI/MemoryImageView.cpp
        Source/UI/BufferListPanel.cpp
        Source/UI/DapThreadsPanel.cpp
        Source/UI/BufferView.cpp
        Source/UI/BufferView/Paint.cpp
        Source/UI/BufferView/Gutter.cpp
        Source/UI/BufferView/Prompts.cpp
        Source/UI/BufferView/Lsp.cpp
        Source/UI/BufferView/Rename.cpp
        Source/UI/BufferView/ClassFileSync.cpp
        Source/UI/BufferView/VcsDap.cpp
        Source/UI/BufferView/Buffers.cpp
        Source/UI/BufferView/CandidateList.cpp
        Source/UI/BufferView/GutterModel.cpp
        Source/UI/BufferView/Viewport.cpp
        Source/UI/BufferView/Mouse.cpp
        Source/UI/WindowManager.cpp
        Source/UI/AsyncFileLoader.cpp
        Source/UI/HugeFileLoader.cpp
        Source/UI/TabBar.cpp
        Source/UI/ScrollArrowButton.cpp
        Source/UI/ProjectSidebar.cpp
        Source/UI/LeftDock.cpp
        Source/UI/VcsPanel.cpp
        Source/UI/VcsDiffPreview.cpp
        Source/UI/ModeLine.cpp
        Source/UI/EchoArea.cpp
        Source/UI/Theme.cpp
        Source/UI/ThemePalette.cpp
        Source/UI/ThemeRegistry.cpp
        Source/UI/ThemeFile.cpp
        Source/UI/ThemeResolve.cpp
        Source/UI/ThemeGallery.cpp
        Source/UI/DesktopThemeProbe.cpp)

target_include_directories(ned_lib
        PUBLIC Source
        PUBLIC ${CMAKE_BINARY_DIR}/ned_generated
        PUBLIC ${Janet_INCLUDE_DIRS})

# Janet_LIBRARIES is just the bare library name ("janet"), not a full path --
# pkg_check_modules doesn't add -L for it, so linking relies on the bare
# "-ljanet" resolving via the linker's own implicit default search path.
# That's true often enough to go unnoticed (works with GNU ld's Debian/Ubuntu
# default search list, or when Janet happens to already be installed
# somewhere already on the default path), but broke under lld on a fresh CI
# runner where /usr/local/lib (where CI's own `make install` puts it) isn't
# implicitly searched -- ld.lld: unable to find library -ljanet. Explicit
# target_link_directories is what actually fixes it, not luck.
target_link_directories(ned_lib PUBLIC ${Janet_LIBRARY_DIRS})

target_link_libraries(ned_lib
        PUBLIC ${Janet_LIBRARIES}
        # Every widget (Widget/KeyTranslation/Theme/EventLoop/Layout and every
        # concrete widget through main.cpp's own composition root) is built on
        # Notcurses. Shared, not static (notcurses' static build needs a
        # static libdeflate/zlib this project has no reason to require) --
        # notcurses' own CMakeLists.txt already installs notcurses-core's
        # shared library to CMAKE_INSTALL_LIBDIR unconditionally, so
        # `cmake --install` places it on the normal system library path
        # alongside `ned` with no RPATH trick needed.
        PUBLIC notcurses-core
        # Terminal-panel follow-up: PUBLIC because vterm.h reaches ned_lib's
        # own headers via Source/Editor/Terminal/Emulator.h.
        PUBLIC vterm
        PUBLIC utf8proc
        PUBLIC nlohmann_json::nlohmann_json
        # The tree-sitter RUNTIME is deliberately absent: since the Phase 4b
        # engine swap ned parses with Source/Editor/Parse/ interpreting the
        # generated grammar tables below directly. ned_tests still links the
        # runtime privately as the conformance suite's differential
        # reference.
        PUBLIC tree-sitter-json
        PUBLIC tree-sitter-c
        PUBLIC tree-sitter-cpp
        PUBLIC tree-sitter-php
        PUBLIC tree-sitter-javascript
        PUBLIC tree-sitter-typescript
        PUBLIC tree-sitter-tsx
        PUBLIC tree-sitter-html
        PUBLIC tree-sitter-css
        PUBLIC tree-sitter-python
        PUBLIC tree-sitter-bash
        PUBLIC tree-sitter-janet-simple
        PUBLIC tree-sitter-markdown
        PUBLIC tree-sitter-markdown-inline
        PUBLIC tree-sitter-org
        PUBLIC tree-sitter-yaml
        PUBLIC tree-sitter-toml
        PUBLIC tree-sitter-clojure
        PUBLIC tree-sitter-fish
        PUBLIC tree-sitter-xml
        PUBLIC tree-sitter-rust
        PUBLIC tree-sitter-go
        PUBLIC tree-sitter-c-sharp
        PUBLIC tree-sitter-java
        PUBLIC tree-sitter-kotlin
        PUBLIC tree-sitter-lua
        PUBLIC tree-sitter-cmake
        PUBLIC tree-sitter-diff
        PUBLIC tree-sitter-sql
        PUBLIC tree-sitter-dockerfile
        PUBLIC tree-sitter-make
        PUBLIC tree-sitter-hcl
        PUBLIC tree-sitter-nix
        PUBLIC tree-sitter-ruby
        PUBLIC tree-sitter-gitcommit
        PUBLIC tree-sitter-gitrebase
        PUBLIC tree-sitter-r
        PRIVATE ${CMAKE_DL_LIBS}
        # internal-project-search follow-up: only ProjectSearch.cpp's own
        # implementation includes RE2 headers, so this stays off the public
        # link interface.
        PRIVATE re2::re2
        # in-file-regex follow-up: same treatment for PCRE2 -- only
        # RegexPattern.cpp's implementation includes pcre2.h (the header is
        # pimpl'd specifically so this dependency stays private).
        PRIVATE pcre2-8-static)

if (NED_ENABLE_SANITIZERS)
    target_compile_options(ned_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ned_lib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ned Source/main.cpp)

target_link_libraries(ned PRIVATE ned_lib CLI11::CLI11)

# Packaging follow-up: no install() rule existed at all before this -- fine
# for `cmake --build build && ./build/ned` (this project's own documented
# dev workflow), not fine for anything that needs a real `cmake --install`
# (a system package, an ebuild, ...). Deliberately minimal: just the
# executable, standard GNUInstallDirs layout, no desktop file/icon/man page
# (see ROADMAP.md's "Documentation framework" entry for those).
include(GNUInstallDirs)
# notcurses-core used to be a private FetchContent'd copy, which needed its
# own install(TARGETS ...) plus an INSTALL_RPATH pointing `ned` at a private,
# ned-only subdirectory rather than the shared system libdir -- avoiding a
# real install-time collision with a system notcurses package owning that
# same SONAME path, and pinning the exact version/ABI a system package might
# not carry (see PatchNotcurses*.cmake above). Now that notcurses-core *is*
# the system package (see its own comment above), neither concern applies:
# `ned` links against and finds it exactly the way any other system-library
# consumer does, with the linker's normal default search path.
install(TARGETS ned RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Standalone diagnostic, not part of ned/ned_lib -- see its own header
# comment (Tools/NotcursesAlphaProbe.cpp) for what it's answering. Links
# directly against notcurses-core, nothing else from this project.
add_executable(notcurses_alpha_probe Tools/NotcursesAlphaProbe.cpp)
target_link_libraries(notcurses_alpha_probe PRIVATE notcurses-core)

# Same "standalone diagnostic, not part of ned/ned_lib" shape as
# notcurses_alpha_probe above -- see Tools/NotcursesPixelProbe.cpp's own
# header comment for what it's answering (real pixel-graphics support, for
# a possible minimap feature).
add_executable(notcurses_pixel_probe Tools/NotcursesPixelProbe.cpp)
target_link_libraries(notcurses_pixel_probe PRIVATE notcurses-core)

# Same shape again -- see Tools/NotcursesMinimapProbe.cpp's own header
# comment: exercises the exact small-owned-plane blit path
# Minimap::EnsurePixelPlane() uses, at real minimap cell dimensions, next to
# Notcurses' own non-pixel blitters for a direct comparison.
add_executable(notcurses_minimap_probe Tools/NotcursesMinimapProbe.cpp)
target_link_libraries(notcurses_minimap_probe PRIVATE notcurses-core)

# Same shape again -- see Tools/NotcursesGradientProbe.cpp's own header
# comment: a paged probe for translucency (background/foreground/overlapping,
# and what Notcurses' 2-bit cell alpha can and cannot express) next to
# gradients in every direction, opacity-only and colour+opacity both.
add_executable(notcurses_gradient_probe Tools/NotcursesGradientProbe.cpp)
target_link_libraries(notcurses_gradient_probe PRIVATE notcurses-core)

# Standalone diagnostic again, but this one links against nothing at all --
# see Tools/TerminalImageAlphaProbe.cpp's header comment: it speaks the
# iTerm2/kitty/sixel graphics protocols directly, to test per-pixel alpha
# handling that Notcurses itself cannot reach (3.0.17 has no iTerm2 backend).
add_executable(terminal_image_alpha_probe Tools/TerminalImageAlphaProbe.cpp)

# Same standalone-diagnostic shape -- see Tools/NotcursesLayerProbe.cpp's own
# header comment: whether layered ncplanes can produce a background that is
# translucent to the desktop, or only to Notcurses' own idea of a backdrop.
add_executable(notcurses_layer_probe Tools/NotcursesLayerProbe.cpp)
target_link_libraries(notcurses_layer_probe PRIVATE notcurses-core)

if (NED_BUILD_TESTS)
    enable_testing()
    include(CTest)
    include(Catch)

    add_executable(ned_tests
            Tests/ApplicationTest.cpp
            Tests/RopeTest.cpp
            Tests/MappedFileTest.cpp
            Tests/PieceTableTest.cpp
            Tests/PieceTableStorageTest.cpp
            Tests/GraphemeTest.cpp
            Tests/UndoTreeTest.cpp
            Tests/KillRingTest.cpp
            Tests/RegisterTest.cpp
            Tests/RectangleTest.cpp
            Tests/AutoPairTest.cpp
            Tests/CompletionSessionTest.cpp
            Tests/LspEditApplyTest.cpp
            Tests/DabbrevCompleteTest.cpp
            Tests/LocalScopesTest.cpp
            Tests/ModeLocalsTest.cpp
            Tests/RenameReviewTest.cpp
            Tests/ClassFileSyncTest.cpp
            Tests/ImportFixupTest.cpp
            Tests/BufferViewRenameSymbolTest.cpp
            Tests/BufferViewClassFileSyncTest.cpp
            Tests/BufferViewRenameReviewTest.cpp
            Tests/BufferViewImportFixupTest.cpp
            Tests/SnippetTest.cpp
            Tests/SnippetRegistryTest.cpp
            Tests/BundledSnippetsTest.cpp
            Tests/LinkedEditingSessionTest.cpp
            Tests/JanetSymbolCompleteTest.cpp
            Tests/AutoRevertTest.cpp
            Tests/AutoMergeTest.cpp
            Tests/FileWatchTest.cpp
            Tests/LineDiffTest.cpp
            Tests/ThreeWayMergeTest.cpp
            Tests/ConflictHunkTest.cpp
            Tests/ConflictResolutionTest.cpp
            Tests/BufferViewConflictQuickKeyTest.cpp
            Tests/BufferViewChoicePromptTest.cpp
            Tests/BufferViewPromptDriverTest.cpp
            Tests/BufferTest.cpp
            Tests/BufferHugeFileTest.cpp
            Tests/DiskSpaceTest.cpp
            Tests/BufferSaveEquivalenceTest.cpp
            Tests/MultiCursorTest.cpp
            Tests/BufferListTest.cpp
            Tests/FilePreservationTest.cpp
            Tests/BinaryDetectTest.cpp
            Tests/LineEndingTest.cpp
            Tests/LineEndingPolicyTest.cpp
            Tests/KeyTest.cpp
            Tests/CommandTest.cpp
            Tests/FuzzyMatchTest.cpp
            Tests/KeymapTest.cpp
            Tests/DispatcherTest.cpp
            Tests/CommandsTest.cpp
            Tests/JanetTestSupport.cpp
            Tests/ValueTest.cpp
            Tests/EnvironmentTest.cpp
            Tests/ScriptingSessionTest.cpp
            Tests/EditorBindingsTest.cpp
            Tests/JanetVcsProviderTest.cpp
            Tests/GitVcsPluginTest.cpp
            Tests/InitFileTest.cpp
            Tests/TestEvents.cpp
            Tests/KeyTranslationTest.cpp
            Tests/BufferViewTest.cpp
            Tests/BufferViewBlameGutterTest.cpp
            Tests/BufferViewDiffGutterTest.cpp
            Tests/BufferViewHunkNavigationTest.cpp
            Tests/BufferViewIndentGuideDepthTest.cpp
            Tests/BufferViewStickyScrollTest.cpp
            Tests/BufferViewSymbolGutterTest.cpp
            Tests/BufferViewHugeStructuralGutterTest.cpp
            Tests/BufferViewVcsStatusTest.cpp
            Tests/BufferViewVcsCommitTest.cpp
            Tests/WindowManagerTest.cpp
            Tests/ModeLineTest.cpp
            Tests/EchoAreaTest.cpp
            Tests/Utf8Test.cpp
            Tests/IncrementalSearchTest.cpp
            Tests/LineListSearchTest.cpp
            Tests/PrefixArgumentTest.cpp
            Tests/QueryReplaceTest.cpp
            Tests/RegexPatternTest.cpp
            Tests/HugeRegexScanTest.cpp
            Tests/ModeTest.cpp
            Tests/LanguageDefinitionTest.cpp
            Tests/QueryDataTest.cpp
            Tests/QueryMatcherTest.cpp
            Tests/MatchCacheTest.cpp
            Tests/LanguageParseTest.cpp
            Tests/CaptureClassifierTest.cpp
            Tests/JanetCaptureClassifierTest.cpp
            Tests/CodeFoldTest.cpp
            Tests/IndentStyleTest.cpp
            Tests/IndentEngineTest.cpp
            Tests/IndentTest.cpp
            Tests/StickyScrollTest.cpp
            Tests/OrgTest.cpp
            Tests/OrgHighlightTest.cpp
            Tests/OrgCaptureTest.cpp
            Tests/OrgClockTest.cpp
            Tests/TableTest.cpp
            Tests/MarkdownTest.cpp
            Tests/InlayHintColumnTest.cpp
            Tests/MarkdownHighlightTest.cpp
            Tests/InjectionTest.cpp
            Tests/EmbeddedDocumentsTest.cpp
            Tests/HtmlHighlightTest.cpp
            Tests/LinkTest.cpp
            Tests/HeaderSourceTest.cpp
            Tests/PerformanceTest.cpp
            Tests/CompositingTest.cpp
            Tests/PaintTest.cpp
            Tests/PaintParseTest.cpp
            Tests/BundledPaintsTest.cpp
            Tests/ChromeSurfaceTest.cpp
            Tests/ThemeTest.cpp
            Tests/ThemePaletteTest.cpp
            Tests/VariablesTest.cpp
            Tests/BundledThemesTest.cpp
            Tests/SyntaxThemeTest.cpp
            Tests/ThemeFileTest.cpp
            Tests/ThemeResolveTest.cpp
            Tests/ThemeGalleryTest.cpp
            Tests/DocSourcePathsTest.cpp
            Tests/OracleTest.cpp
            Tests/ImprintTest.cpp
            Tests/ImprintIndentTest.cpp
            Tests/CommandReferenceTest.cpp
            Tests/ImprintBracketTest.cpp
            Tests/FormatterPropertiesTest.cpp
            Tests/ThemeKeyDocsTest.cpp
            Tests/InitFileThemeWriteTest.cpp
            Tests/LayoutStabilityProbe.cpp
            Tests/OffsetRemapTest.cpp
            Tests/OverlayScrimTest.cpp
            Tests/OverlayShadowTest.cpp
            Tests/RecencyGlowTest.cpp
            Tests/HighlightCacheTest.cpp
            Tests/ChunkedHighlightTest.cpp
            Tests/KeystrokeBench.cpp
            Tests/BufferViewTintCompositingTest.cpp
            Tests/DesktopThemeProbeTest.cpp
            Tests/ActiveBufferTest.cpp
            Tests/CacheStampTest.cpp
            Tests/CandidateListTest.cpp
            Tests/RequestSlotTest.cpp
            Tests/MinibufferPromptTest.cpp
            Tests/PromptHistoryTest.cpp
            Tests/TabBarTest.cpp
            Tests/ScrollArrowButtonTest.cpp
            Tests/FinalNewlineTest.cpp
            Tests/TrimOnSaveTest.cpp
            Tests/FormatOnSaveTest.cpp
            Tests/ClipboardTest.cpp
            Tests/ClipboardTestGuard.cpp
            Tests/SigpipeTestGuard.cpp
            Tests/TerminalOutputTestGuard.cpp
            Tests/TabWidthTest.cpp
            Tests/FillColumnTest.cpp
            Tests/FillTest.cpp
            Tests/PageScrollTest.cpp
            Tests/DiffRefreshSettingsTest.cpp
            Tests/ProcessTimeoutsTest.cpp
            Tests/HighlightSettingsTest.cpp
            Tests/HugeStructuralWindowTest.cpp
            Tests/MinimapSettingsTest.cpp
            Tests/WhitespaceSettingsTest.cpp
            Tests/RelativeLineNumberSettingsTest.cpp
            Tests/WhichKeySettingsTest.cpp
            Tests/ListPopupTest.cpp
            Tests/TreeViewTest.cpp
            Tests/BufferListPanelTest.cpp
            Tests/MinimapTest.cpp
            Tests/OverlayTest.cpp
            Tests/PanelDockTest.cpp
            Tests/GitIgnoreTest.cpp
            Tests/SearchSettingsTest.cpp
            Tests/ProjectSearchTest.cpp
            Tests/ProjectAgendaTest.cpp
            Tests/ProjectReplaceTest.cpp
            Tests/ProjectTreeTest.cpp
            Tests/ProjectRootTest.cpp
            Tests/ProjectSettingsTest.cpp
            Tests/ToolchainIncludePathsTest.cpp
            Tests/NodeModulesTest.cpp
            Tests/ImportResolutionConfigTest.cpp
            Tests/ImportTargetTest.cpp
            Tests/PhpTest.cpp
            Tests/ProjectFileOpsTest.cpp
            Tests/ScratchPadTest.cpp
            Tests/BackupTest.cpp
            Tests/PersistentUndoTest.cpp
            Tests/ProjectUndoTest.cpp
            Tests/DiagnosticsLogTest.cpp
            Tests/SanitizerOutputParserTest.cpp
            Tests/ValgrindOutputParserTest.cpp
            Tests/MassifOutputParserTest.cpp
            Tests/MassifReportBufferTest.cpp
            Tests/SessionTest.cpp
            Tests/RecentFilesTest.cpp
            Tests/BookmarkTest.cpp
            Tests/ProjectSessionTest.cpp
            Tests/ProjectTrustTest.cpp
            Tests/ProjectRegistryTest.cpp
            Tests/PendingReExecTest.cpp
            Tests/TerminalTabLauncherTest.cpp
            Tests/ProjectSwitchTest.cpp
            Tests/ProjectPluginsTest.cpp
            Tests/VcsProviderRegistryTest.cpp
            Tests/DiffPatchTest.cpp
            Tests/VcsRunnerTest.cpp
            Tests/VcsRowStatusTest.cpp
            Tests/BackgroundActivityTest.cpp
            Tests/AcpTransportTest.cpp
            Tests/AcpClientTest.cpp
            Tests/AcpConfigTest.cpp
            Tests/AcpManagerTest.cpp
            Tests/AcpPanelTest.cpp
            Tests/LspTransportTest.cpp
            Tests/LspClientTest.cpp
            Tests/LspServerConfigTest.cpp
            Tests/LspRootResolverTest.cpp
            Tests/ProseCheckerTest.cpp
            Tests/ProseCheckerTestGuard.cpp
            Tests/LspPositionTest.cpp
            Tests/LspContentTest.cpp
            Tests/LspManagerTest.cpp
            Tests/ExpandableTreeTest.cpp
            Tests/LspBrokerTest.cpp
            Tests/LspBrokerConnectTest.cpp
            Tests/LspBrokerDaemonTest.cpp
            Tests/McpTransportTest.cpp
            Tests/McpToolRegistryTest.cpp
            Tests/McpBridgeServerTest.cpp
            Tests/DapConfigTest.cpp
            Tests/DapClientTest.cpp
            Tests/DapManagerTest.cpp
            Tests/PointerGraphNodeTest.cpp
            Tests/SparklineTest.cpp
            Tests/MemoryImageTest.cpp
            Tests/MemoryImageViewTest.cpp
            Tests/DebugConsolePanelTest.cpp
            Tests/JanetReplPanelTest.cpp
            Tests/DapThreadsPanelTest.cpp
            Tests/ChildProcessTest.cpp
            Tests/TaskConfigTest.cpp
            Tests/TaskProcessTest.cpp
            Tests/TaskRunnerTest.cpp
            Tests/ReplConfigTest.cpp
            Tests/TestOutputParserTest.cpp
            Tests/BufferViewTestGutterTest.cpp
            Tests/BufferViewCoverageGutterTest.cpp
            Tests/TestDiscoveryTest.cpp
            Tests/TestResultsBufferTest.cpp
            Tests/TestRunConfigTest.cpp
            Tests/TestRunnerTest.cpp
            Tests/TestSourceResolverTest.cpp
            Tests/CoverageOutputParserTest.cpp
            Tests/CoverageReportTest.cpp
            Tests/CoverageConfigTest.cpp
            Tests/JanetTestParserTest.cpp
            Tests/TerminalEmulatorTest.cpp
            Tests/TerminalPanelTest.cpp
            Tests/TreeSitterTest.cpp
            Tests/ParseConformanceTest.cpp
            Tests/DynamicGrammarTest.cpp
            Tests/ModeOverridesTest.cpp
            Tests/ModePrewarmTest.cpp
            Tests/BufferViewSearchInResultsTest.cpp
            Tests/MultibufferTest.cpp
            Tests/NextErrorTest.cpp
            Tests/BufferViewNextErrorTest.cpp
            Tests/BufferViewDiagnosticsBufferTest.cpp
            Tests/BufferViewProjectReplaceTest.cpp
            Tests/BufferViewResultJumpTest.cpp
            Tests/BufferViewEmbeddedLspTest.cpp
            Tests/BufferViewDocumentLinkTest.cpp
            Tests/BufferViewProjectFindReferencesTest.cpp
            Tests/BufferViewHierarchyTest.cpp
            Tests/WrapOverridesTest.cpp
            Tests/BorderTest.cpp
            Tests/ProjectSidebarTest.cpp
            Tests/LeftDockTest.cpp
            Tests/VcsPanelTest.cpp
            Tests/VcsDiffPreviewTest.cpp
            Tests/VimMotionTest.cpp
            Tests/VimTextObjectTest.cpp
            Tests/VimSurroundTest.cpp
            Tests/VimRegistersTest.cpp
            Tests/VimExCommandTest.cpp
            Tests/VimMagicTest.cpp
            Tests/VimEngineTest.cpp
    )

    if (NED_TEST_TERMINAL_INTEGRATION)
        # PtyProcessTest.cpp forkpty's real shells/processes (sh, sleep, ...) --
        # excluded from the default test run since a real controlling-tty/session
        # forkpty isn't guaranteed to behave under every sandbox/CI environment.
        # ProjectSwitchIntegrationTest.cpp calls the real ActivateProjectRoot
        # against an existing directory, which has no test seam over
        # TerminalTabLauncher's real terminal detection -- on a real interactive
        # session (e.g. Konsole with its D-Bus gate enabled) this genuinely opens
        # a new terminal tab pointed at "/tmp" on every run.
        # Configure with -DNED_TEST_TERMINAL_INTEGRATION=ON to include both.
        target_sources(ned_tests PRIVATE
                Tests/PtyProcessTest.cpp
                Tests/ProjectSwitchIntegrationTest.cpp)
    endif()

    target_link_libraries(ned_tests PRIVATE ned_lib Catch2::Catch2WithMain tree-sitter)

    # ThemeKeyDocsTest holds Docs/Themes.md's key reference against the real
    # ui::ThemeKeys() table, so it needs to find the repo from a test binary
    # that runs out of the build tree. Tests-only, deliberately: nothing in
    # ned_lib or the ned binary may depend on where the source happens to be.
    target_compile_definitions(ned_tests PRIVATE NED_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

    catch_discover_tests(ned_tests)
endif()
