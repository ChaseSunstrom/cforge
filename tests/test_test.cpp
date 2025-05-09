#include "test_framework.h"

int add(int a, int b) {
    return a + b;
}

int subtract(int a, int b) {
    return a - b;
}

int divide(int a, int b) {
    return a / b;
}

TEST(Add) {
    cf_assert(add(2, 2) == 4);
}

TEST(Subtract) {
    cf_assert(subtract(2, 2) == 0);
}

TEST(Divide) {
    cf_assert(divide(2, 2) == 2);
}
