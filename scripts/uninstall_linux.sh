#!/bin/bash
set -e

# ==================================================
# 🗑️ Yanich DeskSound Linux Uninstaller Script
# ==================================================

TARGET_BIN="$HOME/.local/bin/yanich-desksound"
TARGET_APP="$HOME/.local/share/applications/yanich-desksound.desktop"
TARGET_ICON="$HOME/.local/share/icons/hicolor/256x256/apps/yanich-desksound.png"
AUTOSTART_APP="$HOME/.config/autostart/yanich-desksound.desktop"

echo "=================================================="
echo " 🗑️ Uninstalling Yanich DeskSound..."
echo "=================================================="

# Kill active process
killall -9 desksound-linux yanich-desksound 2>/dev/null || true

# Remove installed files
if [ -f "$TARGET_BIN" ]; then
    rm -f "$TARGET_BIN"
    echo "[+] Removed executable: $TARGET_BIN"
fi

if [ -f "$TARGET_APP" ]; then
    rm -f "$TARGET_APP"
    echo "[+] Removed desktop entry: $TARGET_APP"
fi

if [ -f "$TARGET_ICON" ]; then
    rm -f "$TARGET_ICON"
    echo "[+] Removed icon: $TARGET_ICON"
fi

if [ -f "$AUTOSTART_APP" ]; then
    rm -f "$AUTOSTART_APP"
    echo "[+] Removed autostart entry: $AUTOSTART_APP"
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

echo "=================================================="
echo " ✅ SUCCESS: Yanich DeskSound has been uninstalled."
echo "=================================================="
