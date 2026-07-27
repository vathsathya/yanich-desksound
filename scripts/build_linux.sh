#!/bin/bash
set -e

# Yanich DeskSound - Linux Build & Package Script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION_FILE="$ROOT_DIR/version.txt"
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.2.1")

echo "=================================================="
echo " 🔊 Building Yanich DeskSound Linux Server v$VERSION_STR"
echo "=================================================="

BUILD_DIR="$ROOT_DIR/server-linux/build"
mkdir -p "$BUILD_DIR"

cd "$ROOT_DIR/server-linux"

APPIND_CFLAGS=$(pkg-config --cflags ayatana-appindicator3-0.1 2>/dev/null || pkg-config --cflags appindicator3-0.1 2>/dev/null || echo "")
APPIND_LIBS=$(pkg-config --libs ayatana-appindicator3-0.1 2>/dev/null || pkg-config --libs appindicator3-0.1 2>/dev/null || echo "")

GTK_CFLAGS="$(pkg-config --cflags gtk+-3.0 2>/dev/null || echo "") $APPIND_CFLAGS"
GTK_LIBS="$(pkg-config --libs gtk+-3.0 2>/dev/null || echo "") $APPIND_LIBS"

if command -v make &> /dev/null && [ -f "Makefile" ]; then
    echo "[+] Compiling using make..."
    make
    OUTPUT_BIN="$BUILD_DIR/desksound-linux"
else
    echo "[+] Compiling using g++ directly..."
    g++ -O3 -std=c++17 src/main_linux.cpp src/audio_pulse.cpp src/config_manager.cpp src/logger.cpp src/tray_linux.cpp src/gui_linux.cpp $GTK_CFLAGS -Iinclude -lpulse-simple -lpulse -lpthread $GTK_LIBS -o "$BUILD_DIR/desksound-linux"
    OUTPUT_BIN="$BUILD_DIR/desksound-linux"
fi

if [ -f "$OUTPUT_BIN" ]; then
    DIST_BIN="$ROOT_DIR/yanich-desksound_v${VERSION_STR}-linux-x64"
    cp -f "$OUTPUT_BIN" "$DIST_BIN"
    chmod +x "$DIST_BIN"

    # Package Release Archive (.tar.gz) for Production Installer
    PKG_DIR="$ROOT_DIR/build_pkg_temp"
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR"

    cp -f "$DIST_BIN" "$PKG_DIR/yanich-desksound"
    chmod +x "$PKG_DIR/yanich-desksound"
    cp -f "$SCRIPT_DIR/install_linux.sh" "$PKG_DIR/install.sh"
    chmod +x "$PKG_DIR/install.sh"
    cp -f "$SCRIPT_DIR/uninstall_linux.sh" "$PKG_DIR/uninstall.sh"
    chmod +x "$PKG_DIR/uninstall.sh"
    if [ -f "$ROOT_DIR/icon.png" ]; then
        cp -f "$ROOT_DIR/icon.png" "$PKG_DIR/icon.png"
    fi
    if [ -f "$ROOT_DIR/README.md" ]; then
        cp -f "$ROOT_DIR/README.md" "$PKG_DIR/README.md"
    fi
    cp -f "$VERSION_FILE" "$PKG_DIR/version.txt"

    TAR_BALL="$ROOT_DIR/yanich-desksound_v${VERSION_STR}-linux-x64.tar.gz"
    tar -czf "$TAR_BALL" -C "$PKG_DIR" .
    rm -rf "$PKG_DIR"

    echo "=================================================="
    echo " SUCCESS: Built Linux Binary & Installer Package:"
    echo " 🔹 Binary: $DIST_BIN"
    echo " 📦 Package: $TAR_BALL"
    echo "=================================================="
else
    echo "[-] ERROR: Build failed. Output binary not found."
    exit 1
fi
