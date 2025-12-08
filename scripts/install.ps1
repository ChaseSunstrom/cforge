#Requires -Version 5.1
<#
.SYNOPSIS
    cforge installer for Windows

.DESCRIPTION
    Downloads, builds, and installs cforge - a modern C/C++ build system.

.PARAMETER Prefix
    Installation directory (default: $env:LOCALAPPDATA\cforge)

.PARAMETER NoPath
    Don't add cforge to PATH

.PARAMETER Verbose
    Show verbose output

.EXAMPLE
    # One-liner installation (run in PowerShell):
    irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex

    # Or with options:
    .\install.ps1 -Prefix "C:\Tools\cforge"

.NOTES
    Author: Chase Sunstrom
    License: MIT
#>

param(
    [string]$Prefix,
    [switch]$NoPath,
    [switch]$VerboseOutput,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# Detect existing installation or use default
if (-not $Prefix) {
    # Check common installation locations (using the installed/cforge/bin structure)
    $existingPaths = @(
        "$env:ProgramFiles\cforge",
        "$env:LOCALAPPDATA\cforge",
        "C:\cforge"
    )

    foreach ($path in $existingPaths) {
        if (Test-Path "$path\installed\cforge\bin\cforge.exe") {
            $Prefix = $path
            break
        }
    }

    # Default to ProgramFiles if no existing installation found
    if (-not $Prefix) {
        $Prefix = "$env:ProgramFiles\cforge"
    }
}

# Cargo-style output: 12-char right-aligned status word
# Format: "{status:>12} {message}"
function Write-Status {
    param(
        [string]$Status,
        [string]$Message,
        [ConsoleColor]$Color = "Green"
    )
    Write-Host ("{0,12}" -f $Status) -ForegroundColor $Color -NoNewline
    Write-Host " $Message"
}

# Cargo-style helpers
function Write-Info { Write-Status -Status "Checking" -Message "$args" -Color Cyan }
function Write-Ok { Write-Status -Status "Finished" -Message "$args" -Color Green }
function Write-Warn { Write-Status -Status "Warning" -Message "$args" -Color Yellow }
function Write-Err { Write-Status -Status "Error" -Message "$args" -Color Red }
function Write-Found { Write-Status -Status "Found" -Message "$args" -Color Green }
function Write-Fetching { Write-Status -Status "Fetching" -Message "$args" -Color Green }
function Write-Cloning { Write-Status -Status "Cloning" -Message "$args" -Color Green }
function Write-Building { Write-Status -Status "Building" -Message "$args" -Color Green }
function Write-Installing { Write-Status -Status "Installing" -Message "$args" -Color Green }
function Write-Configuring { Write-Status -Status "Configuring" -Message "$args" -Color Green }
function Write-Adding { Write-Status -Status "Adding" -Message "$args" -Color Green }
function Write-Setting { Write-Status -Status "Setting" -Message "$args" -Color Cyan }

function Show-Help {
    @"
cforge Installer for Windows

Usage: .\install.ps1 [OPTIONS]

Options:
    -Prefix PATH     Installation directory (default: $env:LOCALAPPDATA\cforge)
    -NoPath          Don't add cforge to PATH
    -VerboseOutput   Show verbose output
    -Help            Show this help message

Examples:
    .\install.ps1                              # Install to default location
    .\install.ps1 -Prefix "C:\Tools\cforge"    # Install to custom location
    .\install.ps1 -NoPath                      # Install without modifying PATH

One-liner installation (run in PowerShell as Administrator):
    irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex

"@
    exit 0
}

if ($Help) { Show-Help }

# Check for required tools and install if possible
function Install-Tool {
    param([string]$Name, [string]$WingetId, [string]$ChocoName)

    if (Get-Command $Name -ErrorAction SilentlyContinue) {
        return $true
    }

    Write-Warn "$Name not found, attempting to install..."

    # Try winget first
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        try {
            winget install --id $WingetId --silent --accept-package-agreements --accept-source-agreements
            return $true
        } catch {
            Write-Warn "winget install failed, trying chocolatey..."
        }
    }

    # Try chocolatey
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        try {
            choco install $ChocoName -y
            return $true
        } catch {
            Write-Warn "choco install failed"
        }
    }

    return $false
}

