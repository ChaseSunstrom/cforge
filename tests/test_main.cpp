
#include "test_framework.h"
#include <stdio.h>

test_case_t __c_tests[MAX_TESTS];
size_t __c_test_count = 0;

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running tests...\n");
    return test_main();
}
