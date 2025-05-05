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
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }
Set-Location $buildDir

# Configure the project
cmake .. -DCMAKE_BUILD_TYPE Release

# Build the project
cmake --build . --config Release -- /m

# Determine install prefix
if (-not $Prefix) { $Prefix = Join-Path $buildDir "install" }

# Install the project
cmake --install . --prefix $Prefix

Write-Host "CForge built and installed to $Prefix"
Write-Host "Add $Prefix\bin to your PATH" 