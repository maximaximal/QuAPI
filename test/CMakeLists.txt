set(TEST_SRCS
    test_main.cpp
    test_signature.cpp
    test_init_and_release.cpp
    test_bash_as_solver.cpp
    test_cadical.cpp
    test_caqe.cpp
    test_abort.cpp
    test_supplied_solver.cpp
    test_stdout_cb.cpp

    util.cpp
)

find_package(Threads)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/)

add_executable(tests ${TEST_SRCS})

target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tests quapi Threads::Threads)

set_property(TARGET tests PROPERTY CXX_STANDARD 17)
