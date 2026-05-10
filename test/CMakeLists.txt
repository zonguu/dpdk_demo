enable_testing()

# Find GTest
find_package(GTest REQUIRED)

# Collect source files for the library (exclude main.c)
set(SRC_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/packet_parser.c
    ${CMAKE_SOURCE_DIR}/src/stats.c
    ${CMAKE_SOURCE_DIR}/src/dpdk_init.c
    ${CMAKE_SOURCE_DIR}/src/packet_worker.c
    ${CMAKE_SOURCE_DIR}/src/pipeline.c
    ${CMAKE_SOURCE_DIR}/src/packet_modify.c
    ${CMAKE_SOURCE_DIR}/src/acl_filter.c
    ${CMAKE_SOURCE_DIR}/src/token_bucket.c
)

add_library(dpdk_demo_lib STATIC ${SRC_LIB_SOURCES})
target_include_directories(dpdk_demo_lib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(dpdk_demo_lib PRIVATE ${DPDK_INCLUDE_DIRS})
target_compile_options(dpdk_demo_lib PRIVATE ${DPDK_CFLAGS_OTHER})

# Test executable
add_executable(dpdk_demo_test
    test_main.cpp
    test_packet_parser.cpp
    test_stats.cpp
    test_packet_modify.cpp
    test_acl_filter.cpp
    test_token_bucket.cpp
)

set_target_properties(dpdk_demo_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output
)

target_include_directories(dpdk_demo_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(dpdk_demo_test PRIVATE ${DPDK_INCLUDE_DIRS})
target_compile_options(dpdk_demo_test PRIVATE ${DPDK_CFLAGS_OTHER})
target_link_libraries(dpdk_demo_test PRIVATE
    dpdk_demo_lib
    GTest::gtest
    GTest::gtest_main
    ${DPDK_LIBRARIES}
)

# Add test target
add_test(NAME dpdk_demo_test COMMAND dpdk_demo_test)
