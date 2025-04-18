/**
 * @file main.cpp
 * @brief Main entry point for hi2
 */

#include <iostream>
#include <spdlog/spdlog.h>

/**
 * @brief Main function
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return int Exit code
 */
int main(int argc, char* argv[]) {
    // Silence unused parameter warnings
    (void)argc;
    (void)argv;
    
    spdlog::info("Hello from hi2!");
    return 0;
}