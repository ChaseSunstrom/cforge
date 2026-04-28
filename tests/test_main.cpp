// Tests in this directory use TEST() macros that auto-register at startup.
// This file just hands off to the framework's runner.
#include "test_framework.h"

int main(int argc, char** argv) {
    return cf_run_tests(argc, argv);
}
