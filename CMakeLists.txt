cmake_minimum_required(VERSION 3.26)
project(Ned)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NED_BUILD_TESTS "Build the ned_tests target" ON)
option(NED_ENABLE_SANITIZERS "Build with AddressSanitizer/UndefinedBehaviorSanitizer" OFF)

# Use ccache automatically when it's on PATH -- this project's FetchContent
# deps (TermOx, tree-sitter + grammars, Catch2, utf8proc) make clean rebuilds
# expensive; ccache mainly pays off on incremental/branch-switch rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)

#--- Fetch TermOx ------------------------------------------------------------
FetchContent_Declare(TermOx
        GIT_REPOSITORY https://github.com/a-n-t-h-o-n-y/TermOx.git
        GIT_TAG main
        GIT_PROGRESS TRUE
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(TermOx)
#------------------------------------------------------------------------------

#--- Fetch utf8proc -----------------------------------------------------------
FetchContent_Declare(utf8proc
        GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
        GIT_TAG v2.9.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
# utf8proc's own CMakeLists.txt predates CMake's removal of pre-3.5 compatibility.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(utf8proc)
unset(CMAKE_POLICY_VERSION_MINIMUM)
#------------------------------------------------------------------------------

#--- Fetch tree-sitter (core) --------------------------------------------------
# tree-sitter's own CMakeLists.txt lives at lib/CMakeLists.txt, not the repo
# root (its root has no CMake support at all, only Cargo/Zig/Swift) -- without
# SOURCE_SUBDIR, FetchContent_MakeAvailable silently finds no CMakeLists.txt to
# add, and "tree-sitter" below would silently become a raw, wrong linker flag
# instead of a real CMake target (no configure-time error either way).
FetchContent_Declare(tree-sitter
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
        GIT_TAG v0.25.10
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR lib
        EXCLUDE_FROM_ALL)
# Static, matching every other fetched dependency here -- tree-sitter's own
# default is a shared lib. Scoped to just this fetch, restored after, the same
# "don't leak a one-off override past the dependency it's for" pattern
# utf8proc's own CMAKE_POLICY_VERSION_MINIMUM override already established.
set(BUILD_SHARED_LIBS_PREV ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tree-sitter)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_PREV} CACHE BOOL "" FORCE)
unset(BUILD_SHARED_LIBS_PREV)
# tree-sitter's own target_include_directories call is PRIVATE (src src/wasm
# include), so lib/include (tree_sitter/api.h, the public consumer header)
# doesn't propagate to anything linking against this target -- that's fine
# for its own real install/pkg-config story, but FetchContent consumers never
# run `make install`, so it has to be patched in here instead.
target_include_directories(tree-sitter PUBLIC ${tree-sitter_SOURCE_DIR}/lib/include)
#------------------------------------------------------------------------------

#--- Fetch a tree-sitter grammar -----------------------------------------------
# Every bundled grammar (tree-sitter foundation follow-up, extended by the
# bundle-remaining-grammars follow-up) goes through this one function rather
# than a plain FetchContent_Declare/MakeAvailable pair, because a grammar
# repo's own CMakeLists.txt unconditionally declares an add_custom_command
# that regenerates src/parser.c from src/grammar.json via the tree-sitter CLI
# (a whole separate, Node.js-based toolchain, not installed here and not
# something this project's build should ever require -- the entire point of
# consuming a grammar repo instead of running the CLI ourselves is that
# parser.c already ships pre-generated and committed). Letting
# FetchContent_MakeAvailable add_subdirectory that CMakeLists.txt runs that
# custom command's out-of-date check regardless -- observed to differ by
# generator (Ninja's stricter dependency check triggers a real, hard-failing
# regeneration attempt even though parser.c already exists and is correct;
# Unix Makefiles happened not to, which is exactly the kind of generator-
# dependent luck this project's own CLAUDE.md warns against relying on for
# either of its two build trees).
#
# SOURCE_SUBDIR pointing at a path that doesn't exist in the grammar repo is
# what suppresses FetchContent_MakeAvailable's automatic add_subdirectory --
# confirmed behavior, not a guess: this is the exact same mechanism that
# silently (no configure error) produced a missing "tree-sitter" target
# above before SOURCE_SUBDIR was pointed at the real lib/ subdirectory, now
# deliberately pointed at a bogus one instead. The grammar's source is still
# fully populated (${name}_SOURCE_DIR is set as usual); only add_subdirectory
# is skipped. We then declare our own minimal static-library target
# compiling exactly src/parser.c (+ src/scanner.c, if the grammar needs an
# external scanner), which is all any consumer of a tree-sitter grammar
# actually needs -- matches how every real tree-sitter consumer (Neovim,
# Emacs, ...) treats a grammar: a single tree_sitter_<name>() C symbol,
# nothing else.

