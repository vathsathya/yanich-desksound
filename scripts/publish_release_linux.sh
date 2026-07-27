#!/bin/bash
set -e

# Yanich DeskSound - Multi-Platform GitHub Release Publisher (Linux)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

REPO_OWNER="vathsathya"
REPO_NAME="yanich-desksound"

VERSION_FILE="$ROOT_DIR/version.txt"
VERSION_STR=$(cat "$VERSION_FILE" 2>/dev/null | tr -d '\r\n' || echo "1.2.0")
TAG_NAME="v${VERSION_STR}"
RELEASE_NAME="Yanich DeskSound ${TAG_NAME}"

echo "=================================================="
echo " 🚀 Publishing Release ${TAG_NAME} to GitHub"
echo "=================================================="

# 1. Build Linux Binary & Tarball Package
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

# 3. Check for existing release or create new
RELEASES_URL="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases"
TAG_RELEASE_URL="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/tags/${TAG_NAME}"
RELEASE_INFO=$(curl -s "${HEADERS[@]}" "${TAG_RELEASE_URL}" || true)
EXISTING_RELEASE_ID=$(echo "$RELEASE_INFO" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('id', ''))" 2>/dev/null || true)

if [ -n "$EXISTING_RELEASE_ID" ] && [ "$EXISTING_RELEASE_ID" != "None" ] && [ "$EXISTING_RELEASE_ID" != "" ]; then
    echo "[+] Found existing release ID ${EXISTING_RELEASE_ID} for ${TAG_NAME}. Updating assets..."
    UPLOAD_URL=$(echo "$RELEASE_INFO" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('upload_url', '').split('{')[0])" 2>/dev/null || true)
    HTML_URL=$(echo "$RELEASE_INFO" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('html_url', ''))" 2>/dev/null || true)
else
    echo "[+] Creating new GitHub Release ${TAG_NAME}..."
    RELEASE_BODY="## Yanich DeskSound ${TAG_NAME} Official Release\n\n**Release Assets:**\n- yanich-desksound_${TAG_NAME}-linux-x64.tar.gz (Production Installer Package for Linux Desktop)\n- yanich-desksound_${TAG_NAME}-linux-x64 (Standalone Executable Binary)\n- yanich-desksound_${TAG_NAME}.exe (Windows Desktop Server GUI)\n- yanich-desksound_${TAG_NAME}.apk (Android Receiver Client App)\n\nCreated by Vath Sathya."

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
    EXISTING_RELEASE_ID=$(echo "$RELEASE_RESPONSE" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('id', ''))" 2>/dev/null || true)
    UPLOAD_URL=$(echo "$RELEASE_RESPONSE" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('upload_url', '').split('{')[0])" 2>/dev/null || true)
    HTML_URL=$(echo "$RELEASE_RESPONSE" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('html_url', ''))" 2>/dev/null || true)
fi

if [ -z "$UPLOAD_URL" ]; then
    echo "[-] ERROR: Failed to get upload URL for GitHub release."
    exit 1
fi

echo "[+] Release ready: ${HTML_URL}"

# 4. Helper function to upload an asset (deleting old asset with same name if present)
upload_asset() {
    local file_path="$1"
    local asset_name="$2"
    local content_type="$3"

    if [ ! -f "$file_path" ]; then
        echo "[!] File not found: ${file_path}, skipping..."
        return
    fi

    # Check if asset already exists on this release
    local rel_assets=$(curl -s "${HEADERS[@]}" "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/${EXISTING_RELEASE_ID}/assets" || true)
    local old_asset_id=$(echo "$rel_assets" | python3 -c "import sys, json; data=json.load(sys.stdin); matches=[a['id'] for a in data if a.get('name')=='${asset_name}']; print(matches[0] if matches else '')" 2>/dev/null || true)

    if [ -n "$old_asset_id" ] && [ "$old_asset_id" != "None" ] && [ "$old_asset_id" != "" ]; then
        echo "[!] Replacing existing asset '${asset_name}' (ID: ${old_asset_id})..."
        curl -s -X DELETE "${HEADERS[@]}" "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases/assets/${old_asset_id}" > /dev/null
    fi

    echo "[+] Uploading asset: ${asset_name}..."
    curl -s -X POST "${HEADERS[@]}" \
        -H "Content-Type: ${content_type}" \
        --data-binary "@${file_path}" \
        "${UPLOAD_URL}?name=${asset_name}" > /dev/null
    echo "[+] Uploaded ${asset_name} successfully!"
}

# 5. Upload all multi-platform release assets
upload_asset "$ROOT_DIR/yanich-desksound_${TAG_NAME}-linux-x64.tar.gz" "yanich-desksound_${TAG_NAME}-linux-x64.tar.gz" "application/gzip"
upload_asset "$ROOT_DIR/yanich-desksound_${TAG_NAME}-linux-x64" "yanich-desksound_${TAG_NAME}-linux-x64" "application/octet-stream"
upload_asset "$ROOT_DIR/yanich-desksound_${TAG_NAME}.exe" "yanich-desksound_${TAG_NAME}.exe" "application/octet-stream"
upload_asset "$ROOT_DIR/yanich-desksound_${TAG_NAME}.apk" "yanich-desksound_${TAG_NAME}.apk" "application/vnd.android.package-archive"

echo "=================================================="
echo " SUCCESS: Published ${TAG_NAME} with all multi-platform assets to GitHub!"
echo " Release URL: ${HTML_URL}"
echo "=================================================="
