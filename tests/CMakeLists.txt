# Tests for cpp-forge
message(STATUS "Configuring tests for cpp-forge")

# Add test executable
add_executable(cpp-forge-tests
    test_main.cpp
    # Add more test files here
)

# Currently minimal test files, so just create a simple target
add_custom_target(run-tests DEPENDS cpp-forge-tests)

# Configure test discovery if needed
# enable_testing()
# add_test(NAME cpp-forge-tests COMMAND cpp-forge-tests) 