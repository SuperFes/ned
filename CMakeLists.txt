cmake_minimum_required(VERSION 3.26)
project(Ned)

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
        GIT_TAG v3.0.14
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/PatchNotcursesNulKey.cmake
        EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(notcurses)
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

ned_add_treesitter_grammar(tree-sitter-cpp https://github.com/tree-sitter/tree-sitter-cpp.git v0.23.4)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp.scm" Cpp)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/cpp-folds.scm" CppFolds)

ned_add_treesitter_grammar(tree-sitter-php https://github.com/tree-sitter/tree-sitter-php.git v0.24.2 php)

ned_embed_treesitter_query("${tree-sitter-php_SOURCE_DIR}/queries/highlights.scm" Php)

ned_add_treesitter_grammar(tree-sitter-javascript https://github.com/tree-sitter/tree-sitter-javascript.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-javascript_SOURCE_DIR}/queries/highlights.scm" JavaScript)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/javascript-folds.scm" JavaScriptFolds)

ned_fetch_treesitter_source(tree-sitter-typescript-src https://github.com/tree-sitter/tree-sitter-typescript.git v0.23.2)

ned_add_treesitter_grammar_target(tree-sitter-typescript "${tree-sitter-typescript-src_SOURCE_DIR}/typescript")
ned_add_treesitter_grammar_target(tree-sitter-tsx "${tree-sitter-typescript-src_SOURCE_DIR}/tsx")

ned_embed_treesitter_query("${tree-sitter-typescript-src_SOURCE_DIR}/queries/highlights.scm" TypeScript)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/typescript-folds.scm" TypeScriptFolds)

ned_add_treesitter_grammar(tree-sitter-html https://github.com/tree-sitter/tree-sitter-html.git v0.23.2)

ned_embed_treesitter_query("${tree-sitter-html_SOURCE_DIR}/queries/highlights.scm" Html)

ned_add_treesitter_grammar(tree-sitter-css https://github.com/tree-sitter/tree-sitter-css.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-css_SOURCE_DIR}/queries/highlights.scm" Css)

ned_add_treesitter_grammar(tree-sitter-python https://github.com/tree-sitter/tree-sitter-python.git v0.25.0)

ned_embed_treesitter_query("${tree-sitter-python_SOURCE_DIR}/queries/highlights.scm" Python)
ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/queries/python-folds.scm" PythonFolds)

ned_add_treesitter_grammar(tree-sitter-bash https://github.com/tree-sitter/tree-sitter-bash.git v0.25.1)

ned_embed_treesitter_query("${tree-sitter-bash_SOURCE_DIR}/queries/highlights.scm" Bash)

ned_add_treesitter_grammar(tree-sitter-janet-simple https://github.com/sogaiu/tree-sitter-janet-simple.git v0.0.7)

ned_embed_treesitter_query("${tree-sitter-janet-simple_SOURCE_DIR}/queries/highlights.scm" Janet)

ned_add_treesitter_grammar(tree-sitter-markdown https://github.com/tree-sitter-grammars/tree-sitter-markdown.git v0.5.3 tree-sitter-markdown)

ned_embed_treesitter_query("${tree-sitter-markdown_SOURCE_DIR}/tree-sitter-markdown/queries/highlights.scm" Markdown)

ned_add_treesitter_grammar(tree-sitter-markdown-inline https://github.com/tree-sitter-grammars/tree-sitter-markdown.git v0.5.3 tree-sitter-markdown-inline)

ned_embed_treesitter_query("${tree-sitter-markdown-inline_SOURCE_DIR}/tree-sitter-markdown-inline/queries/highlights.scm" MarkdownInline)

ned_add_treesitter_grammar(tree-sitter-org /Development/NED/tree-sitter-ned-org 05b4c7505c452f42330c681a067842d4cb49b174)

ned_embed_treesitter_query("${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/TreeSitter/OrgHighlights.scm" Org)

ned_add_treesitter_grammar(tree-sitter-yaml https://github.com/tree-sitter-grammars/tree-sitter-yaml.git v0.7.2)

ned_embed_treesitter_query("${tree-sitter-yaml_SOURCE_DIR}/queries/highlights.scm" Yaml)

ned_add_treesitter_grammar(tree-sitter-toml https://github.com/tree-sitter-grammars/tree-sitter-toml.git v0.7.0)

ned_embed_treesitter_query("${tree-sitter-toml_SOURCE_DIR}/queries/highlights.scm" Toml)
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
        Source/Text/Utf8.cpp
        Source/Editor/Key.cpp
        Source/Editor/Command.cpp
        Source/Editor/FuzzyMatch.cpp
        Source/Editor/Keymap.cpp
        Source/Editor/Dispatcher.cpp
        Source/Editor/Commands.cpp
        Source/Editor/ScriptingSession.cpp
        Source/Editor/IncrementalSearch.cpp
        Source/Editor/QueryReplace.cpp
        Source/Editor/Register.cpp
        Source/Editor/Rectangle.cpp
        Source/Editor/MinibufferPrompt.cpp
        Source/Editor/Mode.cpp
        Source/Editor/SyntaxTheme.cpp
        Source/Editor/ModeOverrides.cpp
        Source/Editor/WrapOverrides.cpp
        Source/Editor/CodeFold.cpp
        Source/Editor/CodeFoldSettings.cpp
        Source/Editor/Org.cpp
        Source/Editor/Table.cpp
        Source/Editor/Markdown.cpp
        Source/Editor/Link.cpp
        Source/Editor/FinalNewline.cpp
        Source/Editor/FormatOnSave.cpp
        Source/Editor/TabWidth.cpp
        Source/Editor/ThemeSetting.cpp
        Source/Editor/HighlightSettings.cpp
        Source/Editor/MinimapSettings.cpp
        Source/Editor/GitIgnore.cpp
        Source/Editor/ProjectSearch.cpp
        Source/Editor/ProjectAgenda.cpp
        Source/Editor/ProjectReplace.cpp
        Source/Editor/ProjectTree.cpp
        Source/Editor/ProjectRoot.cpp
        Source/Editor/ProjectFileOps.cpp
        Source/Editor/AutoRevert.cpp
        Source/Editor/ScratchPad.cpp
        Source/Editor/Session.cpp
        Source/Editor/ProjectSession.cpp
        Source/Editor/ProjectTrust.cpp
        Source/Editor/Vcs/DiffPatch.cpp
        Source/Editor/Vcs/VcsProviderRegistry.cpp
        Source/Editor/Vcs/VcsRunner.cpp
        Source/Editor/Process/ChildProcess.cpp
        Source/Editor/Lsp/Transport.cpp
        Source/Editor/Lsp/LspClient.cpp
        Source/Editor/Lsp/LspServerConfig.cpp
        Source/Editor/Lsp/LspPosition.cpp
        Source/Editor/Lsp/LspContent.cpp
        Source/Editor/Lsp/LspManager.cpp
        Source/Editor/Tasks/TaskConfig.cpp
        Source/Editor/Tasks/TaskProcess.cpp
        Source/Editor/Tasks/TaskRunner.cpp
        Source/Editor/Dap/DapConfig.cpp
        Source/Editor/Dap/DapClient.cpp
        Source/Editor/Dap/DapManager.cpp
        Source/Editor/TreeSitter/Node.cpp
        Source/Editor/TreeSitter/Tree.cpp
        Source/Editor/TreeSitter/Parser.cpp
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
        Source/UI/TerminalColorProbe.cpp)

target_include_directories(ned_lib
        PUBLIC Source
        PUBLIC ${Janet_INCLUDE_DIRS})

target_link_libraries(ned_lib
        PUBLIC ${Janet_LIBRARIES}
        # FTXUI -> Notcurses migration: FTXUI itself is gone -- every widget
        # (Widget/KeyTranslation/Theme/EventLoop/Layout and every concrete
        # widget through main.cpp's own composition root) is ported onto
        # Notcurses; see ROADMAP.md's migration entry for the full phase
        # history.
        PUBLIC notcurses-core
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
        PRIVATE ${CMAKE_DL_LIBS})

if (NED_ENABLE_SANITIZERS)
    target_compile_options(ned_lib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ned_lib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ned Source/main.cpp)

target_link_libraries(ned PRIVATE ned_lib CLI11::CLI11)

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
            Tests/AutoRevertTest.cpp
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
            Tests/BufferViewVcsStatusTest.cpp
            Tests/WindowManagerTest.cpp
            Tests/ModeLineTest.cpp
            Tests/EchoAreaTest.cpp
            Tests/Utf8Test.cpp
            Tests/IncrementalSearchTest.cpp
            Tests/QueryReplaceTest.cpp
            Tests/ModeTest.cpp
            Tests/CodeFoldTest.cpp
            Tests/OrgTest.cpp
            Tests/TableTest.cpp
            Tests/MarkdownTest.cpp
            Tests/MarkdownHighlightTest.cpp
            Tests/LinkTest.cpp
            Tests/PerformanceTest.cpp
            Tests/ThemeTest.cpp
            Tests/ThemePaletteTest.cpp
            Tests/BundledThemesTest.cpp
            Tests/SyntaxThemeTest.cpp
            Tests/ThemeFileTest.cpp
            Tests/TerminalColorProbeTest.cpp
            Tests/ActiveBufferTest.cpp
            Tests/MinibufferPromptTest.cpp
            Tests/TabBarTest.cpp
            Tests/ScrollArrowButtonTest.cpp
            Tests/FinalNewlineTest.cpp
            Tests/FormatOnSaveTest.cpp
            Tests/TabWidthTest.cpp
            Tests/HighlightSettingsTest.cpp
            Tests/MinimapSettingsTest.cpp
            Tests/MinimapTest.cpp
            Tests/GitIgnoreTest.cpp
            Tests/ProjectSearchTest.cpp
            Tests/ProjectAgendaTest.cpp
            Tests/ProjectReplaceTest.cpp
            Tests/ProjectTreeTest.cpp
            Tests/ProjectRootTest.cpp
            Tests/ProjectFileOpsTest.cpp
            Tests/ScratchPadTest.cpp
            Tests/SessionTest.cpp
            Tests/ProjectSessionTest.cpp
            Tests/ProjectTrustTest.cpp
            Tests/VcsProviderRegistryTest.cpp
            Tests/DiffPatchTest.cpp
            Tests/VcsRunnerTest.cpp
            Tests/LspTransportTest.cpp
            Tests/LspClientTest.cpp
            Tests/LspServerConfigTest.cpp
            Tests/LspPositionTest.cpp
            Tests/LspContentTest.cpp
            Tests/LspManagerTest.cpp
            Tests/DapConfigTest.cpp
            Tests/DapClientTest.cpp
            Tests/DapManagerTest.cpp
            Tests/ChildProcessTest.cpp
            Tests/TaskConfigTest.cpp
            Tests/TaskProcessTest.cpp
            Tests/TaskRunnerTest.cpp
            Tests/TreeSitterTest.cpp
            Tests/DynamicGrammarTest.cpp
            Tests/ModeOverridesTest.cpp
            Tests/WrapOverridesTest.cpp
            Tests/BorderTest.cpp
            Tests/ProjectSidebarTest.cpp
    )

    target_link_libraries(ned_tests PRIVATE ned_lib Catch2::Catch2WithMain)

    catch_discover_tests(ned_tests)
endif()
