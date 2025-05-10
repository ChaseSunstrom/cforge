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