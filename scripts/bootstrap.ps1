<#
.SYNOPSIS
    Bootstraps building and installing CForge on Windows via PowerShell.
.DESCRIPTION
    Checks for required tools, configures, builds, and installs CForge to a specified prefix.
USAGE
    .\scripts\bootstrap.ps1 [-Prefix <install_dir>]
#>
[CmdletBinding()]
param(
    [string]$Prefix = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Auto-install missing tools
Write-Host "Checking for required tools and installing if missing..."
# Git
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "git not found. Attempting to install..."
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        choco install git --yes
    } elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --id Git.Git -e --source winget --silent
    } else {
        Write-Error "git installation not supported automatically. Please install git manually."
        exit 1
    }
}
# CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "CMake not found. Attempting to install..."
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        choco install cmake --yes
    } elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --id Kitware.CMake -e --source winget --silent
    } else {
        Write-Error "CMake installation not supported automatically. Please install CMake manually."
        exit 1
    }
}
# C++ Compiler
if (-not (Get-Command cl -ErrorAction SilentlyContinue) -and -not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Host "C++ compiler not found. Attempting to install Visual Studio Build Tools or GCC..."
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        choco install visualstudio2019buildtools --yes --package-parameters '--add Microsoft.VisualStudio.Workload.VCTools'
    } elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --id Microsoft.VisualStudio.2019.BuildTools -e --source winget --silent
    } else {
        Write-Error "C++ compiler installation not supported automatically. Please install a compiler manually."
        exit 1
    }
}
# Ninja
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Host "Ninja not found. Attempting to install..."
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        choco install ninja --yes
    } elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --id ninja-build.ninja -e --source winget --silent
    } else {
        Write-Error "Ninja installation not supported automatically. Please install Ninja manually."
        exit 1
    }
}

# Determine script and project root directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Resolve-Path "$ScriptDir\.."
Set-Location $ProjectRoot

# Check for cmake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "Error: cmake not found in PATH"
    exit 1
}

# Check CMake version >= 3.15
$required = [Version]"3.15.0"
$versionText = (cmake --version | Select-String -Pattern 'cmake version (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
$found = [Version]$versionText
if ($found -lt $required) {
    Write-Error "Error: CMake >= $required required (found $found)"
    exit 1
}

# Create and enter build directory
$buildDir = Join-Path $ProjectRoot "build"
# Clear any existing build directory to avoid CMake generator mismatch
if (Test-Path $buildDir) {
    Write-Host "Removing existing build directory: $buildDir"
    Remove-Item -Recurse -Force $buildDir
}
# Recreate build directory
New-Item -ItemType Directory -Path $buildDir | Out-Null
Set-Location $buildDir

# Disable vcpkg toolchain injection
$env:CMAKE_TOOLCHAIN_FILE = ""
Write-Host "Using CMake without vcpkg toolchain file."

# Clone Git dependencies if not already cloned
$depsDir = Join-Path $ProjectRoot "vendor"
if (-not (Test-Path $depsDir)) { New-Item -ItemType Directory -Path $depsDir | Out-Null }
Set-Location $depsDir
if (-not (Test-Path "$depsDir\fmt")) {
    Write-Host "Cloning fmt..."
    git clone https://github.com/fmtlib/fmt.git fmt | Out-Null
    Set-Location "$depsDir\fmt"; git checkout 11.1.4 | Out-Null; Set-Location $depsDir
}
if (-not (Test-Path "$depsDir\tomlplusplus")) {
    Write-Host "Cloning tomlplusplus..."
    git clone https://github.com/marzer/tomlplusplus.git tomlplusplus | Out-Null
    Set-Location "$depsDir\tomlplusplus"; git checkout v3.4.0 | Out-Null; Set-Location $depsDir
}
Set-Location $buildDir

# Configure the project (clear vcpkg integration, force header-only fmt)
& cmake -G "Ninja Multi-Config" -S "$ProjectRoot" -B "$buildDir" `
  -DCMAKE_TOOLCHAIN_FILE="" `
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="" `
  -DCMAKE_BUILD_TYPE=Release `
  -DFMT_HEADER_ONLY=ON `
  -DBUILD_SHARED_LIBS=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }

# Build the project
& cmake --build "$buildDir" --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

# Determine install prefix
if (-not $Prefix) { $Prefix = Join-Path $buildDir "install" }

# Install via build system target
& cmake --build "$buildDir" --config Release --target install
if ($LASTEXITCODE -ne 0) { throw "Project install failed with exit code $LASTEXITCODE" }

Write-Host "CForge built and installed to $Prefix"
Write-Host "Add $Prefix\bin to your PATH"

# Run cforge install with built executable
Set-Location $ProjectRoot
$exePath = Join-Path -Path $buildDir -ChildPath "bin\Release\cforge.exe"
if (-not (Test-Path $exePath)) { throw "cforge executable not found at $exePath" }
& $exePath install 