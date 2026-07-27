#!/bin/bash
set -e

# Yanich DeskSound - Linux GitHub Release Publisher
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

REPO_OWNER="vathsathya"
REPO_NAME="yanich-desksound"

VERSION_FILE="$ROOT_DIR/version.txt"
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.0.7")
TAG_NAME="v${VERSION_STR}"
RELEASE_NAME="Yanich DeskSound ${TAG_NAME}"

echo "=================================================="
echo " 🚀 Publishing Linux Release ${TAG_NAME} to GitHub"
echo "=================================================="

# 1. Build Linux Binary
bash "$SCRIPT_DIR/build_linux.sh"

# 2. Retrieve GitHub Token
if [ -n "$GITHUB_TOKEN" ]; then
    TOKEN="$GITHUB_TOKEN"
else
    echo "[+] Retrieving GitHub access token from git credential manager..."
    TOKEN=$(echo -e "protocol=https\nhost=github.com\n" | git credential fill 2>/dev/null | grep "password=" | cut -d'=' -f2)
fi

if [ -z "$TOKEN" ]; then
    echo "[-] ERROR: GitHub access token not found. Set GITHUB_TOKEN environment variable or configure git credentials."
    exit 1
fi

HEADERS=(
    -H "Authorization: Bearer ${TOKEN}"
    -H "Accept: application/vnd.github+json"
    -H "User-Agent: YanichDeskSound-Linux-Publisher"
)

# 3. Check for existing release
TAG_RELEASE_URL="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/tags/${TAG_NAME}"
EXISTING_RELEASE_ID=$(curl -s "${HEADERS[@]}" "${TAG_RELEASE_URL}" | grep '"id":' | head -n 1 | awk '{print $2}' | tr -d ',')

if [ -n "$EXISTING_RELEASE_ID" ] && [ "$EXISTING_RELEASE_ID" != "null" ]; then
    echo "[!] Deleting existing release ID ${EXISTING_RELEASE_ID} for ${TAG_NAME}..."
    curl -s -X DELETE "${HEADERS[@]}" "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/${EXISTING_RELEASE_ID}" > /dev/null
fi

# 4. Create Release
echo "[+] Creating GitHub Release ${TAG_NAME}..."
RELEASE_BODY="## Yanich DeskSound ${TAG_NAME} Official Release\n\n**Release Assets:**\n- yanich-desksound_${TAG_NAME}-linux-x64 (Ubuntu / Linux Desktop Server)\n- yanich-desksound_${TAG_NAME}.exe (Windows Desktop Server GUI)\n- yanich-desksound_${TAG_NAME}.apk (Android Receiver Client App)\n\nCreated by Vath Sathya."

CREATE_JSON=$(cat <<EOF
{
  "tag_name": "${TAG_NAME}",
  "target_commitish": "main",
  "name": "${RELEASE_NAME}",
  "body": "${RELEASE_BODY}",
  "draft": false,
  "prerelease": false
}
EOF
)

RELEASE_RESPONSE=$(curl -s -X POST "${HEADERS[@]}" -H "Content-Type: application/json" -d "${CREATE_JSON}" "${RELEASES_URL}")
UPLOAD_URL=$(echo "$RELEASE_RESPONSE" | grep "\"upload_url\":" | head -n 1 | cut -d'"' -f4 | cut -d'{' -f1)
HTML_URL=$(echo "$RELEASE_RESPONSE" | grep "\"html_url\":" | head -n 1 | cut -d'"' -f4)

if [ -z "$UPLOAD_URL" ]; then
    echo "[-] ERROR: Failed to create GitHub release."
    echo "$RELEASE_RESPONSE"
    exit 1
fi

echo "[+] Release created successfully: ${HTML_URL}"

# 5. Upload Linux Asset
BIN_PATH="$ROOT_DIR/yanich-desksound_${TAG_NAME}-linux-x64"
ASSET_NAME="yanich-desksound_${TAG_NAME}-linux-x64"

if [ -f "$BIN_PATH" ]; then
    echo "[+] Uploading Linux binary asset: ${ASSET_NAME}..."
    curl -s -X POST "${HEADERS[@]}" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@${BIN_PATH}" \
        "${UPLOAD_URL}?name=${ASSET_NAME}" > /dev/null
    echo "[+] Uploaded ${ASSET_NAME} successfully!"
fi

echo "=================================================="
echo " SUCCESS: Published ${TAG_NAME} to GitHub Releases!"
echo " Release URL: ${HTML_URL}"
echo "=================================================="
