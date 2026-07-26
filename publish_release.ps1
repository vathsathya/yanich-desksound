param (
    [string]$TagName = "v1.0.2",
    [string]$ReleaseName = "Yanich DeskSound v1.0.2",
    [string]$RepoOwner = "vathsathya",
    [string]$RepoName = "yanich-desksound"
)

$ErrorActionPreference = "Stop"

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
    body             = "## Yanich DeskSound v1.0.2 Official Release`n`n**Full Release Assets:**`n- desksound.exe (Windows Desktop Server GUI)`n- app-release.apk (Android Receiver App)`n`n**Key Fixes & Enhancements in v1.0.2:**`n- **Auto-Update System**: Added Startup Auto-Update check on launch for Android Client App & Windows Server GUI.`n- **Windows Server GUI**: DWM Immersive Dark Mode integration, Studio Slate Cards (14px radius), Inline Version Badge, dynamic signal peak colors.`n- **Android Receiver Client**: Material 3 Update Dialog, top notification banner, Coroutine async release query.`n`nCreated by Vath Sathya."
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
$workDir = $PSScriptRoot
if (-not $workDir) { $workDir = Get-Location }

Upload-FileAsset -filePath "$workDir\desksound.exe" -contentType "application/octet-stream"
Upload-FileAsset -filePath "$workDir\app-release.apk" -contentType "application/vnd.android.package-archive"

Write-Host "`nGitHub Release v1.0.0 successfully published with all binary assets!" -ForegroundColor Green
Write-Host "Release URL: $($newRelease.html_url)" -ForegroundColor Yellow
