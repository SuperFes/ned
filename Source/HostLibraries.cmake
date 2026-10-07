# ned_core, ned_host and ned-agent: everything a headless host links.
# Included from Source/CMakeLists.txt, and from CMake/StaticAgent.cmake for
# the static agent build, so paths are relative to this file rather than to
# whichever directory includes it. Expects re2::re2,
# nlohmann_json::nlohmann_json and zstd::libzstd to exist already.

# Process, protocol, logging and file plumbing with no terminal, Janet or
# grammar dependency -- what a headless host process links. Nothing here may
# reach UI/ or Text/'s buffer layer; ned_core_link_check enforces that at
# link time.
add_library(ned_core STATIC
        ${CMAKE_CURRENT_LIST_DIR}/Editor/DiagnosticsLog.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/EditorConfig.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/FileWatch.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/GitIgnore.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/PostQueue.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Process/ChildProcess.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Process/StreamedChild.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/ProcessTimeouts.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Project/FileOps.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Project/SearchEngine.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Protocol/Cbor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Protocol/Endpoint.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Protocol/FrameTransport.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Protocol/LineTransport.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/SearchSettings.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/BinaryDetect.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/Charset.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/FileContents.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/FilePreservation.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/LineEnding.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/SavePlan.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Text/WhitespaceHygiene.cpp)

target_include_directories(ned_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Only SearchEngine.cpp includes RE2; its header forward-declares it. Cbor.h
# hands out nlohmann::json, so that one is public.
target_link_libraries(ned_core
        PUBLIC nlohmann_json::nlohmann_json
        PRIVATE re2::re2
        PRIVATE zstd::libzstd)

if (NED_ENABLE_SANITIZERS)
    target_compile_options(ned_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ned_core PUBLIC -fsanitize=address,undefined)
endif()

# Links every ned_core object and nothing else, so a dependency that leaks in
# fails the build as an undefined symbol rather than going unnoticed inside
# ned_lib's link.
add_executable(ned_core_link_check ${CMAKE_CURRENT_LIST_DIR}/Editor/CoreLinkCheck.cpp)
target_link_libraries(ned_core_link_check PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ned_core>")

# The host side of Docs/ClientServerProtocol.md: the dispatcher and its
# method handlers over ned_core, with no terminal, Janet or grammar either.
# ned-agent is this and a main(); linking it is ned_host's own link check.
add_library(ned_host STATIC
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/Agent.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/Codec.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/Dispatcher.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/Fs.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/FsCodec.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/Hello.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/SearchCodec.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Editor/Host/WatchCodec.cpp)

target_link_libraries(ned_host PUBLIC ned_core)

add_executable(ned-agent ${CMAKE_CURRENT_LIST_DIR}/Agent/main.cpp)
target_link_libraries(ned-agent PRIVATE ned_host)
