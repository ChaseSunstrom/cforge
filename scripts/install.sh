#!/usr/bin/env bash
#
# cforge installer for Linux/macOS
#
# One-liner installation:
#   curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash
#
# Or with options:
#   curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash -s -- --prefix=/usr/local
#

set -euo pipefail

# Colors for output (Cargo-style)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Default installation paths
# Install to ~/.local/cforge to match `cforge install` and `cforge update --self`
DEFAULT_PREFIX="$HOME/.local/cforge"
PREFIX="${PREFIX:-$DEFAULT_PREFIX}"
ADD_TO_PATH=true
VERBOSE=false
CLEANUP=true

# Cargo-style output: 12-char right-aligned status word
# Format: "{status:>12} {message}"
print_status() {
    local status="$1"
    local message="$2"
    local color="$3"
    printf "${color}${BOLD}%12s${NC} %s\n" "$status" "$message"
}

# Cargo-style helpers
info() { print_status "Checking" "$*" "$CYAN"; }
success() { print_status "Finished" "$*" "$GREEN"; }
warn() { print_status "Warning" "$*" "$YELLOW"; }
error() { print_status "Error" "$*" "$RED" >&2; }
found() { print_status "Found" "$*" "$GREEN"; }
fetching() { print_status "Fetching" "$*" "$GREEN"; }
cloning() { print_status "Cloning" "$*" "$GREEN"; }
building() { print_status "Building" "$*" "$GREEN"; }
installing() { print_status "Installing" "$*" "$GREEN"; }
configuring() { print_status "Configuring" "$*" "$GREEN"; }
adding() { print_status "Adding" "$*" "$GREEN"; }

# Print usage
usage() {
    cat << EOF
cforge installer

Usage: $0 [OPTIONS]

Options:
    -p, --prefix PATH    Installation prefix (default: $DEFAULT_PREFIX)
    -n, --no-path        Don't add cforge to PATH
    -v, --verbose        Verbose output
    --no-cleanup         Keep build directory after installation
    -h, --help           Show this help message

Examples:
    $0                           # Install to ~/.local/cforge
    $0 --prefix /opt/cforge      # Install to /opt/cforge (may need sudo)
    $0 --no-path                 # Install without modifying PATH

One-liner installation:
    curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash

EOF
    exit 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--prefix)
            PREFIX="$2"
            shift 2
            ;;
        -n|--no-path)
            ADD_TO_PATH=false
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --no-cleanup)
            CLEANUP=false
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            error "Unknown option: $1"
            usage
            ;;
    esac
done

# Detect OS
detect_os() {
    case "$(uname -s)" in
        Linux*)     OS=linux;;
        Darwin*)    OS=macos;;
        CYGWIN*|MINGW*|MSYS*) OS=windows;;
        *)          OS=unknown;;
    esac
    echo "$OS"
}

# Detect package manager
detect_package_manager() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "apt"
    elif command -v dnf >/dev/null 2>&1; then
        echo "dnf"
    elif command -v yum >/dev/null 2>&1; then
        echo "yum"
    elif command -v pacman >/dev/null 2>&1; then
        echo "pacman"
    elif command -v brew >/dev/null 2>&1; then
        echo "brew"
    elif command -v apk >/dev/null 2>&1; then
        echo "apk"
    else
        echo "unknown"
    fi
}

# Install package using detected package manager
install_package() {
    local pkg="$1"
    local pm
    pm=$(detect_package_manager)

    case "$pm" in
        apt)
            sudo apt-get update && sudo apt-get install -y "$pkg"
            ;;
        dnf)
            sudo dnf install -y "$pkg"
            ;;
        yum)
            sudo yum install -y "$pkg"
            ;;
        pacman)
            sudo pacman -S --noconfirm "$pkg"
            ;;
        brew)
            brew install "$pkg"
            ;;
        apk)
            sudo apk add "$pkg"
            ;;
        *)
            error "Unknown package manager. Please install $pkg manually."
            return 1
            ;;
    esac
}

