cmake_minimum_required(VERSION 3.15)
project(cforge_tests C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
file(GLOB_RECURSE TEST_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "test_main.cpp",
    "test_framework.h"
    "test_test.cpp"
)
add_executable(${PROJECT_NAME} ${TEST_SRCS})
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
include(FetchContent)
set(FETCHCONTENT_GIT_PROTOCOL "https")
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fmt
)
FetchContent_MakeAvailable(fmt)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tomlplusplus
)
FetchContent_MakeAvailable(tomlplusplus)
