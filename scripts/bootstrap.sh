#!/usr/bin/env bash
set -euo pipefail

# Auto-install missing tools
echo "Checking for required tools and installing if missing..."
# Git
if ! command -v git >/dev/null 2>&1; then
  echo "git not found. Attempting to install..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y git
  elif command -v yum >/dev/null 2>&1; then
    sudo yum install -y git
  elif command -v brew >/dev/null 2>&1; then
    brew install git
  else
    echo "Please install git manually" >&2
    exit 1
  fi
fi
# CMake
if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Attempting to install..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y cmake
  elif command -v yum >/dev/null 2>&1; then
    sudo yum install -y cmake
  elif command -v brew >/dev/null 2>&1; then
    brew install cmake
  else
    echo "Please install cmake manually" >&2
    exit 1
  fi
fi
# C++ compiler
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
  echo "C++ compiler not found. Attempting to install build-essential or gcc..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y build-essential
  elif command -v yum >/dev/null 2>&1; then
    sudo yum groupinstall -y 'Development Tools'
  elif command -v brew >/dev/null 2>&1; then
    brew install gcc
  else
    echo "Please install a C++ compiler manually" >&2
    exit 1
  fi
fi
# Ninja
if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja not found. Attempting to install..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y ninja-build
  elif command -v yum >/dev/null 2>&1; then
    sudo yum install -y ninja-build
  elif command -v brew >/dev/null 2>&1; then
    brew install ninja
  else
    echo "Please install ninja manually" >&2
    exit 1
  fi
fi

# Determine script and project root directories
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
cd "$dir"

# Clone git dependencies if not already cloned
DEPS_DIR="${dir}/vendor"
mkdir -p "$DEPS_DIR"
cd "$DEPS_DIR"
if [ ! -d "fmt" ]; then
  echo "Cloning fmt..."
  git clone https://github.com/fmtlib/fmt.git fmt
  cd fmt
  git checkout 11.1.4
  cd ..
fi
if [ ! -d "tomlplusplus" ]; then
  echo "Cloning tomlplusplus..."
  git clone https://github.com/marzer/tomlplusplus.git tomlplusplus
  cd tomlplusplus
  git checkout v3.4.0
  cd ..
fi
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

# Build directory name
BUILD_DIR="${BUILD_DIR:-build}"

# Prepare absolute build directory path
BUILD_PATH="${dir}/${BUILD_DIR:-build}"
mkdir -p "$BUILD_PATH"

# Configure the project (clear vcpkg, header-only fmt, static libs)
cmake -U CMAKE_TOOLCHAIN_FILE -U VCPKG_CHAINLOAD_TOOLCHAIN_FILE \
  -S "$dir" -B "$BUILD_PATH" \
  -DCMAKE_TOOLCHAIN_FILE="" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFMT_HEADER_ONLY=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX="$BUILD_PATH/install"
if [ $? -ne 0 ]; then echo "CMake configuration failed"; exit 1; fi

# Build the project
if command -v nproc >/dev/null 2>&1; then
  PROCS=$(nproc)
else
  PROCS=$(sysctl -n hw.ncpu)
fi
cmake --build "$BUILD_PATH" --config Release -- -j"$PROCS"
if [ $? -ne 0 ]; then echo "CMake build failed"; exit 1; fi

# Install the project via build-target
cmake --install "$BUILD_PATH" --config Release --prefix "$BUILD_PATH/install"
if [ $? -ne 0 ]; then echo "Project install failed"; exit 1; fi

echo "CForge built and installed"

# Run cforge install with built executable
cd "$dir"
"$BUILD_PATH/bin/Release/cforge" install --to "$BUILD_PATH/install" 