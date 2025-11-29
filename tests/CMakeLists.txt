cmake_minimum_required(VERSION 3.15)
project(cforge_tests C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Test source files
set(TEST_SRCS
    test_main.cpp
    test_test.cpp
    test_version.cpp
    test_lockfile.cpp
)

add_executable(${PROJECT_NAME} ${TEST_SRCS})

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# Dependencies
include(FetchContent)
set(FETCHCONTENT_GIT_PROTOCOL "https")

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.1.4
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fmt
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tomlplusplus
)
FetchContent_MakeAvailable(tomlplusplus)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt tomlplusplus::tomlplusplus)