function Test-Dependencies {
    Write-Status -Status "Checking" -Message "dependencies..." -Color Cyan

    # Git
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        if (-not (Install-Tool "git" "Git.Git" "git")) {
            Write-Err "git is required. Please install it from https://git-scm.com/"
            exit 1
        }
        # Refresh PATH
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    }
    Write-Found "git"

    # CMake
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        if (-not (Install-Tool "cmake" "Kitware.CMake" "cmake")) {
            Write-Err "cmake is required. Please install it from https://cmake.org/"
            exit 1
        }
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    }

    # Check CMake version
    $cmakeVersionRaw = (cmake --version | Select-Object -First 1) -replace "cmake version ", ""
    # Extract just the version number (X.Y.Z) and strip suffixes like -rc1, -alpha, etc.
    if ($cmakeVersionRaw -match "^(\d+\.\d+\.?\d*)") {
        $cmakeVersion = $matches[1]
    } else {
        $cmakeVersion = $cmakeVersionRaw -replace "-.*$", ""
    }
    try {
        if ([version]$cmakeVersion -lt [version]"3.15.0") {
            Write-Err "CMake >= 3.15.0 required (found $cmakeVersionRaw)"
            exit 1
        }
    } catch {
        Write-Warn "Could not parse CMake version '$cmakeVersionRaw', continuing anyway..."
    }
    Write-Found "cmake $cmakeVersionRaw"

    # Visual Studio / Build Tools
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $hasVS = $false
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $hasVS = $true
            Write-Found "Visual Studio at $vsPath"
        }
    }

    if (-not $hasVS) {
        Write-Warn "Visual Studio Build Tools not found"
        Write-Installing "Visual Studio Build Tools..."

        if (Get-Command winget -ErrorAction SilentlyContinue) {
            try {
                winget install --id Microsoft.VisualStudio.2022.BuildTools --silent --accept-package-agreements --accept-source-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
                Write-Ok "Visual Studio Build Tools installed"
            } catch {
                Write-Err "Failed to install Build Tools. Please install manually from https://visualstudio.microsoft.com/downloads/"
                exit 1
            }
        } else {
            Write-Err "Please install Visual Studio Build Tools from https://visualstudio.microsoft.com/downloads/"
            exit 1
        }
    }

    # Ninja (optional)
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        Write-Warn "ninja not found, attempting to install for faster builds..."
        Install-Tool "ninja" "Ninja-build.Ninja" "ninja" | Out-Null
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    }
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        Write-Found "ninja"
    }
}

