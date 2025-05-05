#!/usr/bin/env bash
set -euo pipefail

# Determine script and project root directories
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
cd "$dir"

# Check for cmake
command -v cmake >/dev/null 2>&1 || { echo "Error: cmake not found in PATH" >&2; exit 1; }
# Check CMake version >= 3.15
required="3.15.0"
cmake_version=$(cmake --version | head -n1 | awk '{print $3}')
if [ "$(printf '%s\n' "$required" "$cmake_version" | sort -V | head -n1)" != "$required" ]; then
  echo "Error: CMake >= $required required (found $cmake_version)" >&2
  exit 1
fi

# Check for C++ compiler
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1 && ! command -v cl >/dev/null 2>&1; then
  echo "Error: No C++ compiler found (gcc, clang, or MSVC)" >&2
  exit 1
fi

# Create and enter build directory
BUILD_DIR="${BUILD_DIR:-build}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure the project
cmake .. -DCMAKE_BUILD_TYPE Release

# Build the project
cmake --build . --config Release -- -j"$(nproc)"

# Install to a local prefix (default: ./install)
PREFIX="${PREFIX:-"$(pwd)/install"}"
cmake --install . --prefix "$PREFIX"

echo "CForge built and installed to $PREFIX"
echo "Add $PREFIX/bin to your PATH" 