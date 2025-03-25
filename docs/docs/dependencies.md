---
id: dependencies
title: Working with Dependencies
---

## 📦 Working with Dependencies

CForge supports multiple dependency management systems:

### vcpkg Integration

```toml
[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"  # Optional, defaults to ~/.vcpkg
packages = ["fmt", "boost", "nlohmann-json"] 
```

Example C++ code using vcpkg dependencies:

```cpp
#include <fmt/core.h>
#include <nlohmann/json.hpp>

int main() {
    // Using fmt library from vcpkg
    fmt::print("Hello, {}!\n", "world");
    
    // Using nlohmann/json library from vcpkg
    nlohmann::json j = {
        {"name", "CForge"},
        {"version", "1.2.0"}
    };
    
    fmt::print("JSON: {}\n", j.dump(2));
    return 0;
} 
```

### Conan Integration

```toml 
[dependencies.conan]
enabled = true
packages = ["fmt/9.1.0", "spdlog/1.10.0"]
options = { "fmt:shared": "False", "spdlog:shared": "False" }
generators = ["cmake", "cmake_find_package"] 
```

### Git Dependencies

```toml 
[[dependencies.git]]
name = "nlohmann_json"
url = "https://github.com/nlohmann/json.git"
tag = "v3.11.3"
# Optional settings
shallow = true  # Faster clone with reduced history
update = false  # Whether to update the repo on builds

[[dependencies.git]]
name = "fmt"
url = "https://github.com/fmtlib/fmt.git"
tag = "9.1.0"
cmake_options = ["-DFMT_TEST=OFF", "-DFMT_DOC=OFF"]  # Pass CMake options when building

[[dependencies.git]]
name = "imgui"
url = "https://github.com/ocornut/imgui.git"
branch = "master"  # Use a specific branch instead of tag
shallow = true

[[dependencies.git]]
name = "custom_repo"
url = "https://example.com/repo.git"
commit = "abc123def456"  # Use a specific commit hash 
```

Git dependencies are automatically cloned into a deps directory. The libraries can be included in your project by adding their include paths to your target configuration:

``` toml
[targets.default]
include_dirs = ["include", "deps/nlohmann_json/single_include", "deps/fmt/include"]
defines = ["FMT_HEADER_ONLY"]  # Optionally add defines for your dependencies 
```

You can also use the libraries in your code immediately:

```cpp
#include <nlohmann/json.hpp>
#include <fmt/core.h>

int main() {
    // Using nlohmann/json
    nlohmann::json obj = {{"name", "cforge"}, {"version", "1.4.0"}};
    
    // Using fmt
    fmt::print("Project: {}\n", obj["name"].get<std::string>());
    return 0;
} 
```

### Custom Dependencies

```toml
[[dependencies.custom]]
name = "my_library"
url = "https://example.com/my_library-1.0.0.zip"
include_path = "include"
library_path = "lib" 
```

### System Dependencies

```toml
[dependencies]
system = ["X11", "pthread", "dl"] 
```