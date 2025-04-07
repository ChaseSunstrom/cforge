/**
 * @file test_main.cpp
 * @brief Main test runner for proj3
 */

#include <gtest/gtest.h>

/**
 * @brief Main function for running all tests
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
