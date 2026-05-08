cmake_minimum_required(VERSION 3.16)
project(dpdk_pcap_demo C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find DPDK via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Collect source files
file(GLOB SOURCES src/*.c)

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${DPDK_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${DPDK_CFLAGS_OTHER})
target_link_libraries(${PROJECT_NAME} PRIVATE ${DPDK_LIBRARIES})
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output
)
message(STATUS "runtime output directory: ${CMAKE_SOURCE_DIR}/output")

# Enable common warnings for learning
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
endif()

# Optional: AddressSanitizer (default OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()

# Optional: Multi-lcore ring pipeline (default OFF)
option(ENABLE_PIPELINE "Enable multi-lcore ring pipeline" OFF)
if(ENABLE_PIPELINE)
    message(STATUS "Pipeline mode enabled (multi-lcore)")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_PIPELINE)
endif()

# Optional: Multi-lcore direct polling (default OFF)
option(ENABLE_MULTICORE "Enable multi-lcore direct polling" OFF)
if(ENABLE_MULTICORE)
    message(STATUS "Multi-lcore mode enabled")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_MULTICORE)
endif()

# Optional: L2 forwarding (default OFF)
option(ENABLE_L2FWD "Enable L2 forwarding mode" OFF)
if(ENABLE_L2FWD)
    message(STATUS "L2 forwarding mode enabled")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_L2FWD)
endif()

# Optional: PCAP dump of received packets (default OFF)
option(ENABLE_PCAP_DUMP "Enable PCAP dump of RX packets" OFF)
if(ENABLE_PCAP_DUMP)
    message(STATUS "PCAP dump enabled")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_PCAP_DUMP)
endif()

# Tests
option(BUILD_TESTS "Build unit tests with gtest" ON)
if(BUILD_TESTS)
    add_subdirectory(test)
endif()