# Check and install dependencies
check_dependencies() {
    print_status "Checking" "dependencies..." "$CYAN"

    # Git
    if ! command -v git >/dev/null 2>&1; then
        warn "git not found, installing..."
        install_package git || { error "Failed to install git"; exit 1; }
    fi
    found "git"

    # CMake
    if ! command -v cmake >/dev/null 2>&1; then
        warn "cmake not found, installing..."
        install_package cmake || { error "Failed to install cmake"; exit 1; }
    fi

    # Check CMake version
    local cmake_version
    cmake_version=$(cmake --version | head -n1 | awk '{print $3}')
    local required="3.15.0"
    if [ "$(printf '%s\n' "$required" "$cmake_version" | sort -V | head -n1)" != "$required" ]; then
        error "CMake >= $required required (found $cmake_version)"
        exit 1
    fi
    found "cmake $cmake_version"

    # C++ compiler
    if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
        warn "C++ compiler not found, installing..."
        local pm
        pm=$(detect_package_manager)
        case "$pm" in
            apt) install_package build-essential ;;
            dnf|yum) install_package gcc-c++ ;;
            pacman) install_package base-devel ;;
            brew) install_package gcc ;;
            apk) install_package build-base ;;
            *) error "Please install a C++ compiler manually"; exit 1 ;;
        esac
    fi
    if command -v g++ >/dev/null 2>&1; then
        found "g++"
    elif command -v clang++ >/dev/null 2>&1; then
        found "clang++"
    fi

    # Ninja (optional but recommended)
    if ! command -v ninja >/dev/null 2>&1; then
        warn "ninja not found, installing for faster builds..."
        local pm
        pm=$(detect_package_manager)
        local ninja_pkg="ninja-build"
        [[ "$pm" == "brew" ]] && ninja_pkg="ninja"
        [[ "$pm" == "pacman" ]] && ninja_pkg="ninja"
        [[ "$pm" == "apk" ]] && ninja_pkg="ninja"
        install_package "$ninja_pkg" 2>/dev/null || warn "Could not install ninja, will use make instead"
    fi
    if command -v ninja >/dev/null 2>&1; then
        found "ninja"
    fi
}

# Create temporary directory
create_temp_dir() {
    TEMP_DIR=$(mktemp -d)
    trap 'cleanup' EXIT
    print_status "Preparing" "build environment" "$CYAN"
}

# Cleanup function
cleanup() {
    if [[ "$CLEANUP" == true && -n "${TEMP_DIR:-}" && -d "$TEMP_DIR" ]]; then
        rm -rf "$TEMP_DIR"
    fi
}

# Clone and build cforge
build_cforge() {
    cloning "cforge repository"
    cd "$TEMP_DIR"

    if [[ "$VERBOSE" == true ]]; then
        git clone https://github.com/ChaseSunstrom/cforge.git
    else
        git clone --quiet https://github.com/ChaseSunstrom/cforge.git
    fi

    cd cforge

    fetching "dependencies"
    mkdir -p vendor
    cd vendor

    if [[ "$VERBOSE" == true ]]; then
        git clone https://github.com/fmtlib/fmt.git
        git clone https://github.com/marzer/tomlplusplus.git
    else
        git clone --quiet https://github.com/fmtlib/fmt.git
        git clone --quiet https://github.com/marzer/tomlplusplus.git
    fi

    cd fmt && git checkout 11.1.4 --quiet && cd ..
    cd tomlplusplus && git checkout v3.4.0 --quiet && cd ..
    cd ..

    configuring "CMake build"
    mkdir -p build

    local cmake_args=(
        -S . -B build
        -DCMAKE_BUILD_TYPE=Release
        -DFMT_HEADER_ONLY=ON
        -DBUILD_SHARED_LIBS=OFF
        "-DCMAKE_INSTALL_PREFIX=$PREFIX"
    )

    if command -v ninja >/dev/null 2>&1; then
        cmake_args+=(-G Ninja)
    fi

    if [[ "$VERBOSE" == true ]]; then
        cmake "${cmake_args[@]}"
    else
        cmake "${cmake_args[@]}" >/dev/null
    fi

    building "cforge"
    local jobs
    if command -v nproc >/dev/null 2>&1; then
        jobs=$(nproc)
    else
        jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    fi

    if [[ "$VERBOSE" == true ]]; then
        cmake --build build --config Release -j"$jobs"
    else
        cmake --build build --config Release -j"$jobs" >/dev/null
    fi

    success "build completed"
}

