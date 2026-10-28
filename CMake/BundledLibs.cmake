# The NED_BUNDLE_LIBS build: Notcurses and Janet from pinned sources, linked
# statically, for packages built on a distribution whose Notcurses lacks
# ned's patches (Patches/notcurses/README.md) or that doesn't package Janet.
# Everything else still comes from the system (CMake/Dependencies.cmake).
# Produces the same ned::notcurses target and Janet_* variables the system
# lookups do, plus janet_bundled for ned_lib to build after.

include(ExternalProject)
include(FetchContent)

#--- Notcurses -------------------------------------------------------------------
# The core library only, as the system build trims it, with the three
# PatchNotcurses*.cmake scripts applied: each fails the configure if upstream
# code moved under it. EXCLUDE_FROM_ALL keeps Notcurses' own install() rules
# out of ned's packages; only the static library ned links gets built.
set(USE_MULTIMEDIA "none" CACHE STRING "" FORCE)
set(USE_DOCTEST OFF CACHE BOOL "" FORCE)
set(USE_PANDOC OFF CACHE BOOL "" FORCE)
set(USE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(USE_POC OFF CACHE BOOL "" FORCE)
set(USE_QRCODEGEN OFF CACHE BOOL "" FORCE)
set(USE_GPM OFF CACHE BOOL "" FORCE)
set(USE_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(BUILD_FFI_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(notcurses
        URL https://github.com/dankamongmen/notcurses/archive/refs/tags/v3.0.17.tar.gz
        URL_HASH SHA256=b0fbe824984fe25b5a16770dbd00b85d44db5d09cc35bd881b95335d0db53128
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/PatchNotcursesNulKey.cmake
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/PatchNotcursesMouseWheel.cmake
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/PatchNotcursesBracketedPaste.cmake
        EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(notcurses)
# Notcurses keeps its include directory PRIVATE; without this, ned would
# compile against whatever system notcurses.h happens to be installed, the
# ABI mismatch Dependencies.cmake warns about.
target_include_directories(notcurses-core-static INTERFACE "${notcurses_SOURCE_DIR}/include" "${notcurses_BINARY_DIR}/include")
# Its static target links DEFLATE_STATIC_LIBRARIES, which its own CMake never
# sets (only DEFLATE_LIBRARIES); the system's shared libdeflate does.
find_library(NED_LIBDEFLATE deflate REQUIRED)
target_link_libraries(notcurses-core-static INTERFACE "${NED_LIBDEFLATE}")
add_library(ned::notcurses ALIAS notcurses-core-static)
#------------------------------------------------------------------------------

#--- Janet ---------------------------------------------------------------------
# Janet bootstraps itself (a first-stage interpreter builds its core image),
# so its own Makefile builds it rather than CMake. Installed into the build
# tree only; libjanet.a is linked into ned and nothing of Janet's is shipped.
set(NED_JANET_PREFIX "${CMAKE_BINARY_DIR}/bundled/janet")
ExternalProject_Add(janet_bundled
        URL https://github.com/janet-lang/janet/archive/refs/tags/v1.32.1.tar.gz
        URL_HASH SHA256=ac74444f1b545830c34738fe9ebb58c865ea4b819b0b0c3124315c646d9ce4cb
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make CC=${CMAKE_C_COMPILER} PREFIX=${NED_JANET_PREFIX}
        INSTALL_COMMAND make CC=${CMAKE_C_COMPILER} PREFIX=${NED_JANET_PREFIX} install
        BUILD_BYPRODUCTS "${NED_JANET_PREFIX}/lib/libjanet.a")
find_package(Threads REQUIRED)
set(Janet_INCLUDE_DIRS "${NED_JANET_PREFIX}/include/janet")
set(Janet_LIBRARIES "${NED_JANET_PREFIX}/lib/libjanet.a" m dl Threads::Threads)
set(Janet_LIBRARY_DIRS "")
#------------------------------------------------------------------------------
