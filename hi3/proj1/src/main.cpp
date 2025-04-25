/**
 * @file main.cpp
 * @brief Main entry point for proj1
 */

#include <iostream>

#include "spdlog/spdlog.h"

/**
 * @brief Main function
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return int Exit code
 */
int main(int argc, char* argv[]) {
    std::cout << "Hello from proj1!" << std::endl;
    spdlog::log(spdlog::level::info, "Hello from proj1!");
    return 0;
}
