set(PRELOAD_SRCS
    src/inject_read.c
    src/inject_main.c

    src/runtime.c
    src/timing.c
    )

add_library(quapi_preload SHARED ${PRELOAD_SRCS})

target_link_libraries(quapi_preload quapi_common dl)

target_include_directories(quapi_preload
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(quapi_preload PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

set_property(TARGET quapi_preload PROPERTY POSITION_INDEPENDENT_CODE ON)

generate_export_header(quapi_preload)
target_include_directories(quapi_preload PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
