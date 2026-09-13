# A representative build file: commands, variables, control flow, functions.
cmake_minimum_required(VERSION 3.24)
project(sample LANGUAGES CXX)

set(SAMPLE_SOURCES
    main.cpp
    util.cpp)

option(SAMPLE_TESTS "Build the tests" ON)

function(sample_add_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

add_executable(sample ${SAMPLE_SOURCES})
sample_add_warnings(sample)

if(SAMPLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
elseif(DEFINED ENV{SAMPLE_MINIMAL})
    message(STATUS "minimal build")
endif()

foreach(source IN LISTS SAMPLE_SOURCES)
    message(VERBOSE "source: ${source}")
endforeach()
