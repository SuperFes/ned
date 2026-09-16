cmake_minimum_required(VERSION 3.29)
project(Ned VERSION 0.7.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# project(Ned VERSION ...) above is the single source of truth for ned's
# version; this is what makes it reachable from C++ (`ned --version`). Without
# it the CMake version was metadata nothing consumed, which is how it came to
# read 0.5.0 while the only git tag read v0.1.0 and the binary could report
# neither. Generated into ned_generated/ alongside the embedded-query sources,
# and added to ned_lib's PUBLIC include path in Source/CMakeLists.txt, so the
# `ned` executable picks it up through the link. configure_file (rather than
# the file(WRITE) the query embedders use) leaves the output's timestamp alone
# when nothing changed, so a reconfigure doesn't rebuild every translation
# unit that includes it.
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

# System library/package lookups (Notcurses, utf8proc, CLI11, nlohmann/json,
# RE2, PCRE2, libvterm, Catch2, Janet) -- see CMake/Dependencies.cmake's own
# header comment.
include(CMake/Dependencies.cmake)

# tree-sitter core (test-only conformance reference) plus every bundled
# grammar -- see CMake/Grammars.cmake's own header comment. Must run before
# add_subdirectory(Source), which links ned_lib against the grammar targets
# this creates.
include(CMake/Grammars.cmake)

# Embeds bundled Janet plugin/language-query source as C++ constants -- see
# CMake/EmbedGenerated.cmake's own header comment. Populates
# NED_GENERATED_QUERY_SOURCES, which Source/CMakeLists.txt adds to ned_lib.
include(CMake/EmbedGenerated.cmake)

# ned_lib (everything except main()) and the ned executable.
add_subdirectory(Source)

# Standalone Notcurses/terminal-capability diagnostics -- not part of
# ned/ned_lib, see Tools/CMakeLists.txt.
add_subdirectory(Tools)

if (NED_BUILD_TESTS)
    enable_testing()
    include(CTest)
    include(Catch)
    add_subdirectory(Tests)
endif()
