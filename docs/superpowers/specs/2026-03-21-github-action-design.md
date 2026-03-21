# Design Spec: cforge-action GitHub Action

**Date:** 2026-03-21
**Repo:** `ChaseSunstrom/cforge-action` (separate from cforge itself)
**Status:** Draft

---

## Overview

`cforge-action` is a composite GitHub Action that installs cforge and caches the vendor directory for C/C++ CI workflows. It surfaces `cforge` as a PATH command for all subsequent steps.

---

## Usage

```yaml
- uses: ChaseSunstrom/cforge-action@v1
  with:
    version: '3.1.0'   # optional, default: latest
    cache: true         # optional, default: true - cache vendor/ dir
    cache-key: ''       # optional, custom cache key suffix
```

Subsequent steps use cforge normally:

```yaml
- run: cforge build -c Release
- run: cforge test
```

---

## Repo Layout

```
cforge-action/
├── action.yml
└── scripts/
    ├── install.sh      # Linux / macOS
    └── install.ps1     # Windows
```

---

## action.yml

```yaml
name: 'Setup cforge'
description: 'Install cforge C/C++ build tool and cache dependencies'
author: 'ChaseSunstrom'

branding:
  icon: 'package'
  color: 'blue'

inputs:
  version:
    description: 'cforge version to install (e.g. 3.1.0). Defaults to latest release.'
    required: false
    default: 'latest'
  cache:
    description: 'Cache the vendor/ directory between runs.'
    required: false
    default: 'true'
  cache-key:
    description: 'Optional suffix appended to the cache key.'
    required: false
    default: ''

runs:
  using: composite
  steps:
    # --- 1. Resolve version ---
    - name: Resolve cforge version
      id: resolve-version
      shell: bash
      run: |
        if [ "${{ inputs.version }}" = "latest" ]; then
          VERSION=$(curl -fsSL https://api.github.com/repos/ChaseSunstrom/cforge/releases/latest \
            | grep '"tag_name"' | sed 's/.*"v\([^"]*\)".*/\1/')
        else
          VERSION="${{ inputs.version }}"
        fi
        echo "version=${VERSION}" >> "$GITHUB_OUTPUT"

    # --- 2. Cache: cforge binary ---
    - name: Cache cforge binary
      id: cache-binary
      uses: actions/cache@v4
      with:
        path: |
          ~/.local/share/cforge
          ${{ runner.os == 'Windows' && '%LOCALAPPDATA%\cforge' || '' }}
        key: cforge-binary-${{ runner.os }}-${{ runner.arch }}-${{ steps.resolve-version.outputs.version }}

    # --- 3. Install cforge (if binary cache missed) ---
    - name: Install cforge (Linux / macOS)
      if: steps.cache-binary.outputs.cache-hit != 'true' && runner.os != 'Windows'
      shell: bash
      run: |
        bash "${{ github.action_path }}/scripts/install.sh" "${{ steps.resolve-version.outputs.version }}"

    - name: Install cforge (Windows)
      if: steps.cache-binary.outputs.cache-hit != 'true' && runner.os == 'Windows'
      shell: pwsh
      run: |
        & "${{ github.action_path }}\scripts\install.ps1" -Version "${{ steps.resolve-version.outputs.version }}"

    # --- 4. Add cforge to PATH ---
    - name: Add cforge to PATH (Linux / macOS)
      if: runner.os != 'Windows'
      shell: bash
      run: echo "$HOME/.local/share/cforge/installed/cforge/bin" >> "$GITHUB_PATH"

    - name: Add cforge to PATH (Windows)
      if: runner.os == 'Windows'
      shell: pwsh
      run: |
        $bin = "$env:LOCALAPPDATA\cforge\installed\cforge\bin"
        echo $bin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append

    # --- 5. Cache vendor/ directory ---
    - name: Cache vendor directory
      if: inputs.cache == 'true'
      uses: actions/cache@v4
      with:
        path: |
          vendor/
          build/
        key: cforge-deps-${{ runner.os }}-${{ runner.arch }}-${{ steps.resolve-version.outputs.version }}-${{ hashFiles('**/cforge.toml', '**/cforge.lock') }}${{ inputs.cache-key != '' && format('-{0}', inputs.cache-key) || '' }}
        restore-keys: |
          cforge-deps-${{ runner.os }}-${{ runner.arch }}-${{ steps.resolve-version.outputs.version }}-
          cforge-deps-${{ runner.os }}-${{ runner.arch }}-

    # --- 6. Update package registry ---
    - name: Update cforge registry
      shell: bash
      run: cforge deps update
```

---

## Installer Scripts

### scripts/install.sh

Downloads the pre-built binary from GitHub releases. Falls back to building from source if no binary exists for the current platform.

