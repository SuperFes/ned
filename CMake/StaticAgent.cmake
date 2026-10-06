# The NED_AGENT_ONLY build: ned-agent alone, linked fully static so it runs
# on any Linux of its architecture whatever libc that host has
# (Docs/ClientServerProtocol.md, section 4). The configured toolchain must
# target musl; CMake/Toolchains/ZigMusl.cmake is one that does, and the
# agent-* presets use it.
#
# Dependencies are built here from pinned sources rather than found on the
# system: a cross build can't link the host's packages. They're the
# versions the editor itself links, so the agent's RE2 matches exactly
# what a local search does.

include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_BUILD_TESTING OFF)
# re2's install rules export its absl dependencies, so absl's must exist
# too, though nothing here is ever installed.
set(ABSL_ENABLE_INSTALL ON)
FetchContent_Declare(absl
        URL https://github.com/abseil/abseil-cpp/archive/20260107.1.tar.gz
        URL_HASH SHA256=4314e2a7cbac89cac25a2f2322870f343d81579756ceff7f431803c2c9090195
        EXCLUDE_FROM_ALL
        OVERRIDE_FIND_PACKAGE)

set(RE2_BUILD_TESTING OFF)
set(RE2_USE_ICU OFF)
FetchContent_Declare(re2
        URL https://github.com/google/re2/releases/download/2025-08-12/re2-2025-08-12.tar.gz
        URL_HASH SHA256=2f3bec634c3e51ea1faf0d441e0a8718b73ef758d7020175ed7e352df3f6ae12
        EXCLUDE_FROM_ALL)

set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake
        EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF)
set(JSON_Install OFF)
FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/archive/v3.12.0.tar.gz
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
        EXCLUDE_FROM_ALL)

# absl first: re2's find_package(absl) resolves to it.
FetchContent_MakeAvailable(absl re2 zstd nlohmann_json)

add_library(zstd::libzstd ALIAS libzstd_static)

include(${CMAKE_CURRENT_LIST_DIR}/../Source/HostLibraries.cmake)

target_link_options(ned-agent PRIVATE -static $<$<CONFIG:Release,MinSizeRel>:-s>)

# Where the editor looks for agents to deploy: libexec/ned/agent/<arch>/,
# named as `uname -m` names the architecture. `cmake --install <dir>
# --component agent --prefix <editor prefix>` puts it there, including the
# editor's own build tree.
include(GNUInstallDirs)
install(TARGETS ned-agent RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/ned/agent/${CMAKE_SYSTEM_PROCESSOR}" COMPONENT agent)
