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
