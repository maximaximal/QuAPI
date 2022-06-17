cmake_minimum_required(VERSION 3.9.4)
project(quapi)

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    message(FATAL_ERROR "Currently, QuAPI only supports Linux! In the future, The BSDs (FreeBSD, etc) and MacOS may be supported. Windows has too little support for preloading libraries reliably. This system is ${CMAKE_SYSTEM_NAME}")
endif()

include(GenerateExportHeader)

set(CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/"
    ${CMAKE_MODULE_PATH})

# Required PCRE2 Library for SAT and UNSAT matching
find_package(PCRE2)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

option(DEBUG_ENABLE_ADDRESS_SANITIZER "enable address sanitizer" OFF)
option(ENABLE_ZEROCOPY "enable \"zerocopy\" using vmsplice, splice, memfd, and mmap for pipe communication" OFF)

set(LIBASAN_PATH "")

if(DEBUG_ENABLE_ADDRESS_SANITIZER)
    set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

    # If the address sanitizer is on, LD_PRELOAD has to be used to load asan for
    # spawned child processes!
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libclang_rt.asan-x86_64.so
            OUTPUT_VARIABLE LIBASAN_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libasan.so
            OUTPUT_VARIABLE LIBASAN_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    message(STATUS "Have to LD_PRELOAD libasan in fork()ed child. Path: " ${LIBASAN_PATH})
endif()

add_subdirectory(common)
add_subdirectory(preload)
add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(quapify build-quapify)
