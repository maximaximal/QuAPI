set(LIB_SRCS
    src/quapi.c
)

add_library(quapi STATIC ${LIB_SRCS})

set_target_properties(quapi PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

target_include_directories(quapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(quapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

target_link_libraries(quapi PUBLIC quapi_common)

target_link_libraries(quapi PRIVATE PCRE2::pcre2)

if(NOT ${LIBASAN_PATH} STREQUAL "")
    target_compile_definitions(quapi PRIVATE LIBASAN_PATH="${LIBASAN_PATH}")
endif()

generate_export_header(quapi)
target_include_directories(quapi PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set_property(TARGET quapi PROPERTY POSITION_INDEPENDENT_CODE ON)
