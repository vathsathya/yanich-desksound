#!/bin/bash
set -e

# Yanich DeskSound - All-in-One Master Build & Publish Pipeline (Linux)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "======================================================================="
echo " 🔊 Yanich DeskSound - All-in-One Master Build & Publish Pipeline (Linux)"
echo "======================================================================="

VERSION_FILE="$ROOT_DIR/version.txt"
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.0.7")
TAG_NAME="v${VERSION_STR}"

echo "[!] Target Release Version: ${TAG_NAME}"

# 1. Build Linux Server
echo ""
echo "[1/4] Building Linux Server (server-linux/)..."
bash "$ROOT_DIR/scripts/build_linux.sh"

# 2. Build Android Client APK (If Android SDK / Gradle is configured)
echo ""
echo "[2/4] Building Android Receiver Client Release APK (client-android/)..."
if [ -d "$ROOT_DIR/client-android" ]; then
    cd "$ROOT_DIR/client-android"
    if [ -f "./gradlew" ]; then
        chmod +x ./gradlew
        ./gradlew assembleRelease || echo "[!] Warning: Gradle build failed or Android SDK not found on Linux."
        if [ -f "$ROOT_DIR/client-android/app/build/outputs/apk/release/yanich-desksound_${TAG_NAME}.apk" ]; then
            cp -f "$ROOT_DIR/client-android/app/build/outputs/apk/release/yanich-desksound_${TAG_NAME}.apk" "$ROOT_DIR/yanich-desksound_${TAG_NAME}.apk"
            cp -f "$ROOT_DIR/client-android/app/build/outputs/apk/release/yanich-desksound_${TAG_NAME}.apk" "$ROOT_DIR/app-release.apk"
            echo "[+] Android Release APK copied to root!"
        fi
    fi
    cd "$ROOT_DIR"
fi

# 3. Git Commit & Tag
echo ""
echo "[3/4] Committing changes & pushing git tag ${TAG_NAME}..."
git add .
git commit -m "Automated Linux build and release ${TAG_NAME}" || true
git tag -a "${TAG_NAME}" -m "Yanich DeskSound Release ${TAG_NAME}" -f
git push origin main --force
git push origin "${TAG_NAME}" --force

# 4. Publish to GitHub Releases
echo ""
echo "[4/4] Publishing Linux assets to GitHub Releases..."
bash "$ROOT_DIR/scripts/publish_release_linux.sh"

echo ""
echo "======================================================================="
echo " 🎉 SUCCESS: Complete Linux Server & Client Build + GitHub Release Published!"
echo " Release Tag: ${TAG_NAME}"
echo "======================================================================="
