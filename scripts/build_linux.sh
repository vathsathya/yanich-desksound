#!/bin/bash
set -e

# Yanich DeskSound - Linux Build Script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION_FILE="$ROOT_DIR/version.txt"
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.0.7")

echo "=================================================="
echo " 🔊 Building Yanich DeskSound Linux Server v$VERSION_STR"
echo "=================================================="

BUILD_DIR="$ROOT_DIR/server-linux/build"
mkdir -p "$BUILD_DIR"

cd "$ROOT_DIR/server-linux"

if [ -f "CMakeLists.txt" ] && command -v cmake &> /dev/null; then
    echo "[+] Configuring CMake..."
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    echo "[+] Compiling binaries..."
    cmake --build build --config Release
    OUTPUT_BIN="$BUILD_DIR/desksound-linux"
elif command -v make &> /dev/null && [ -f "Makefile" ]; then
    echo "[+] Compiling using make..."
    make
    OUTPUT_BIN="$BUILD_DIR/desksound-linux"
else
    echo "[+] Compiling using g++ directly..."
    g++ -O3 -std=c++17 src/main_linux.cpp src/audio_pulse.cpp -Iinclude -lpulse-simple -lpulse -lpthread -o "$BUILD_DIR/desksound-linux"
    OUTPUT_BIN="$BUILD_DIR/desksound-linux"
fi

if [ -f "$OUTPUT_BIN" ]; then
    DIST_BIN="$ROOT_DIR/yanich-desksound_v${VERSION_STR}-linux-x64"
    cp -f "$OUTPUT_BIN" "$DIST_BIN"
    chmod +x "$DIST_BIN"
    echo "=================================================="
    echo " SUCCESS: Built Linux Binary: $DIST_BIN"
    echo "=================================================="
else
    echo "[-] ERROR: Build failed. Output binary not found."
    exit 1
fi