# Install cforge
install_cforge() {
    # Install to same location as `cforge install` and `cforge update --self`
    local install_dir="$PREFIX/installed/cforge/bin"
    installing "to $install_dir"

    mkdir -p "$install_dir"

    # Find the built binary
    local binary
    if [[ -f "$TEMP_DIR/cforge/build/bin/Release/cforge" ]]; then
        binary="$TEMP_DIR/cforge/build/bin/Release/cforge"
    elif [[ -f "$TEMP_DIR/cforge/build/bin/cforge" ]]; then
        binary="$TEMP_DIR/cforge/build/bin/cforge"
    elif [[ -f "$TEMP_DIR/cforge/build/cforge" ]]; then
        binary="$TEMP_DIR/cforge/build/cforge"
    else
        error "Could not find built cforge binary"
        find "$TEMP_DIR/cforge/build" -name "cforge*" -type f
        exit 1
    fi

    cp "$binary" "$install_dir/cforge"
    chmod +x "$install_dir/cforge"

    # Also create symlink in ~/.local/bin for convenience
    mkdir -p "$HOME/.local/bin"
    ln -sf "$install_dir/cforge" "$HOME/.local/bin/cforge"

    success "installed cforge to $install_dir/cforge"
    success "created symlink at $HOME/.local/bin/cforge"
}

# Add to PATH
add_to_path() {
    if [[ "$ADD_TO_PATH" != true ]]; then
        return
    fi

    # Add ~/.local/bin to PATH (where the symlink is)
    local bin_dir="$HOME/.local/bin"

    # Check if already in PATH
    if [[ ":$PATH:" == *":$bin_dir:"* ]]; then
        print_status "Skipping" "$bin_dir is already in PATH" "$CYAN"
        return
    fi

    adding "$bin_dir to PATH"

    # Detect shell config file
    local shell_config=""
    local shell_name
    shell_name=$(basename "$SHELL")

    case "$shell_name" in
        bash)
            if [[ -f "$HOME/.bashrc" ]]; then
                shell_config="$HOME/.bashrc"
            elif [[ -f "$HOME/.bash_profile" ]]; then
                shell_config="$HOME/.bash_profile"
            fi
            ;;
        zsh)
            shell_config="$HOME/.zshrc"
            ;;
        fish)
            shell_config="$HOME/.config/fish/config.fish"
            ;;
    esac

    if [[ -n "$shell_config" ]]; then
        local export_line="export PATH=\"$bin_dir:\$PATH\""
        [[ "$shell_name" == "fish" ]] && export_line="set -gx PATH \"$bin_dir\" \$PATH"

        if ! grep -q "$bin_dir" "$shell_config" 2>/dev/null; then
            echo "" >> "$shell_config"
            echo "# Added by cforge installer" >> "$shell_config"
            echo "$export_line" >> "$shell_config"
            success "added to $shell_config"
            warn "Run 'source $shell_config' or restart your terminal"
        else
            print_status "Skipping" "PATH entry already exists in $shell_config" "$CYAN"
        fi
    else
        warn "Could not detect shell config file"
        warn "Add the following to your shell config:"
        echo "  export PATH=\"$bin_dir:\$PATH\""
    fi
}

# Verify installation
verify_installation() {
    print_status "Verifying" "installation" "$CYAN"

    local install_dir="$PREFIX/installed/cforge/bin"
    if [[ -x "$install_dir/cforge" ]]; then
        local version
        version=$("$install_dir/cforge" version 2>/dev/null || echo "unknown")
        success "cforge installed successfully!"
        echo ""
        echo "  Version:  $version"
        echo "  Location: $install_dir/cforge"
        echo "  Symlink:  $HOME/.local/bin/cforge"
        echo ""
        echo "Get started:"
        echo "  cforge init my_project    # Create a new project"
        echo "  cd my_project"
        echo "  cforge build              # Build the project"
        echo "  cforge run                # Run the project"
        echo ""
    else
        error "Installation verification failed"
        exit 1
    fi
}

# Main installation flow
main() {
    echo ""
    echo -e "${GREEN}${BOLD}cforge${NC} - C/C++ Build System Installer"
    echo ""

    local os
    os=$(detect_os)
    print_status "Detected" "OS: $os" "$CYAN"
    print_status "Prefix" "$PREFIX" "$CYAN"
    echo ""

    check_dependencies
    echo ""

    create_temp_dir
    build_cforge
    echo ""

    install_cforge
    add_to_path
    echo ""

    verify_installation
}

main
