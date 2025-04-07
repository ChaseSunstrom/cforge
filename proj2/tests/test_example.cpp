/**
 * @file test_example.cpp
 * @brief Example tests for proj2
 */

#include <gtest/gtest.h>
#include "proj2/example.hpp"

/**
 * @brief Example test case
 */
TEST(ExampleTest, BasicTest) {
    // Call the example function from the library
    const char* message = proj2::get_example_message();
    
    // Verify the result
    ASSERT_NE(message, nullptr);
    EXPECT_TRUE(strlen(message) > 0);
}

/**
 * @brief Another example test case
 */
TEST(ExampleTest, TrivialTest) {
    // Basic assertions
    EXPECT_EQ(1, 1);
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
}
