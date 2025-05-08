#include "test_framework.h"

int add(int a, int b) {
    return a + b;
}

int subtract(int a, int b) {
    return a - b;
}

TEST(Math, Add) {
    cf_assert(add(2, 2) == 4);
}

TEST(Math, Subtract) {
    cf_assert(subtract(2, 2) == 0);
}