# Populates (but does not build) a tree-sitter grammar repo's source via the
# SOURCE_SUBDIR trick described above, without ever processing its own
# CMakeLists.txt. Split out from ned_add_treesitter_grammar below (bundle-
# remaining-grammars follow-up) so a repo housing *multiple* grammars in
# subdirectories of one clone -- tree-sitter-typescript's typescript/+tsx/,
# tree-sitter-php's php/+php_only/ -- fetches once and builds N targets from
# it, instead of cloning the same repository twice.
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

# Declares target_name as a static library compiling grammar_dir/src/parser.c
# (+ src/scanner.c, if the grammar needs an external scanner) -- the actual
# "turn an already-populated grammar source tree into a linkable CMake
# target" step, matching how every real tree-sitter consumer (Neovim,
# Emacs, ...) treats a grammar: a single tree_sitter_<name>() C symbol,
# nothing else.
function(ned_add_treesitter_grammar_target target_name grammar_dir)
    add_library(${target_name} STATIC "${grammar_dir}/src/parser.c")
    if (EXISTS "${grammar_dir}/src/scanner.c")
        target_sources(${target_name} PRIVATE "${grammar_dir}/src/scanner.c")
    endif()
    target_include_directories(${target_name} PRIVATE "${grammar_dir}/src")
    set_target_properties(${target_name} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endfunction()

# Convenience wrapper for the common one-repo-one-grammar case: fetches and
# builds in one call. grammar_subdir (optional 4th arg) is for a grammar
# whose parser.c isn't at the repo root (e.g. tree-sitter-php's real,
# HTML-embedding-capable grammar lives under php/, not the repo root, which
# houses a separate php_only/ variant this project doesn't use).
function(ned_add_treesitter_grammar name repository tag)
    set(grammar_subdir "${ARGN}") # optional trailing positional arg
    ned_fetch_treesitter_source(${name} ${repository} ${tag})
    if (grammar_subdir)
        ned_add_treesitter_grammar_target(${name} "${${name}_SOURCE_DIR}/${grammar_subdir}")
    else()
        ned_add_treesitter_grammar_target(${name} "${${name}_SOURCE_DIR}")
    endif()
    # PARENT_SCOPE only propagates up exactly one function-call level --
    # ned_fetch_treesitter_source's own PARENT_SCOPE set only reaches *this*
    # function's scope, not all the way back to whichever scope called
    # *this* one (usually the top-level CMakeLists.txt, which needs
    # ${name}_SOURCE_DIR to build an ned_embed_treesitter_query() call right
    # after). Re-propagating here is what makes that work -- a real, first-
    # attempt CMake scoping bug, not a hypothetical one (surfaced instantly
    # as a "queries/highlights.scm not found" configure error with an empty
    # path prefix, since the variable silently read as empty one level up).
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# Embeds a query file's content as the definition of
# `ned::editor::treesitter::queries::k<symbol>` (declared by hand in
# Source/Editor/TreeSitter/Queries.h; this only generates the .cpp defining
# it) -- consuming each bundled grammar's own real queries/highlights.scm
# directly, rather than hand-writing highlight rules per language, is the
# whole point of this follow-up (the one exception, JsonMode's original
# small hand-written query from the tree-sitter foundation phase, predates
# this mechanism and is superseded by it). Generated sources accumulate into
# NED_GENERATED_QUERY_SOURCES, added to ned_lib's own source list below,
# rather than target_sources()-ing ned_lib directly here -- ned_lib isn't
# declared as a target yet at this point in the file.
#
# NOTE: this deliberately does NOT use configure_file(... @ONLY) to splice
# the query text into a .cpp.in template -- a real highlights.scm is dense
# with standalone '@' characters (every capture name is "@something"), and
# configure_file's @ONLY mode scans the *entire* templated file for @VAR@
# substitution markers. With real query content in the mix, it would (and
# during development, did) treat two unrelated '@' tokens far apart in the
# file as the start/end of one bogus substitution variable, silently
# deleting everything between them -- surfaced as mangled/truncated raw
# string literals and "stray '@' in program" compile errors, not a
# configure-time failure, so it wasn't obvious what was wrong. Composing the
# output via string(CONCAT)/file(WRITE) instead never re-scans the query
# text for substitution patterns -- a variable reference is expanded exactly
# once, at the call site, not recursively re-interpreted -- so literal '@'
# and ';' characters in the query content pass through untouched.
function(ned_embed_treesitter_query query_file symbol)
    if (NOT EXISTS "${query_file}")
        message(FATAL_ERROR "ned_embed_treesitter_query: ${query_file} not found")
    endif()
    file(READ "${query_file}" query_content)
    # The raw-string delimiter below (13 chars) must stay at or under the
    # standard's 16-character cap on a raw-string d-char-sequence -- an
    # earlier, longer delimiter (NED_TS_QUERY_EOF_MARKER, 23 chars) silently
    # produced an invalid raw string on every one of these files; GCC's
    # actual failure mode wasn't a clean "delimiter too long" error for most
    # of them (only Bash's happened to surface that one directly) but a
    # cascade of stray '@'/unterminated-string errors once the compiler
    # stopped treating the literal as a raw string at all partway through.
    string(CONCAT generated_content
        "// Auto-generated by CMake's ned_embed_treesitter_query (see CMakeLists.txt)\n"
        "// from ${query_file} -- do not edit by hand, changes will be\n"
        "// overwritten on the next configure.\n"
        # `const` at namespace scope has internal linkage by default in
        # C++ (unlike C) -- without an explicit `extern` on this
        # definition, each generated .cpp would get its own private copy
        # invisible to other translation units, leaving Queries.h's
        # `extern const char* const kX;` declarations unsatisfied at link
        # time (surfaced as "undefined reference to ...::kX" even though
        # the symbol is right there in the object file, just as a local
        # `d`-type symbol per nm, not a global one).
        "namespace ned::editor::treesitter::queries {\n"
        "extern const char* const k${symbol} = R\"NED_QUERY_EOF(${query_content})NED_QUERY_EOF\";\n"
        "}\n")
    set(generated_file "${CMAKE_BINARY_DIR}/ned_generated/EmbeddedQuery_${symbol}.cpp")
    file(WRITE "${generated_file}" "${generated_content}")
    set(NED_GENERATED_QUERY_SOURCES "${NED_GENERATED_QUERY_SOURCES}" "${generated_file}" PARENT_SCOPE)
endfunction()

ned_add_treesitter_grammar(tree-sitter-json https://github.com/tree-sitter/tree-sitter-json.git v0.24.8)
ned_embed_treesitter_query("${tree-sitter-json_SOURCE_DIR}/queries/highlights.scm" Json)

ned_add_treesitter_grammar(tree-sitter-c https://github.com/tree-sitter/tree-sitter-c.git v0.24.2)
ned_embed_treesitter_query("${tree-sitter-c_SOURCE_DIR}/queries/highlights.scm" C)

ned_add_treesitter_grammar(tree-sitter-cpp https://github.com/tree-sitter/tree-sitter-cpp.git v0.23.4)
ned_embed_treesitter_query("${tree-sitter-cpp_SOURCE_DIR}/queries/highlights.scm" Cpp)

# php/, not the repo root -- the real grammar, which also handles HTML
# embedded around <?php ?> tags; the repo root houses a separate php_only/
# variant (pure PHP code, no HTML) this project doesn't use, since a real
# .php file is virtually never pure PHP with no surrounding markup.
ned_add_treesitter_grammar(tree-sitter-php https://github.com/tree-sitter/tree-sitter-php.git v0.24.2 php)
ned_embed_treesitter_query("${tree-sitter-php_SOURCE_DIR}/queries/highlights.scm" Php)

ned_add_treesitter_grammar(tree-sitter-javascript https://github.com/tree-sitter/tree-sitter-javascript.git v0.25.0)
ned_embed_treesitter_query("${tree-sitter-javascript_SOURCE_DIR}/queries/highlights.scm" JavaScript)

# One repo, two grammars (typescript/ and tsx/), sharing one top-level
# queries/highlights.scm -- confirmed by checking the actual repo, not
# guessed (tsx/ has no queries/ directory of its own).
ned_fetch_treesitter_source(tree-sitter-typescript-src https://github.com/tree-sitter/tree-sitter-typescript.git v0.23.2)
ned_add_treesitter_grammar_target(tree-sitter-typescript "${tree-sitter-typescript-src_SOURCE_DIR}/typescript")
ned_add_treesitter_grammar_target(tree-sitter-tsx "${tree-sitter-typescript-src_SOURCE_DIR}/tsx")
ned_embed_treesitter_query("${tree-sitter-typescript-src_SOURCE_DIR}/queries/highlights.scm" TypeScript)

ned_add_treesitter_grammar(tree-sitter-html https://github.com/tree-sitter/tree-sitter-html.git v0.23.2)
ned_embed_treesitter_query("${tree-sitter-html_SOURCE_DIR}/queries/highlights.scm" Html)

ned_add_treesitter_grammar(tree-sitter-css https://github.com/tree-sitter/tree-sitter-css.git v0.25.0)
ned_embed_treesitter_query("${tree-sitter-css_SOURCE_DIR}/queries/highlights.scm" Css)

ned_add_treesitter_grammar(tree-sitter-python https://github.com/tree-sitter/tree-sitter-python.git v0.25.0)
ned_embed_treesitter_query("${tree-sitter-python_SOURCE_DIR}/queries/highlights.scm" Python)

ned_add_treesitter_grammar(tree-sitter-bash https://github.com/tree-sitter/tree-sitter-bash.git v0.25.1)
ned_embed_treesitter_query("${tree-sitter-bash_SOURCE_DIR}/queries/highlights.scm" Bash)

# Community grammar, not under the official tree-sitter org -- see
# Languages.cpp's own comment on why this one (over GrayJack/tree-sitter-janet)
# was chosen. No official tree-sitter/tree-sitter-perl exists with a
# pre-generated parser.c (confirmed by checking: upstream only ships
# grammar.js, requiring the Node-based tree-sitter CLI this project's build
# deliberately never depends on) -- Perl is skipped from the bundled set for
# this reason, not an oversight; see ROADMAP.md.
ned_add_treesitter_grammar(tree-sitter-janet-simple https://github.com/sogaiu/tree-sitter-janet-simple.git v0.0.7)
ned_embed_treesitter_query("${tree-sitter-janet-simple_SOURCE_DIR}/queries/highlights.scm" Janet)

# tree-sitter-markdown/, not the repo root -- this repo (the actively
# maintained tree-sitter-grammars org fork of the original, now-stale
# ikatyang one) splits block-level and inline markdown into two separate
# grammars (tree-sitter-markdown/ + tree-sitter-markdown-inline/, mirroring
# typescript/tsx's own split); only the block-level one is bundled here.
# Real markdown inline formatting (emphasis, links, inline code spans)
# showing up unhighlighted inside an otherwise-highlighted block is a known,
# explicit v1 gap, not an oversight -- properly supporting it needs a
# tree-sitter *injection* query (one grammar's tree marking a sub-region to
# be parsed by a second grammar), which this phase doesn't build yet; see
# ROADMAP.md.
ned_add_treesitter_grammar(tree-sitter-markdown https://github.com/tree-sitter-grammars/tree-sitter-markdown.git v0.5.3 tree-sitter-markdown)
ned_embed_treesitter_query("${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/queries/highlights.scm" Markdown)
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

# ned_lib holds everything except main() so ned_tests can link against the real
# editor code without pulling in the application entry point.
add_library(ned_lib STATIC
        ${NED_GENERATED_QUERY_SOURCES}
        Source/Application.cpp
        Source/Text/Rope.cpp
        Source/Text/Grapheme.cpp
        Source/Text/UndoTree.cpp
        Source/Text/KillRing.cpp
        Source/Text/Buffer.cpp
        Source/Text/BufferList.cpp
        Source/Text/Utf8.cpp
        Source/Editor/Key.cpp
        Source/Editor/Command.cpp
        Source/Editor/Keymap.cpp
        Source/Editor/Dispatcher.cpp
        Source/Editor/Commands.cpp
        Source/Editor/ScriptingSession.cpp
        Source/Editor/IncrementalSearch.cpp
        Source/Editor/QueryReplace.cpp
        Source/Editor/MinibufferPrompt.cpp
        Source/Editor/Mode.cpp
        Source/Editor/ModeOverrides.cpp
        Source/Editor/FormatOnSave.cpp
        Source/Editor/TabWidth.cpp
        Source/Editor/ProjectSearch.cpp
        Source/Editor/ProjectReplace.cpp
        Source/Editor/ProjectTree.cpp
        Source/Editor/ProjectRoot.cpp
        Source/Editor/ProjectFileOps.cpp
        Source/Editor/ScratchPad.cpp
        Source/Editor/TreeSitter/Node.cpp
        Source/Editor/TreeSitter/Tree.cpp
        Source/Editor/TreeSitter/Parser.cpp
        Source/Editor/TreeSitter/Query.cpp
        Source/Editor/TreeSitter/Languages.cpp
        Source/Editor/TreeSitter/DynamicGrammar.cpp
        Source/Janet/Value.cpp
        Source/Janet/Environment.cpp
        Source/Janet/EditorBindings.cpp
        Source/Janet/InitFile.cpp
        Source/UI/KeyTranslation.cpp
        Source/UI/BufferView.cpp
        Source/UI/TabBar.cpp
        Source/UI/ScrollArrowButton.cpp
        Source/UI/ProjectSidebar.cpp
        Source/UI/SidebarToggle.cpp
        Source/UI/ModeLine.cpp
        Source/UI/EchoArea.cpp
        Source/UI/Theme.cpp
        Source/UI/ThemeFile.cpp
        Source/UI/TerminalColorProbe.cpp)

target_include_directories(ned_lib
        PUBLIC Source
        PUBLIC ${Janet_INCLUDE_DIRS})

target_link_libraries(ned_lib
        PUBLIC ${Janet_LIBRARIES}
        PUBLIC TermOx
        PUBLIC utf8proc
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
        PRIVATE ${CMAKE_DL_LIBS})

if (NED_ENABLE_SANITIZERS)
    target_compile_options(ned_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ned_lib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ned Source/main.cpp)

target_link_libraries(ned PRIVATE ned_lib)

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
            Tests/BufferTest.cpp
            Tests/BufferListTest.cpp
            Tests/KeyTest.cpp
            Tests/CommandTest.cpp
            Tests/KeymapTest.cpp
            Tests/DispatcherTest.cpp
            Tests/CommandsTest.cpp
            Tests/JanetTestSupport.cpp
            Tests/ValueTest.cpp
            Tests/EnvironmentTest.cpp
            Tests/ScriptingSessionTest.cpp
            Tests/EditorBindingsTest.cpp
            Tests/InitFileTest.cpp
            Tests/KeyTranslationTest.cpp
            Tests/BufferViewTest.cpp
            Tests/ModeLineTest.cpp
            Tests/EchoAreaTest.cpp
            Tests/Utf8Test.cpp
            Tests/IncrementalSearchTest.cpp
            Tests/QueryReplaceTest.cpp
            Tests/ModeTest.cpp
            Tests/PerformanceTest.cpp
            Tests/ThemeTest.cpp
            Tests/ThemeFileTest.cpp
            Tests/TerminalColorProbeTest.cpp
            Tests/ActiveBufferTest.cpp
            Tests/MinibufferPromptTest.cpp
            Tests/TabBarTest.cpp
            Tests/ScrollArrowButtonTest.cpp
            Tests/FormatOnSaveTest.cpp
            Tests/TabWidthTest.cpp
            Tests/ProjectSearchTest.cpp
            Tests/ProjectReplaceTest.cpp
            Tests/ProjectTreeTest.cpp
            Tests/ProjectRootTest.cpp
            Tests/ProjectFileOpsTest.cpp
            Tests/ScratchPadTest.cpp
            Tests/TreeSitterTest.cpp
            Tests/DynamicGrammarTest.cpp
            Tests/ModeOverridesTest.cpp
            Tests/ProjectSidebarTest.cpp
            Tests/SidebarToggleTest.cpp
    )

    target_link_libraries(ned_tests PRIVATE ned_lib Catch2::Catch2WithMain)

    catch_discover_tests(ned_tests)
endif()
