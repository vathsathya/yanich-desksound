param (
    [string]$TagName = "",
    [string]$ReleaseName = "",
    [string]$RepoOwner = "vathsathya",
    [string]$RepoName = "yanich-desksound"
)

$ErrorActionPreference = "Stop"

$rootDir = Resolve-Path "$PSScriptRoot\.."
$versionPath = "$rootDir\version.txt"
$versionStr = if (Test-Path $versionPath) { (Get-Content $versionPath -Raw).Trim() } else { "1.0.7" }

if ([string]::IsNullOrWhiteSpace($TagName)) {
    $TagName = "v$versionStr"
}
if ([string]::IsNullOrWhiteSpace($ReleaseName)) {
    $ReleaseName = "Yanich DeskSound $TagName"
}

Write-Host "Retrieving GitHub access token..." -ForegroundColor Cyan
$credInput = "protocol=https`nhost=github.com`n"
$credOutput = $credInput | git credential fill 2>$null
$tokenLine = $credOutput | Select-String "password="
if (-not $tokenLine) {
    Write-Host "Failed to retrieve GitHub token from Git Credential Manager." -ForegroundColor Red
    exit 1
}
$token = ($tokenLine -split "password=")[1].Trim()

$headers = @{
    "Authorization" = "Bearer $token"
    "Accept"        = "application/vnd.github+json"
    "User-Agent"    = "YanichDeskSound-Publisher"
}

# Check if release already exists for this tag
$releasesUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases"
Write-Host "Checking existing GitHub releases for tag $TagName..." -ForegroundColor Cyan

$existingRelease = $null
try {
    $allReleases = Invoke-RestMethod -Uri $releasesUrl -Headers $headers -Method Get
    $existingRelease = $allReleases | Where-Object { $_.tag_name -eq $TagName }
} catch {
    # No existing releases
}

if ($existingRelease) {
    Write-Host "Deleting old release for tag $TagName..." -ForegroundColor Yellow
    $delUrl = "https://api.github.com/repos/$RepoOwner/$RepoName/releases/$($existingRelease.id)"
    Invoke-RestMethod -Uri $delUrl -Headers $headers -Method Delete | Out-Null
}

# Create Release
Write-Host "Creating GitHub Release $TagName..." -ForegroundColor Green
$bodyJson = @{
    tag_name         = $TagName
    target_commitish = "main"
    name             = $ReleaseName
    body             = "## Yanich DeskSound ${TagName} Official Release`n`n**Full Release Assets:**`n- yanich-desksound_${TagName}.exe (Windows Desktop Server GUI)`n- yanich-desksound_${TagName}.apk (Android Receiver App)`n`n**Key Fixes & Enhancements in ${TagName}:**`n- **Enterprise Codebase Structure**: Restructured into modular platform directories (`server-windows/`, `server-linux/`, `client-android/`, `scripts/`).`n- **Android High-Performance Lock**: Added `WIFI_MODE_FULL_LOW_LATENCY` & `PARTIAL_WAKE_LOCK` for zero Doze mode background throttling.`n- **Mouse Pointer Hover Fix**: Native Windows Arrow (`IDC_ARROW`) by default with precise Hand (`IDC_HAND`) hover feedback.`n`nCreated by Vath Sathya."
    draft            = $false
    prerelease       = $false
} | ConvertTo-Json

$newRelease = Invoke-RestMethod -Uri $releasesUrl -Headers $headers -Method Post -Body $bodyJson -ContentType "application/json"
$uploadUrlRaw = $newRelease.upload_url
$uploadUrlBase = $uploadUrlRaw.Substring(0, $uploadUrlRaw.IndexOf('{'))

Write-Host "Release created: $($newRelease.html_url)" -ForegroundColor Green

# Upload Asset Function
function Upload-FileAsset($filePath, $contentType) {
    $fileName = [System.IO.Path]::GetFileName($filePath)
    if (-not (Test-Path $filePath)) {
        Write-Host "File not found: $filePath" -ForegroundColor Red
        return
    }
    
    $assetUploadUrl = $uploadUrlBase + "?name=" + $fileName
    Write-Host "Uploading binary asset: $fileName..." -ForegroundColor Cyan
    
    $fileBytes = [System.IO.File]::ReadAllBytes($filePath)
    $uploadHeaders = @{
        "Authorization" = "Bearer $token"
        "Accept"        = "application/vnd.github+json"
        "User-Agent"    = "YanichDeskSound-Publisher"
    }

    $res = Invoke-RestMethod -Uri $assetUploadUrl -Headers $uploadHeaders -Method Post -Body $fileBytes -ContentType $contentType
    Write-Host "Uploaded $fileName successfully." -ForegroundColor Green
}

# Upload Binaries
$exeName = "yanich-desksound_$TagName.exe"
$apkName = "yanich-desksound_$TagName.apk"

$exePath = "$rootDir\$exeName"
if (-not (Test-Path $exePath)) {
    if (Test-Path "$rootDir\desksound.exe") {
        Copy-Item "$rootDir\desksound.exe" $exePath -Force
    }
}

$apkPath = "$rootDir\$apkName"
if (-not (Test-Path $apkPath)) {
    if (Test-Path "$rootDir\app-release.apk") {
        Copy-Item "$rootDir\app-release.apk" $apkPath -Force
    }
}

Upload-FileAsset -filePath $exePath -contentType "application/octet-stream"
Upload-FileAsset -filePath $apkPath -contentType "application/vnd.android.package-archive"

Write-Host "`nGitHub Release $TagName successfully published with all binary assets!" -ForegroundColor Green
Write-Host "Release URL: $($newRelease.html_url)" -ForegroundColor Yellow
