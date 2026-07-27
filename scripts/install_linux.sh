#!/bin/bash
set -e

# ==================================================
# 🔊 Yanich DeskSound Linux Installer Script
# ==================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." 2>/dev/null && pwd || echo "$SCRIPT_DIR")"

VERSION_FILE="$ROOT_DIR/version.txt"
if [ ! -f "$VERSION_FILE" ]; then
    VERSION_FILE="$SCRIPT_DIR/version.txt"
fi
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.2.1")
BINARY_NAME="yanich-desksound_v${VERSION_STR}-linux-x64"

# Locate Binary
BIN_PATH=""
if [ -f "$ROOT_DIR/$BINARY_NAME" ]; then
    BIN_PATH="$ROOT_DIR/$BINARY_NAME"
elif [ -f "$SCRIPT_DIR/$BINARY_NAME" ]; then
    BIN_PATH="$SCRIPT_DIR/$BINARY_NAME"
elif [ -f "$SCRIPT_DIR/yanich-desksound" ]; then
    BIN_PATH="$SCRIPT_DIR/yanich-desksound"
elif [ -f "$ROOT_DIR/desksound-linux" ]; then
    BIN_PATH="$ROOT_DIR/desksound-linux"
elif [ -d "$ROOT_DIR" ]; then
    BIN_PATH=$(find "$ROOT_DIR" -maxdepth 2 -type f \( -name "yanich-desksound*" -o -name "desksound-linux" \) ! -name "*.tar.gz" ! -name "*.sh" 2>/dev/null | head -n 1)
fi

if [ -z "$BIN_PATH" ] || [ ! -f "$BIN_PATH" ]; then
    echo "[-] ERROR: Yanich DeskSound binary not found."
    exit 1
fi

# Locate Icon
ICON_PATH=""
if [ -f "$ROOT_DIR/icon.png" ]; then
    ICON_PATH="$ROOT_DIR/icon.png"
elif [ -f "$SCRIPT_DIR/icon.png" ]; then
    ICON_PATH="$SCRIPT_DIR/icon.png"
fi

# Target Directories (User-local installation without needing sudo)
TARGET_BIN_DIR="$HOME/.local/bin"
TARGET_APP_DIR="$HOME/.local/share/applications"
TARGET_ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"

mkdir -p "$TARGET_BIN_DIR"
mkdir -p "$TARGET_APP_DIR"
mkdir -p "$TARGET_ICON_DIR"

echo "=================================================="
echo " 🔊 Installing Yanich DeskSound v${VERSION_STR}..."
echo "=================================================="

# 1. Install Executable Binary
echo "[+] Installing binary to $TARGET_BIN_DIR/yanich-desksound..."
cp -f "$BIN_PATH" "$TARGET_BIN_DIR/yanich-desksound"
chmod +x "$TARGET_BIN_DIR/yanich-desksound"

# 2. Install Application Icon
if [ -f "$ICON_PATH" ]; then
    echo "[+] Installing icon to $TARGET_ICON_DIR/yanich-desksound.png..."
    cp -f "$ICON_PATH" "$TARGET_ICON_DIR/yanich-desksound.png"
fi

# 3. Create Desktop Launcher Entry
DESKTOP_FILE="$TARGET_APP_DIR/yanich-desksound.desktop"
echo "[+] Creating Desktop Shortcut at $DESKTOP_FILE..."

cat <<EOF > "$DESKTOP_FILE"
[Desktop Entry]
Version=1.0
Type=Application
Name=Yanich DeskSound
Comment=Ultra-Low Latency Audio Server for Linux Desktop
Exec=$TARGET_BIN_DIR/yanich-desksound
Icon=yanich-desksound
Terminal=false
Categories=AudioVideo;Audio;Utility;
Keywords=Audio;Sound;Stream;PulseAudio;PipeWire;DeskSound;
StartupWMClass=yanich-desksound
EOF

chmod +x "$DESKTOP_FILE"

# 4. Refresh Desktop Database
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$TARGET_APP_DIR" 2>/dev/null || true
fi

echo "=================================================="
echo " 🎉 SUCCESS: Yanich DeskSound v${VERSION_STR} Installed!"
echo "=================================================="
echo " 📌 លោកអ្នកអាច Search ឈ្មោះ 'Yanich DeskSound' ក្នុង App Launcher"
echo "    ឬរ៉ាន់ 'yanich-desksound' ក្នុង Terminal បានភ្លាមៗ!"
echo "=================================================="
