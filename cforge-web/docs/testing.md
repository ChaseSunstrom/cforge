---
id: testing
title: Testing
---

## 🧪 Testing

CForge integrates with CTest for testing:

```toml
[tests]
directory = "tests"
enabled = true
timeout = 30  # seconds

[[tests.executables]]
name = "math_tests"
sources = ["tests/math_test.cpp"]
includes = ["include", "tests/common"]
links = ["my_project"]
labels = ["unit", "math"] 
```

Example test file (`tests/math_test.cpp`):
```cpp 
#include <iostream>
#include <cassert>
#include "my_project.h"

void test_addition() {
    assert(my_project::add(2, 3) == 5);
    std::cout << "Addition test passed!" << std::endl;
}

void test_multiplication() {
    assert(my_project::multiply(2, 3) == 6);
    std::cout << "Multiplication test passed!" << std::endl;
}

int main() {
    test_addition();
    test_multiplication();
    std::cout << "All tests passed!" << std::endl;
    return 0;
} 
```

Running tests:
```bash 
# Run all tests
cforge test

# Run tests with a specific label
cforge test --label unit

# Run tests matching a pattern
cforge test --filter math

# Initialize test directory with sample test
cforge test --init

# Discover tests and update config
cforge test --discover

# Generate test reports
cforge test --report xml 
```