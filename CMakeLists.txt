cmake_minimum_required(VERSION 3.29)
project(Ned VERSION 0.5.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NED_BUILD_TESTS "Build the ned_tests target" ON)
option(NED_ENABLE_SANITIZERS "Build with AddressSanitizer/UndefinedBehaviorSanitizer" OFF)

# Use ccache automatically when it's on PATH -- this project's FetchContent
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)

#--- Fetch Notcurses (FTXUI -> Notcurses migration, Phase 0) ------------------
# Core TUI library only -- everything below trims Notcurses down to just what
# this project needs (the terminal I/O + plane/rendering library itself), not
# its bundled test/doc/executable/multimedia tooling, mirroring the same
# "disable everything we don't need" approach already used for utf8proc/json.
set(USE_MULTIMEDIA "none" CACHE STRING "" FORCE)
set(USE_DOCTEST OFF CACHE BOOL "" FORCE)
set(USE_PANDOC OFF CACHE BOOL "" FORCE)
set(USE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(BUILD_FFI_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_POC OFF CACHE BOOL "" FORCE)
set(USE_QRCODEGEN OFF CACHE BOOL "" FORCE)
set(USE_STATIC OFF CACHE BOOL "" FORCE)
# PATCH_COMMAND: legacy-terminal Ctrl+Space (a raw NUL byte) is otherwise
# swallowed inside Notcurses itself -- see CMake/PatchNotcursesNulKey.cmake's
# own header comment for the full story (id 0 collides with notcurses_get's
# "no input" return value, so no caller-side fix exists).
FetchContent_Declare(notcurses
        GIT_REPOSITORY https://github.com/dankamongmen/notcurses.git
        GIT_TAG v3.0.17
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/PatchNotcursesNulKey.cmake
        EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(notcurses)
# real-pixel-graphics-investigation follow-up: notcurses' own CMakeLists.txt
# marks notcurses-core's include directory PRIVATE (its own choice, not a
# mistake on its part -- see that file's target_include_directories(notcurses-core
# BEFORE PRIVATE include ...) call), so target_link_libraries(... notcurses-core)
# alone never propagates a header search path to anything linking against it,
# and EXCLUDE_FROM_ALL above (see that flag's own comment further down this
# file) means notcurses' own install() rules -- the other normal way a
# consumer would find its headers -- never run either. Without this line,
# every #include <notcurses/notcurses.h> in this project silently fell
# through to the system's own installed notcurses headers (wherever a distro
# package happens to provide one) instead of the version actually pinned and
# linked above -- API-compatible enough that everything still compiled clean
# with zero warnings, but a real, confirmed ABI mismatch: enum member order
# in notcurses.h's own ncblitter_e changed between the system's notcurses
# 3.0.8 and the version pinned here, so NCBLIT_PIXEL as this project's own
# code understood it (compiled against the wrong header) and NCBLIT_PIXEL as
# the actually-linked library understood it were two different integers --
# silently asking the real library for a completely different blitter
# (Braille) every single time, the actual root cause of a long, otherwise
# inexplicable minimap-pixel-graphics investigation. Declared INTERFACE (not
# PRIVATE/PUBLIC) since we're adding this to a target this project doesn't
# itself define -- it must reach every consumer transitively via
# target_link_libraries(... notcurses-core), not be used to compile
# notcurses-core a second time.
target_include_directories(notcurses-core INTERFACE "${notcurses_SOURCE_DIR}/include")
#------------------------------------------------------------------------------

#--- Fetch utf8proc -----------------------------------------------------------
FetchContent_Declare(utf8proc
        GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
        GIT_TAG v2.9.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)

# utf8proc's own CMakeLists.txt predates CMake's removal of pre-3.5 compatibility (boo).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(utf8proc)
unset(CMAKE_POLICY_VERSION_MINIMUM)
#------------------------------------------------------------------------------

#--- Fetch CLI11 (command-line parameter handling follow-up) ------------------
FetchContent_Declare(CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.5.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(CLI11)
#------------------------------------------------------------------------------

#--- Fetch nlohmann/json (LSP client follow-up) --------------------------------
FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)
#------------------------------------------------------------------------------

#--- Fetch RE2 (internal-project-search follow-up) ------------------------------
# The regex engine behind the internal, multi-threaded project-search backend
# that replaces the `rg` shell-out (Editor/ProjectSearch.cpp) -- linear-time,
# no-backtracking matching, the same engine-model philosophy `rg` itself uses.
# Pinned to the last tag before RE2's CMake build started hard-requiring
# Abseil (2023-06-01 added `find_package(absl REQUIRED)` with no FetchContent
# fallback of its own) -- pulling in all of Abseil transitively just for
# regex matching isn't worth it here; 2022-12-01 still builds standalone
# against nothing but pthreads.
FetchContent_Declare(re2
        GIT_REPOSITORY https://github.com/google/re2.git
        GIT_TAG 2022-12-01
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(re2)
#------------------------------------------------------------------------------

#--- Fetch PCRE2 (in-file-regex follow-up) -------------------------------------
# The regex engine behind the two user-facing "type your own regex" surfaces
# that edit buffer/file content: query-replace-regexp (Editor/QueryReplace.cpp)
# and project-replace's rewrite step (Editor/ProjectReplace.cpp), both through
# Editor/RegexPattern.h. Deliberately a second engine alongside RE2: RE2's
# linear-time model is right for project *search* (untrusted-ish patterns fanned
# out across every file) but can't do lookaround or backreferences at all --
# PCRE2 provides those plus named groups and real Unicode classes, with JIT for
# speed and a match limit as the backtracking safety net (see RegexPattern.cpp).
FetchContent_Declare(pcre2
        GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
        GIT_TAG pcre2-10.45
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pcre2)
#------------------------------------------------------------------------------

#--- Fetch tree-sitter (core) --------------------------------------------------
FetchContent_Declare(tree-sitter
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
        GIT_TAG v0.25.10
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR lib
        EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS_PREV ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tree-sitter)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_PREV} CACHE BOOL "" FORCE)
unset(BUILD_SHARED_LIBS_PREV)

target_include_directories(tree-sitter PUBLIC ${tree-sitter_SOURCE_DIR}/lib/include)
#------------------------------------------------------------------------------

#--- Fetch libvterm (terminal-panel follow-up) --------------------------------
# libvterm (the VT100/xterm emulator behind Neovim's :terminal and
# emacs-vterm) ships a plain Makefile, not CMake, so this is the same
# fetch-but-don't-configure + hand-built-static-target arrangement the
# tree-sitter grammars use (see ned_fetch_treesitter_source below for why the
# bogus SOURCE_SUBDIR is load-bearing).
FetchContent_Declare(libvterm
        GIT_REPOSITORY https://github.com/neovim/libvterm.git
        GIT_TAG v0.3.3
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR _ned_do_not_configure
        EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libvterm)

# Upstream's Makefile generates src/encoding/*.inc from the *.tbl tables via
# a small Perl script (tbl2inc_c.pl) before compiling encoding.c, which
# #includes them. Rather than adding a Perl configure-time dependency for a
# ~30-line mechanical transform, the same transform is done here in CMake
# (the ned_embed_treesitter_query precedent: generate-with-string-ops beats a
# new tool dependency). Output goes to the binary dir -- encoding.c's quoted
# #include "encoding/X.inc" falls back to the -I search path when the file
# isn't next to it in the source tree, so the fetched tree stays pristine.
function(ned_generate_vterm_inc tbl_file inc_file)
    get_filename_component(enc_name ${tbl_file} NAME_WE)
    # ENCODING UTF-8: file(STRINGS) strips non-ASCII bytes by default, which
    # would silently mangle uk.tbl's "£" entry before the match below.
    file(STRINGS ${tbl_file} tbl_lines ENCODING UTF-8)
    set(body "")
    foreach(line IN LISTS tbl_lines)
        string(REGEX REPLACE "[ \t]*#.*" "" line "${line}")
        if(line MATCHES "^([0-9]+)/([0-9]+) = (.+)$")
            math(EXPR cell "${CMAKE_MATCH_1} * 16 + ${CMAKE_MATCH_2}" OUTPUT_FORMAT HEXADECIMAL)
            set(value "${CMAKE_MATCH_3}")
            if(value MATCHES "^U\\+([0-9A-Fa-f]+)$")
                set(value "0x${CMAKE_MATCH_1}")
            elseif(value STREQUAL "\"£\"")
                # The one quoted-literal entry across both tables (uk.tbl's
                # POUND SIGN); CMake has no ord(), so known literals are
                # mapped here explicitly.
                set(value "0x00a3")
            else()
                message(FATAL_ERROR "ned_generate_vterm_inc: unhandled table entry '${value}' in ${tbl_file}")
            endif()
            string(APPEND body "    [${cell}] = ${value},\n")
        endif()
    endforeach()
    string(CONCAT content
        "// Auto-generated by CMake's ned_generate_vterm_inc (see CMakeLists.txt)\n"
        "static const struct StaticTableEncoding encoding_${enc_name} = {\n"
        "  { .decode = &decode_table },\n"
        "  {\n"
        "${body}"
        "  }\n"
        "};\n")
    file(WRITE ${inc_file} "${content}")
endfunction()

set(NED_VTERM_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/libvterm-generated)
file(GLOB NED_VTERM_TBL_FILES ${libvterm_SOURCE_DIR}/src/encoding/*.tbl)
foreach(tbl ${NED_VTERM_TBL_FILES})
    get_filename_component(tbl_name ${tbl} NAME_WE)
    ned_generate_vterm_inc(${tbl} ${NED_VTERM_GEN_DIR}/encoding/${tbl_name}.inc)
endforeach()

add_library(vterm STATIC
        ${libvterm_SOURCE_DIR}/src/encoding.c
        ${libvterm_SOURCE_DIR}/src/keyboard.c
        ${libvterm_SOURCE_DIR}/src/mouse.c
        ${libvterm_SOURCE_DIR}/src/parser.c
        ${libvterm_SOURCE_DIR}/src/pen.c
        ${libvterm_SOURCE_DIR}/src/screen.c
        ${libvterm_SOURCE_DIR}/src/state.c
        ${libvterm_SOURCE_DIR}/src/unicode.c
        ${libvterm_SOURCE_DIR}/src/vterm.c)
target_include_directories(vterm
        PUBLIC ${libvterm_SOURCE_DIR}/include
        PRIVATE ${NED_VTERM_GEN_DIR})
set_target_properties(vterm PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
#------------------------------------------------------------------------------

#--- Fetch a tree-sitter grammars ---------------------------------------------
function(ned_fetch_treesitter_source content_name repository tag)
    FetchContent_Declare(${content_name}
            GIT_REPOSITORY ${repository}
            GIT_TAG ${tag}
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR _ned_do_not_configure
            EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(${content_name})
    set(${content_name}_SOURCE_DIR "${${content_name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(ned_add_treesitter_grammar_target target_name grammar_dir)
    add_library(${target_name} STATIC "${grammar_dir}/src/parser.c")
    if (EXISTS "${grammar_dir}/src/scanner.c")
        target_sources(${target_name} PRIVATE "${grammar_dir}/src/scanner.c")
    endif()
    target_include_directories(${target_name} PRIVATE "${grammar_dir}/src")
    set_target_properties(${target_name} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endfunction()

function(ned_add_treesitter_grammar name repository tag)
    set(grammar_subdir "${ARGN}") # optional trailing positional arg
    ned_fetch_treesitter_source(${name} ${repository} ${tag})
    if (grammar_subdir)
        ned_add_treesitter_grammar_target(${name} "${${name}_SOURCE_DIR}/${grammar_subdir}")
    else()
        ned_add_treesitter_grammar_target(${name} "${${name}_SOURCE_DIR}")
    endif()

    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(ned_embed_treesitter_query query_file symbol)
    if (NOT EXISTS "${query_file}")
        message(FATAL_ERROR "ned_embed_treesitter_query: ${query_file} not found")
    endif()
    # file(READ) at configure time creates no dependency on its own -- without
    # this, editing an embedded source file silently keeps the stale generated
    # .cpp until something else happens to trigger a reconfigure (a real trap,
    # hit via vcs-git.janet during the VCS vocabulary-completion follow-up).
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${query_file}")
    file(READ "${query_file}" query_content)
    string(CONCAT generated_content
        "// Auto-generated by CMake's ned_embed_treesitter_query (see CMakeLists.txt)\n"
        "// from ${query_file} -- do not edit by hand, changes will be\n"
        "// overwritten on the next configure.\n"
        "namespace ned::editor::treesitter::queries {\n"
        "extern const char* const k${symbol} = R\"NED_QUERY_EOF(${query_content})NED_QUERY_EOF\";\n"
        "}\n")
    set(generated_file "${CMAKE_BINARY_DIR}/ned_generated/EmbeddedQuery_${symbol}.cpp")
    file(WRITE "${generated_file}" "${generated_content}")
    set(NED_GENERATED_QUERY_SOURCES "${NED_GENERATED_QUERY_SOURCES}" "${generated_file}" PARENT_SCOPE)
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

ned_add_treesitter_grammar(tree-sitter-json https://github.com/tree-sitter/tree-sitter-json.git v0.24.8)

ned_embed_treesitter_query("${tree-sitter-json_SOURCE_DIR}/queries/highlights.scm" Json)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/json-folds.scm" JsonFolds)

ned_add_treesitter_grammar(tree-sitter-c https://github.com/tree-sitter/tree-sitter-c.git v0.24.2)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/c.scm" C)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/c-folds.scm" CFolds)
# import-target-tree-sitter follow-up: shared by CMode and CppMode below --
# both grammars define preproc_include identically.
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/c-imports.scm" CImports)
# gutter-symbol-kind follow-up: a repo-local vendored file, not the grammar's
# own real queries/tags.scm -- upstream's @definition.function pattern
# matches any function_declarator regardless of context, which is ambiguous
# with C's "most vexing parse" (a local variable declared with
# constructor-call-style syntax parses identically to a function
# prototype/definition); c-tags.scm's own header comment has the full story,
# confirmed against a real false positive (see BufferViewSymbolGutterTest.cpp).
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/c-tags.scm" CTags)

ned_add_treesitter_grammar(tree-sitter-cpp https://github.com/tree-sitter/tree-sitter-cpp.git v0.23.4)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp.scm" Cpp)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp-folds.scm" CppFolds)
# Same vendored-not-upstream reasoning as CTags above -- see cpp-tags.scm's
# own header comment.
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp-tags.scm" CppTags)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp-tests.scm" CppTests)

ned_add_treesitter_grammar(tree-sitter-php https://github.com/tree-sitter/tree-sitter-php.git v0.24.2 php)

ned_embed_treesitter_query("${tree-sitter-php_SOURCE_DIR}/queries/highlights.scm" Php)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/php-imports.scm" PhpImports)
# gutter-symbol-kind follow-up: the grammar's own real queries/tags.scm
# (ctags/nvim-treesitter's own "@definition.*"/"@reference.*" convention),
# consumed unmodified -- same "use the real upstream file directly" approach
# highlights.scm/folds.scm already establish for grammars without local
# customization (Source/Editor/TreeSitter/Queries.h). Unlike CTags/CppTags
# above, PHP's own tags.scm has no "most vexing parse"-style ambiguity to fix
# -- see c-tags.scm's own header comment for why those two needed vendoring.
ned_embed_treesitter_query("${tree-sitter-php_SOURCE_DIR}/queries/tags.scm" PhpTags)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/php-tests.scm" PhpTests)

ned_add_treesitter_grammar(tree-sitter-javascript https://github.com/tree-sitter/tree-sitter-javascript.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-javascript_SOURCE_DIR}/queries/highlights.scm" JavaScript)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/javascript-folds.scm" JavaScriptFolds)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/javascript-imports.scm" JavaScriptImports)
# gutter-symbol-kind follow-up: see PhpTags' own comment above -- the
# grammar's real, unmodified queries/tags.scm; no vendoring needed here
# either.
ned_embed_treesitter_query("${tree-sitter-javascript_SOURCE_DIR}/queries/tags.scm" JavaScriptTags)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/javascript-tests.scm" JavaScriptTests)

ned_fetch_treesitter_source(tree-sitter-typescript-src https://github.com/tree-sitter/tree-sitter-typescript.git v0.23.2)

ned_add_treesitter_grammar_target(tree-sitter-typescript "${tree-sitter-typescript-src_SOURCE_DIR}/typescript")
ned_add_treesitter_grammar_target(tree-sitter-tsx "${tree-sitter-typescript-src_SOURCE_DIR}/tsx")

ned_embed_treesitter_query("${tree-sitter-typescript-src_SOURCE_DIR}/queries/highlights.scm" TypeScript)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/typescript-folds.scm" TypeScriptFolds)
# import-target-tree-sitter follow-up: shared by TypeScriptMode and TsxMode,
# same sharing as the highlight/fold queries above.
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/typescript-imports.scm" TypeScriptImports)
# gutter-symbol-kind follow-up: see PhpTags' own comment above; shared by
# TypeScriptMode and TsxMode, same sharing as the highlight/fold/imports
# queries above.
ned_embed_treesitter_query("${tree-sitter-typescript-src_SOURCE_DIR}/queries/tags.scm" TypeScriptTags)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/typescript-tests.scm" TypeScriptTests)

ned_add_treesitter_grammar(tree-sitter-html https://github.com/tree-sitter/tree-sitter-html.git v0.23.2)

ned_embed_treesitter_query("${tree-sitter-html_SOURCE_DIR}/queries/highlights.scm" Html)

ned_add_treesitter_grammar(tree-sitter-css https://github.com/tree-sitter/tree-sitter-css.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-css_SOURCE_DIR}/queries/highlights.scm" Css)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/css-imports.scm" CssImports)

ned_add_treesitter_grammar(tree-sitter-python https://github.com/tree-sitter/tree-sitter-python.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-python_SOURCE_DIR}/queries/highlights.scm" Python)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/python-folds.scm" PythonFolds)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/python-imports.scm" PythonImports)
# gutter-symbol-kind follow-up: see PhpTags' own comment above.
ned_embed_treesitter_query("${tree-sitter-python_SOURCE_DIR}/queries/tags.scm" PythonTags)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/python-tests.scm" PythonTests)

ned_add_treesitter_grammar(tree-sitter-bash https://github.com/tree-sitter/tree-sitter-bash.git v0.25.1)

ned_embed_treesitter_query("${tree-sitter-bash_SOURCE_DIR}/queries/highlights.scm" Bash)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/bash-imports.scm" BashImports)

ned_add_treesitter_grammar(tree-sitter-janet-simple https://github.com/sogaiu/tree-sitter-janet-simple.git v0.0.7)

ned_embed_treesitter_query("${tree-sitter-janet-simple_SOURCE_DIR}/queries/highlights.scm" Janet)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/janet-imports.scm" JanetImports)

ned_add_treesitter_grammar(tree-sitter-markdown https://github.com/tree-sitter-grammars/tree-sitter-markdown.git v0.5.3 tree-sitter-markdown)

ned_embed_treesitter_query("${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/queries/highlights.scm" Markdown)

ned_add_treesitter_grammar(tree-sitter-markdown-inline https://github.com/tree-sitter-grammars/tree-sitter-markdown.git v0.5.3 tree-sitter-markdown-inline)

ned_embed_treesitter_query("${tree-sitter-markdown-inline_SOURCE_DIR}/tree-sitter-markdown-inline/queries/highlights.scm" MarkdownInline)

ned_add_treesitter_grammar(tree-sitter-org https://github.com/SuperFes/tree-sitter-ned-org.git 05b4c7505c452f42330c681a067842d4cb49b174)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/OrgHighlights.scm" Org)

ned_add_treesitter_grammar(tree-sitter-yaml https://github.com/tree-sitter-grammars/tree-sitter-yaml.git v0.7.2)

ned_embed_treesitter_query("${tree-sitter-yaml_SOURCE_DIR}/queries/highlights.scm" Yaml)

ned_add_treesitter_grammar(tree-sitter-toml https://github.com/tree-sitter-grammars/tree-sitter-toml.git v0.7.0)

ned_embed_treesitter_query("${tree-sitter-toml_SOURCE_DIR}/queries/highlights.scm" Toml)

# sogaiu/tree-sitter-clojure (same author as tree-sitter-janet-simple above);
# one grammar serving both ClojureMode and JankMode -- jank is a Clojure
# dialect with no tree-sitter grammar of its own anywhere (checked the
# jank-lang GitHub org and a repo search directly, not assumed). The embedded
# query is a repo-local vendored file, not the grammar's own
# queries/highlights.scm -- upstream's is 281 bytes (literals/comments only);
# see Source/Editor/TreeSitter/queries/clojure.scm's header comment.
ned_add_treesitter_grammar(tree-sitter-clojure https://github.com/sogaiu/tree-sitter-clojure.git v0.0.13)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/clojure.scm" Clojure)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/clojure-folds.scm" ClojureFolds)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/clojure-imports.scm" ClojureImports)

# ram02z/tree-sitter-fish -- the grammar every real consumer (nvim-treesitter)
# builds on, ships a pre-generated src/parser.c+scanner.c and a real
# queries/highlights.scm, the same bar every other bundled grammar here meets.
ned_add_treesitter_grammar(tree-sitter-fish https://github.com/ram02z/tree-sitter-fish.git 3.7.0)

ned_embed_treesitter_query("${tree-sitter-fish_SOURCE_DIR}/queries/highlights.scm" Fish)

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
ned_add_treesitter_grammar(tree-sitter-xml https://github.com/tree-sitter-grammars/tree-sitter-xml.git v0.7.0 xml)

ned_embed_treesitter_query("${tree-sitter-xml_SOURCE_DIR}/queries/xml/highlights.scm" Xml)
#------------------------------------------------------------------------------

#--- Fetch Catch2 --------------------------------------------------------------
if (NED_BUILD_TESTS)
    FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
#------------------------------------------------------------------------------

include(FindPkgConfig)

pkg_check_modules(Janet REQUIRED janet)

# ned_lib holds everything except main()
add_library(ned_lib STATIC
        ${NED_GENERATED_QUERY_SOURCES}
        Source/Application.cpp
        Source/Text/Rope.cpp
        Source/Text/Grapheme.cpp
        Source/Text/UndoTree.cpp
        Source/Text/KillRing.cpp
        Source/Text/Buffer.cpp
        Source/Text/BufferList.cpp
        Source/Text/BinaryDetect.cpp
        Source/Text/ThreeWayMerge.cpp
        Source/Text/Utf8.cpp
        Source/Editor/Key.cpp
        Source/Editor/Command.cpp
        Source/Editor/FuzzyMatch.cpp
        Source/Editor/Keymap.cpp
        Source/Editor/Dispatcher.cpp
        Source/Editor/Commands.cpp
        Source/Editor/ScriptingSession.cpp
        Source/Editor/IncrementalSearch.cpp
        Source/Editor/PrefixArgument.cpp
        Source/Editor/QueryReplace.cpp
        Source/Editor/RegexPattern.cpp
        Source/Editor/Register.cpp
        Source/Editor/Rectangle.cpp
        Source/Editor/AutoPair.cpp
        Source/Editor/DabbrevComplete.cpp
        Source/Editor/JanetSymbolComplete.cpp
        Source/Editor/MinibufferPrompt.cpp
        Source/Editor/PromptHistory.cpp
        Source/Editor/Mode.cpp
        Source/Editor/SyntaxTheme.cpp
        Source/Editor/ModeOverrides.cpp
        Source/Editor/ModePrewarm.cpp
        Source/Editor/Multibuffer.cpp
        Source/Editor/WrapOverrides.cpp
        Source/Editor/CodeFold.cpp
        Source/Editor/CodeFoldSettings.cpp
        Source/Editor/Org.cpp
        Source/Editor/OrgCapture.cpp
        Source/Editor/Snippet.cpp
        Source/Editor/SnippetRegistry.cpp
        Source/Editor/Table.cpp
        Source/Editor/Markdown.cpp
        Source/Editor/Link.cpp
        Source/Editor/HeaderSource.cpp
        Source/Editor/FinalNewline.cpp
        Source/Editor/TrimOnSave.cpp
        Source/Editor/FormatOnSave.cpp
        Source/Editor/Clipboard.cpp
        Source/Editor/TabWidth.cpp
        Source/Editor/PageScroll.cpp
        Source/Editor/DiffRefreshSettings.cpp
        Source/Editor/ThemeSetting.cpp
        Source/Editor/Variables.cpp
        Source/Editor/HighlightSettings.cpp
        Source/Editor/MinimapSettings.cpp
        Source/Editor/WhitespaceSettings.cpp
        Source/Editor/GitIgnore.cpp
        Source/Editor/SearchSettings.cpp
        Source/Editor/ProjectSearch.cpp
        Source/Editor/ProjectAgenda.cpp
        Source/Editor/ProjectReplace.cpp
        Source/Editor/ProjectTree.cpp
        Source/Editor/ProjectRoot.cpp
        Source/Editor/ProjectSettings.cpp
        Source/Editor/ToolchainIncludePaths.cpp
        Source/Editor/NodeModules.cpp
        Source/Editor/ImportResolutionConfig.cpp
        Source/Editor/ProjectFileOps.cpp
        Source/Editor/AutoRevert.cpp
        Source/Editor/AutoMerge.cpp
        Source/Editor/FileWatch.cpp
        Source/Editor/BackgroundActivity.cpp
        Source/Editor/InlineDiagnostics.cpp
        Source/Editor/ScratchPad.cpp
        Source/Editor/Backup.cpp
        Source/Editor/DiagnosticsLog.cpp
        Source/Editor/Session.cpp
        Source/Editor/ProjectSession.cpp
        Source/Editor/ProjectTrust.cpp
        Source/Editor/ProjectPlugins.cpp
        Source/Editor/Vim/VimSettings.cpp
        Source/Editor/Vim/VimMotion.cpp
        Source/Editor/Vim/VimTextObject.cpp
        Source/Editor/Vim/VimRegisters.cpp
        Source/Editor/Vim/VimExCommand.cpp
        Source/Editor/Vim/VimEngine.cpp
        Source/Editor/Vcs/DiffPatch.cpp
        Source/Editor/Vcs/VcsProviderRegistry.cpp
        Source/Editor/Vcs/VcsRunner.cpp
        Source/Editor/Process/ChildProcess.cpp
        Source/Editor/Acp/Transport.cpp
        Source/Editor/Acp/AcpClient.cpp
        Source/Editor/Acp/AcpConfig.cpp
        Source/Editor/Acp/AcpPanelConfig.cpp
        Source/Editor/Acp/AcpManager.cpp
        Source/Editor/Lsp/Transport.cpp
        Source/Editor/Lsp/LspClient.cpp
        Source/Editor/Lsp/LspServerConfig.cpp
        Source/Editor/Lsp/ProseChecker.cpp
        Source/Editor/Lsp/LspPosition.cpp
        Source/Editor/Lsp/LspContent.cpp
        Source/Editor/Lsp/LspManager.cpp
        Source/Editor/Tasks/TaskConfig.cpp
        Source/Editor/Tasks/TaskProcess.cpp
        Source/Editor/Tasks/TaskRunner.cpp
        Source/Editor/TestRun/TestOutputParser.cpp
        Source/Editor/TestRun/TestResult.cpp
        Source/Editor/TestRun/TestResultsBuffer.cpp
        Source/Editor/TestRun/TestRunConfig.cpp
        Source/Editor/TestRun/TestRunner.cpp
        Source/Editor/Dap/DapConfig.cpp
        Source/Editor/Dap/DapClient.cpp
        Source/Editor/Dap/DapManager.cpp
        Source/Editor/Terminal/Config.cpp
        Source/Editor/Terminal/Emulator.cpp
        Source/Editor/Terminal/PtyProcess.cpp
        Source/Editor/TreeSitter/Node.cpp
        Source/Editor/TreeSitter/Tree.cpp
        Source/Editor/TreeSitter/Parser.cpp
        Source/Editor/TreeSitter/IncrementalParse.cpp
        Source/Editor/TreeSitter/Query.cpp
        Source/Editor/TreeSitter/Languages.cpp
        Source/Editor/TreeSitter/DynamicGrammar.cpp
        Source/Janet/Value.cpp
        Source/Janet/Environment.cpp
        Source/Janet/EditorBindings.cpp
        Source/Janet/JanetVcsProvider.cpp
        Source/Janet/PluginLoader.cpp
        Source/Janet/InitFile.cpp
        Source/UI/KeyTranslation.cpp
        Source/UI/Border.cpp
        Source/UI/Widget.cpp
        Source/UI/EventLoop.cpp
        Source/UI/Layout.cpp
        Source/UI/ScrollBar.cpp
        Source/UI/Minimap.cpp
        Source/UI/Overlay.cpp
        Source/UI/TerminalPanel.cpp
        Source/UI/AcpPanel.cpp
        Source/UI/DebugConsolePanel.cpp
        Source/UI/BufferView.cpp
        Source/UI/WindowManager.cpp
        Source/UI/AsyncFileLoader.cpp
        Source/UI/TabBar.cpp
        Source/UI/ScrollArrowButton.cpp
        Source/UI/ProjectSidebar.cpp
        Source/UI/ModeLine.cpp
        Source/UI/EchoArea.cpp
        Source/UI/Theme.cpp
        Source/UI/ThemePalette.cpp
        Source/UI/ThemeRegistry.cpp
        Source/UI/ThemeFile.cpp
        Source/UI/TerminalColorProbe.cpp
        Source/UI/DesktopThemeProbe.cpp)

target_include_directories(ned_lib
        PUBLIC Source
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
        PUBLIC tree-sitter
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
# notcurses-core is FetchContent'd with EXCLUDE_FROM_ALL (see its own
# comment above), which also silently skips notcurses' own install(TARGETS
# notcurses-core ...) rule even though the shared library itself still gets
# built (as a dependency of ned_lib) -- confirmed empirically, not assumed:
# a plain `cmake --install` installed only `ned`, and the resulting binary
# fell back to whatever notcurses happens to already be on the host system
# (a real, silent version/ABI mismatch risk, not just a missing-file one --
# ned is built and tested against a specific pinned notcurses commit/patch,
# see PatchNotcursesNulKey.cmake, that a system package won't carry).
# Installed into a private, ned-only subdirectory rather than the shared
# system libdir -- CMAKE_INSTALL_LIBDIR is also where a system notcurses
# package installs its own same-SONAME libnotcurses-core.so.3, and two
# packages both owning that exact path is a real install-time file
# collision, not just an unlikely edge case (this machine already has one
# installed). INSTALL_RPATH is what makes the *installed* copy of `ned`
# look there -- deliberately not touching the build tree's own rpath, so
# `cmake --build build && ./build/ned` (this project's documented dev
# workflow) keeps resolving notcurses-core from build/_deps/ exactly as
# before.
set_target_properties(ned PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/ned"
)
install(TARGETS ned RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS notcurses-core LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/ned")

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

if (NED_BUILD_TESTS)
    enable_testing()
    include(CTest)
    include(Catch)

    add_executable(ned_tests
            Tests/ApplicationTest.cpp
            Tests/RopeTest.cpp
            Tests/GraphemeTest.cpp
            Tests/UndoTreeTest.cpp
            Tests/KillRingTest.cpp
            Tests/RegisterTest.cpp
            Tests/RectangleTest.cpp
            Tests/AutoPairTest.cpp
            Tests/DabbrevCompleteTest.cpp
            Tests/SnippetTest.cpp
            Tests/SnippetRegistryTest.cpp
            Tests/JanetSymbolCompleteTest.cpp
            Tests/AutoRevertTest.cpp
            Tests/AutoMergeTest.cpp
            Tests/FileWatchTest.cpp
            Tests/ThreeWayMergeTest.cpp
            Tests/BufferTest.cpp
            Tests/MultiCursorTest.cpp
            Tests/BufferListTest.cpp
            Tests/BinaryDetectTest.cpp
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
            Tests/BufferViewSymbolGutterTest.cpp
            Tests/BufferViewVcsStatusTest.cpp
            Tests/BufferViewVcsCommitTest.cpp
            Tests/WindowManagerTest.cpp
            Tests/ModeLineTest.cpp
            Tests/EchoAreaTest.cpp
            Tests/Utf8Test.cpp
            Tests/IncrementalSearchTest.cpp
            Tests/PrefixArgumentTest.cpp
            Tests/QueryReplaceTest.cpp
            Tests/RegexPatternTest.cpp
            Tests/ModeTest.cpp
            Tests/CodeFoldTest.cpp
            Tests/OrgTest.cpp
            Tests/OrgCaptureTest.cpp
            Tests/OrgClockTest.cpp
            Tests/TableTest.cpp
            Tests/MarkdownTest.cpp
            Tests/MarkdownHighlightTest.cpp
            Tests/LinkTest.cpp
            Tests/HeaderSourceTest.cpp
            Tests/PerformanceTest.cpp
            Tests/ThemeTest.cpp
            Tests/ThemePaletteTest.cpp
            Tests/VariablesTest.cpp
            Tests/BundledThemesTest.cpp
            Tests/SyntaxThemeTest.cpp
            Tests/ThemeFileTest.cpp
            Tests/TerminalColorProbeTest.cpp
            Tests/DesktopThemeProbeTest.cpp
            Tests/ActiveBufferTest.cpp
            Tests/MinibufferPromptTest.cpp
            Tests/PromptHistoryTest.cpp
            Tests/TabBarTest.cpp
            Tests/ScrollArrowButtonTest.cpp
            Tests/FinalNewlineTest.cpp
            Tests/TrimOnSaveTest.cpp
            Tests/FormatOnSaveTest.cpp
            Tests/ClipboardTest.cpp
            Tests/ClipboardTestGuard.cpp
            Tests/TabWidthTest.cpp
            Tests/PageScrollTest.cpp
            Tests/DiffRefreshSettingsTest.cpp
            Tests/HighlightSettingsTest.cpp
            Tests/MinimapSettingsTest.cpp
            Tests/WhitespaceSettingsTest.cpp
            Tests/MinimapTest.cpp
            Tests/OverlayTest.cpp
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
            Tests/ProjectFileOpsTest.cpp
            Tests/ScratchPadTest.cpp
            Tests/BackupTest.cpp
            Tests/DiagnosticsLogTest.cpp
            Tests/SessionTest.cpp
            Tests/ProjectSessionTest.cpp
            Tests/ProjectTrustTest.cpp
            Tests/ProjectPluginsTest.cpp
            Tests/VcsProviderRegistryTest.cpp
            Tests/DiffPatchTest.cpp
            Tests/VcsRunnerTest.cpp
            Tests/BackgroundActivityTest.cpp
            Tests/AcpTransportTest.cpp
            Tests/AcpClientTest.cpp
            Tests/AcpConfigTest.cpp
            Tests/AcpManagerTest.cpp
            Tests/AcpPanelTest.cpp
            Tests/LspTransportTest.cpp
            Tests/LspClientTest.cpp
            Tests/LspServerConfigTest.cpp
            Tests/ProseCheckerTest.cpp
            Tests/ProseCheckerTestGuard.cpp
            Tests/LspPositionTest.cpp
            Tests/LspContentTest.cpp
            Tests/LspManagerTest.cpp
            Tests/DapConfigTest.cpp
            Tests/DapClientTest.cpp
            Tests/DapManagerTest.cpp
            Tests/DebugConsolePanelTest.cpp
            Tests/ChildProcessTest.cpp
            Tests/TaskConfigTest.cpp
            Tests/TaskProcessTest.cpp
            Tests/TaskRunnerTest.cpp
            Tests/TestOutputParserTest.cpp
            Tests/BufferViewTestGutterTest.cpp
            Tests/TestDiscoveryTest.cpp
            Tests/TestResultsBufferTest.cpp
            Tests/TestRunConfigTest.cpp
            Tests/TestRunnerTest.cpp
            Tests/JanetTestParserTest.cpp
            Tests/PtyProcessTest.cpp
            Tests/TerminalEmulatorTest.cpp
            Tests/TerminalPanelTest.cpp
            Tests/TreeSitterTest.cpp
            Tests/DynamicGrammarTest.cpp
            Tests/ModeOverridesTest.cpp
            Tests/ModePrewarmTest.cpp
            Tests/MultibufferTest.cpp
            Tests/BufferViewDiagnosticsBufferTest.cpp
            Tests/BufferViewProjectFindReferencesTest.cpp
            Tests/WrapOverridesTest.cpp
            Tests/BorderTest.cpp
            Tests/ProjectSidebarTest.cpp
            Tests/VimMotionTest.cpp
            Tests/VimTextObjectTest.cpp
            Tests/VimRegistersTest.cpp
            Tests/VimExCommandTest.cpp
            Tests/VimEngineTest.cpp
    )

    target_link_libraries(ned_tests PRIVATE ned_lib Catch2::Catch2WithMain)

    catch_discover_tests(ned_tests)
endif()
