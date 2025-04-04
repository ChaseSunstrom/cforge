# Tests CMakeLists.txt for lib-a

# Only enable tests if BUILD_TESTING is enabled
if(NOT BUILD_TESTING)
    message(STATUS "Tests disabled - skipping test setup")
    return()
endif()

# Include FetchContent for Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Disable install rules for GoogleTest
set_target_properties(gtest gtest_main gmock gmock_main
    PROPERTIES EXCLUDE_FROM_ALL 1
               EXCLUDE_FROM_DEFAULT_BUILD 1)

# Add test sources
set(TEST_SOURCES
    test_main.cpp
    test_example.cpp
)

# Define test executable name with configuration suffix
string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type_lower)
set(TEST_EXECUTABLE_NAME ${PROJECT_NAME}_${build_type_lower}_tests)

# Add test executable
add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCES})

# Link with GoogleTest and project libraries
target_link_libraries(${TEST_EXECUTABLE_NAME} PRIVATE
    gtest
    gtest_main
    # Add any project libraries here
)

# Include directories
target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(${TEST_EXECUTABLE_NAME})
