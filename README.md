CBuild
CBuild is a TOML-based build system for C and C++ projects that integrates with CMake and vcpkg. It simplifies project initialization, configuration, building, testing, installation, and more through a consistent command-line interface.

Features
TOML Configuration: Define your project settings in a human-readable cbuild.toml file.
Multi-Command CLI: Easily initialize, build, clean, run, test, install, and package your project.
Dependency Management: Integrates with vcpkg, Conan, Git, and custom dependency sources.
Build Variants and Configurations: Supports multiple build configurations (e.g., Debug, Release) and custom variants.
IDE Integration: Generate project files for VS Code, CLion, Xcode, or Visual Studio.
Cross-Compilation: Predefined support for targets such as Android, iOS, Raspberry Pi, and WebAssembly.
Build Hooks: Run custom scripts before/after configuration, building, cleaning, running, etc.
Installation
Prerequisites
Before installing CBuild, ensure you have the following installed:

Rust: Install Rust
CMake: Version 3.15 or later
vcpkg: (if using vcpkg integration)
Conan: (if using Conan dependency management)
A C/C++ compiler (e.g., GCC, Clang, MSVC)
Building CBuild
Clone the repository and build the tool with Cargo:

bash
Copy
git clone https://github.com/yourusername/cbuild.git
cd cbuild
cargo build --release
After building, you can add the release binary (found in target/release/cbuild) to your system's PATH for easier access.

Getting Started
1. Initialize a New Project
Create a new project by running:

bash
Copy
cbuild init
Use --workspace to initialize a workspace.
Optionally, use --template with values like app, lib, or header-only to create different project types.
2. Configure Your Project
Edit the generated cbuild.toml file to adjust:

Project Info: Name, version, description, type (executable, library, header-only), language, and standard.
Build Settings: Build directory, default configuration (e.g., Debug, Release), and CMake options.
Dependencies: Configure vcpkg path, packages, Conan settings, Git dependencies, or custom dependencies.
Targets: Specify source file patterns, include directories, and libraries to link against.
Platform and Variant Settings: Define platform-specific and build variant options.
3. Build the Project
Run the following command to build your project:

bash
Copy
cbuild build
You can specify a configuration (Debug/Release) or variant:

bash
Copy
cbuild build --config Release
cbuild build --variant performance
4. Clean Build Artifacts
Remove build files and reset your build environment:

bash
Copy
cbuild clean
5. Run the Built Executable
Execute your program with:

bash
Copy
cbuild run
Pass additional arguments to the executable if needed:

bash
Copy
cbuild run -- arg1 arg2
6. Testing
If your project includes tests, run them using:

bash
Copy
cbuild test
You can filter tests using the --filter option:

bash
Copy
cbuild test --filter MyTestSuite
7. Installation and Packaging
Install your project to a specified prefix:

bash
Copy
cbuild install --prefix /usr/local
Create a package (e.g., ZIP, deb, rpm):

bash
Copy
cbuild package --type zip
8. Dependency Management
Automatically install and update dependencies with:

bash
Copy
cbuild deps
Add the --update flag to update existing dependencies:

bash
Copy
cbuild deps --update
9. Running Custom Scripts
Define custom scripts in the scripts section of your configuration and run them with:

bash
Copy
cbuild script <script_name>
10. IDE Integration
Generate project files for your favorite IDE:

VS Code:

bash
Copy
cbuild ide vscode
CLion:

bash
Copy
cbuild ide clion
Xcode (macOS only):

bash
Copy
cbuild ide xcode
Visual Studio (Windows only):

bash
Copy
cbuild ide vs
11. Listing Available Options
List configurations, variants, targets, or scripts:

bash
Copy
cbuild list configs
cbuild list variants
cbuild list targets
cbuild list scripts
Examples
Example 1: Standard Executable Project
Initialize:

bash
Copy
cbuild init --template app
Edit cbuild.toml: Adjust project name, version, and add any additional compiler flags or dependencies.

Build and Run:

bash
Copy
cbuild build --config Debug
cbuild run
Example 2: Library Project with vcpkg Dependency
Initialize:

bash
Copy
cbuild init --template lib
Configure Dependencies:
In cbuild.toml, enable vcpkg and add a package (e.g., "fmt").

Build:

bash
Copy
cbuild build --config Release
Example 3: Cross-Compilation for Android ARM64
Configure Cross-Compile:
In your cbuild.toml, set up cross-compilation settings or use the predefined target.

Build with Cross-Target:

bash
Copy
cbuild build --target android-arm64
Contributing
Contributions are welcome! Feel free to open issues or submit pull requests. When contributing, please follow the existing coding style and include tests for new features.

License
Distributed under the MIT License. See LICENSE for more information.

Acknowledgments
Inspired by various build systems and package managers in the C/C++ ecosystem.
Thanks to the open-source community for the tools and libraries that make CBuild possible.
