set(COMMON_SRCS
    src/common.c
    )

add_library(quapi_common ${COMMON_SRCS})

target_include_directories(quapi_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_property(TARGET quapi_common PROPERTY POSITION_INDEPENDENT_CODE ON)
