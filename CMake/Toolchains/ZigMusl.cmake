# Cross-compiles for static musl with zig's bundled clang, musl and libc++,
# for the static agent build (CMake/StaticAgent.cmake). One way to get a
# musl toolchain, not a requirement: any toolchain that targets musl works
# there. Pick the architecture with NED_AGENT_ARCH (x86_64 or aarch64).

set(CMAKE_SYSTEM_NAME Linux)
if (NOT NED_AGENT_ARCH)
    set(NED_AGENT_ARCH x86_64)
endif ()
set(CMAKE_SYSTEM_PROCESSOR ${NED_AGENT_ARCH})
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES NED_AGENT_ARCH)

find_program(NED_ZIG zig REQUIRED)

# CMake wants each tool as one executable, and zig is one binary with
# subcommands, so each gets a wrapper. configure_file only rewrites one
# whose content changed, which keeps a reconfigure from looking like a new
# compiler.
set(_ned_zig_target "${NED_AGENT_ARCH}-linux-musl")
set(_ned_zig_wrappers "${CMAKE_BINARY_DIR}/zig-${_ned_zig_target}")
foreach (_tool cc c++ ar ranlib)
    if (_tool STREQUAL "cc" OR _tool STREQUAL "c++")
        set(_args "${_tool} -target ${_ned_zig_target}")
    else ()
        set(_args "${_tool}")
    endif ()
    file(WRITE "${_ned_zig_wrappers}/${_tool}.in" "#!/bin/sh\nexec \"${NED_ZIG}\" ${_args} \"$@\"\n")
    configure_file("${_ned_zig_wrappers}/${_tool}.in" "${_ned_zig_wrappers}/${_tool}" COPYONLY
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach ()

set(CMAKE_C_COMPILER "${_ned_zig_wrappers}/cc")
set(CMAKE_CXX_COMPILER "${_ned_zig_wrappers}/c++")
set(CMAKE_AR "${_ned_zig_wrappers}/ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_ned_zig_wrappers}/ranlib" CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# zig's linker has no --dependency-file.
set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)