```bash
#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-latest}"
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

# Normalise arch
case "$ARCH" in
  x86_64)  ARCH="x86_64" ;;
  aarch64|arm64) ARCH="aarch64" ;;
  *) echo "Unsupported arch: $ARCH"; exit 1 ;;
esac

ASSET="cforge-${OS}-${ARCH}.tar.gz"
URL="https://github.com/ChaseSunstrom/cforge/releases/download/v${VERSION}/${ASSET}"

echo "Downloading cforge ${VERSION} for ${OS}-${ARCH}..."
if curl -fsSL "$URL" -o /tmp/cforge.tar.gz 2>/dev/null; then
  tar -xzf /tmp/cforge.tar.gz -C /tmp
  # The release tarball is expected to contain a single binary named 'cforge'
  DEST="$HOME/.local/share/cforge/installed/cforge/bin"
  mkdir -p "$DEST"
  mv /tmp/cforge "$DEST/cforge"
  chmod +x "$DEST/cforge"
else
  echo "No pre-built binary found; building from source..."
  git clone --depth 1 --branch "v${VERSION}" \
    https://github.com/ChaseSunstrom/cforge.git /tmp/cforge-src
  cmake -B /tmp/cforge-src/build -S /tmp/cforge-src -DCMAKE_BUILD_TYPE=Release
  cmake --build /tmp/cforge-src/build --config Release
  DEST="$HOME/.local/share/cforge/installed/cforge/bin"
  mkdir -p "$DEST"
  cp /tmp/cforge-src/build/cforge "$DEST/cforge"
  chmod +x "$DEST/cforge"
fi
```

### scripts/install.ps1

```powershell
param(
  [string]$Version = "latest"
)
$ErrorActionPreference = "Stop"

$Arch = if ([System.Environment]::Is64BitOperatingSystem) { "x86_64" } else { "x86" }
$Asset = "cforge-windows-${Arch}.zip"
$Url = "https://github.com/ChaseSunstrom/cforge/releases/download/v${Version}/${Asset}"
$Dest = "$env:LOCALAPPDATA\cforge\installed\cforge\bin"

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

Write-Host "Downloading cforge ${Version} for windows-${Arch}..."
try {
  Invoke-WebRequest -Uri $Url -OutFile "$env:TEMP\cforge.zip" -UseBasicParsing
  Expand-Archive -Path "$env:TEMP\cforge.zip" -DestinationPath "$env:TEMP\cforge-bin" -Force
  Copy-Item "$env:TEMP\cforge-bin\cforge.exe" -Destination $Dest -Force
} catch {
  Write-Host "No pre-built binary; building from source..."
  git clone --depth 1 --branch "v${Version}" `
    https://github.com/ChaseSunstrom/cforge.git "$env:TEMP\cforge-src"
  cmake -B "$env:TEMP\cforge-src\build" -S "$env:TEMP\cforge-src" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$env:TEMP\cforge-src\build" --config Release
  Copy-Item "$env:TEMP\cforge-src\build\Release\cforge.exe" -Destination $Dest -Force
}
```

---

## Cache Strategy

| Cache | Key | Paths |
|-------|-----|-------|
| Binary | `cforge-binary-{os}-{arch}-{version}` | `~/.local/share/cforge` / `%LOCALAPPDATA%\cforge` |
| Deps | `cforge-deps-{os}-{arch}-{version}-{hash(cforge.toml + cforge.lock)}[-{custom}]` | `vendor/`, `build/` |

Restore keys fall back from most-specific to least-specific so a partial cache hit still speeds up dependency resolution.

---

## Platform Matrix

| Runner | Binary asset |
|--------|-------------|
| `ubuntu-latest` | `cforge-linux-x86_64.tar.gz` |
| `macos-latest` | `cforge-darwin-aarch64.tar.gz` |
| `windows-latest` | `cforge-windows-x86_64.zip` |

---

## Full Example Workflow

```yaml
name: CI
on: [push, pull_request]

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - uses: ChaseSunstrom/cforge-action@v1
        with:
          version: '3.1.0'
          cache: true

      - name: Build
        run: cforge build -c Release

      - name: Test
        run: cforge test -c Release
```

---

## Release Assumptions

- cforge release assets follow the naming scheme `cforge-{os}-{arch}.{tar.gz|zip}`.
- The `latest` version is resolved via the GitHub releases API (no auth required for public repos).
- If a pre-built binary is unavailable the scripts fall back to CMake source build; the runner must have CMake 3.15+ and a C++ compiler (standard on all hosted GitHub runners).

---

## Out of Scope

- Cross-compilation profiles in CI (users configure those in their own `cforge.toml`).
- Publishing/packaging workflows.
- Self-hosted runner support beyond PATH setup.