function Get-VsDevEnv {
    # Find Visual Studio installation
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) {
        Write-Err "vswhere not found"
        return $false
    }

    $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        Write-Err "Visual Studio with C++ tools not found"
        return $false
    }

    # Find vcvarsall.bat
    $vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvarsall)) {
        Write-Err "vcvarsall.bat not found"
        return $false
    }

    Write-Setting "up Visual Studio environment"

    # Run vcvarsall and capture environment
    $arch = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
    $pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $pinfo.FileName = "cmd.exe"
    $pinfo.Arguments = "/c `"$vcvarsall`" $arch && set"
    $pinfo.RedirectStandardOutput = $true
    $pinfo.UseShellExecute = $false
    $pinfo.CreateNoWindow = $true

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $pinfo
    $p.Start() | Out-Null
    $output = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()

    # Parse and set environment variables
    $output -split "`r`n" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    return $true
}

function Build-Cforge {
    param([string]$TempDir)

    Write-Cloning "cforge repository"
    Push-Location $TempDir

    try {
        if ($VerboseOutput) {
            git clone https://github.com/ChaseSunstrom/cforge.git
        } else {
            git clone --quiet https://github.com/ChaseSunstrom/cforge.git 2>$null
        }

        Set-Location cforge

        Write-Fetching "dependencies"
        New-Item -ItemType Directory -Force -Path vendor | Out-Null
        Set-Location vendor

        if ($VerboseOutput) {
            git clone https://github.com/fmtlib/fmt.git
            git clone https://github.com/marzer/tomlplusplus.git
        } else {
            git clone --quiet https://github.com/fmtlib/fmt.git 2>$null
            git clone --quiet https://github.com/marzer/tomlplusplus.git 2>$null
        }

        Set-Location fmt; git checkout 11.1.4 --quiet 2>$null; Set-Location ..
        Set-Location tomlplusplus; git checkout v3.4.0 --quiet 2>$null; Set-Location ..
        Set-Location ..

        Write-Configuring "CMake build"
        New-Item -ItemType Directory -Force -Path build | Out-Null

        $cmakeArgs = @(
            "-S", ".",
            "-B", "build",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DFMT_HEADER_ONLY=ON",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DCMAKE_INSTALL_PREFIX=$Prefix"
        )

        # Use Ninja if available
        if (Get-Command ninja -ErrorAction SilentlyContinue) {
            $cmakeArgs += @("-G", "Ninja Multi-Config")
        }

        # Temporarily allow cmake warnings (stderr) without treating as errors
        $prevErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "Continue"

        if ($VerboseOutput) {
            & cmake @cmakeArgs
        } else {
            $configOutput = & cmake @cmakeArgs 2>&1
        }

        if ($LASTEXITCODE -ne 0) {
            $ErrorActionPreference = $prevErrorAction
            Write-Err "CMake configuration failed!"
            if ($configOutput) { Write-Host $configOutput }
            exit 1
        }

        Write-Building "cforge"
        $jobs = [Environment]::ProcessorCount
        if ($VerboseOutput) {
            cmake --build build --config Release -j $jobs
        } else {
            $buildOutput = cmake --build build --config Release -j $jobs 2>&1
        }

        if ($LASTEXITCODE -ne 0) {
            $ErrorActionPreference = $prevErrorAction
            Write-Err "Build failed!"
            if ($buildOutput) { Write-Host $buildOutput }
            exit 1
        }

        $ErrorActionPreference = $prevErrorAction

        Write-Ok "build completed"
        return "$TempDir\cforge\build"

    } finally {
        Pop-Location
    }
}

function Install-Cforge {
    param([string]$BuildDir)

    Write-Installing "to $Prefix"

    # Create directories - use same structure as `cforge install`
    # This is: <prefix>/installed/cforge/bin/
    $installBinDir = "$Prefix\installed\cforge\bin"
    New-Item -ItemType Directory -Force -Path $installBinDir | Out-Null

    # Find the built binary
    $binaryPaths = @(
        "$BuildDir\bin\Release\cforge.exe",
        "$BuildDir\bin\cforge.exe",
        "$BuildDir\Release\cforge.exe",
        "$BuildDir\cforge.exe"
    )

    $binary = $null
    foreach ($path in $binaryPaths) {
        if (Test-Path $path) {
            $binary = $path
            break
        }
    }

    if (-not $binary) {
        Write-Err "Could not find built cforge.exe"
        Get-ChildItem -Path $BuildDir -Recurse -Filter "cforge.exe" | ForEach-Object { Write-Host $_.FullName }
        exit 1
    }

    Copy-Item $binary "$installBinDir\cforge.exe" -Force
    Write-Ok "installed cforge to $installBinDir\cforge.exe"
}

function Add-ToPath {
    if ($NoPath) { return }

    $binDir = "$Prefix\installed\cforge\bin"
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")

    if ($currentPath -like "*$binDir*") {
        Write-Status -Status "Skipping" -Message "$binDir is already in PATH" -Color Cyan
        return
    }

    Write-Adding "$binDir to PATH"

    $newPath = "$binDir;$currentPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")

    # Update current session
    $env:Path = "$binDir;$env:Path"

    Write-Ok "added to PATH"
    Write-Warn "Restart your terminal for PATH changes to take effect"
}

function Test-Installation {
    Write-Status -Status "Verifying" -Message "installation" -Color Cyan

    $cforge = "$Prefix\installed\cforge\bin\cforge.exe"
    if (Test-Path $cforge) {
        $version = & $cforge version 2>$null
        if (-not $version) { $version = "unknown" }

        Write-Ok "cforge installed successfully!"
        Write-Host ""
        Write-Host "  Version:  $version"
        Write-Host "  Location: $cforge"
        Write-Host ""
        Write-Host "Get started:"
        Write-Host "  cforge init my_project    # Create a new project"
        Write-Host "  cd my_project"
        Write-Host "  cforge build              # Build the project"
        Write-Host "  cforge run                # Run the project"
        Write-Host ""
    } else {
        Write-Err "Installation verification failed"
        exit 1
    }
}

# Main
function Main {
    Write-Host ""
    Write-Host "cforge" -ForegroundColor Green -NoNewline
    Write-Host " - C/C++ Build System Installer"
    Write-Host ""

    Write-Status -Status "Prefix" -Message "$Prefix" -Color Cyan
    Write-Host ""

    Test-Dependencies
    Write-Host ""

    # Setup VS environment
    if (-not (Get-VsDevEnv)) {
        Write-Err "Failed to setup Visual Studio environment"
        exit 1
    }
    Write-Host ""

    # Create temp directory
    $tempDir = Join-Path $env:TEMP "cforge-install-$(Get-Random)"
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    Write-Status -Status "Preparing" -Message "build environment" -Color Cyan

    try {
        $buildDir = Build-Cforge -TempDir $tempDir
        Write-Host ""

        Install-Cforge -BuildDir $buildDir
        Add-ToPath
        Write-Host ""

        Test-Installation
    } finally {
        # Cleanup
        if (Test-Path $tempDir) {
            Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
        }
    }
}

Main
