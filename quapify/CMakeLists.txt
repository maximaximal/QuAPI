set(QUAPIFY_SRCS
    src/quapify.c

    src/file.c
    src/parse.c
    src/common.c
    src/utilities.c
)

add_executable(quapify ${QUAPIFY_SRCS})

set_target_properties(quapify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/)

target_link_libraries(quapify PUBLIC quapi)
