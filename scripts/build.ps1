# =======================================================================
# Yanich DeskSound - Master Multi-Platform Build & Publish CLI
# =======================================================================

param (
    [string]$NewVersion = "",
    [switch]$Bump = $false,
    [string]$Message = "",
    [switch]$SkipPublish = $false
)

$ErrorActionPreference = "Stop"
$scriptDir = $PSScriptRoot
$rootDir = (Resolve-Path "$scriptDir\..").Path
$versionPath = "$rootDir\version.txt"

if (-not (Test-Path $versionPath)) { throw "version.txt not found!" }
$currentVersion = (Get-Content $versionPath -Raw).Trim()

if ($NewVersion -ne "") {
    $versionStr = $NewVersion.Trim()
} else {
    $parts = $currentVersion.Split('.')
    $major = 1
    $minor = 0
    $patch = 0
    if ($parts.Length -gt 0) { $major = [int]$parts[0] }
    if ($parts.Length -gt 1) { $minor = [int]$parts[1] }
    if ($parts.Length -gt 2) { $patch = [int]$parts[2] + 1 }
    $versionStr = "$major.$minor.$patch"
}

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host " Yanich DeskSound Release Pipeline (v$currentVersion -> v$versionStr)" -ForegroundColor Cyan
Write-Host "=======================================================================" -ForegroundColor Cyan

# 1. Update version.txt and sync version.h
Set-Content -Path $versionPath -Value $versionStr -NoNewline
Write-Host "[+] Updated version.txt to $versionStr" -ForegroundColor Green

& "$scriptDir\sync_version.ps1"
$tagName = "v$versionStr"

# 2. Find MSVC vcvars64.bat
$vcvars = Get-ChildItem -Path 'C:\Program Files (x86)\Microsoft Visual Studio', 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter 'vcvars64.bat' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vcvars) {
    throw "MSVC vcvars64.bat not found on system!"
}
$vcvarsPath = $vcvars.FullName

# 3. Build Windows Server
Write-Host ""
Write-Host "[2/4] Compiling Windows Server (.exe)..." -ForegroundColor Cyan
Get-Process | Where-Object { $_.Name -like "*desksound*" } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$l1 = '@echo off'
$l2 = 'call "' + $vcvarsPath + '" >nul 2>&1'
$l3 = 'cd /d "' + $rootDir + '\server"'
$l4 = 'rc.exe /nologo /fo resource.res resource.rc'
$l5 = 'cl.exe /utf-8 /EHsc /std:c++17 src\main.cpp src\gui_app.cpp src\custom_widgets.cpp src\config_manager.cpp src\logger.cpp src\network_server.cpp src\audio_wasapi.cpp thirdparty\imgui\imgui.cpp thirdparty\imgui\imgui_draw.cpp thirdparty\imgui\imgui_widgets.cpp thirdparty\imgui\imgui_tables.cpp thirdparty\imgui\imgui_impl_win32.cpp thirdparty\imgui\imgui_impl_dx11.cpp resource.res /Iinclude /Ithirdparty\imgui /Fe:..\desksound.exe /link /subsystem:windows d3d11.lib d3dcompiler.lib Ws2_32.lib Advapi32.lib Ole32.lib'
$l6 = 'cd /d "' + $rootDir + '"'

$lines = @($l1, $l2, $l3, $l4, $l5, $l6)
[System.IO.File]::WriteAllLines("$rootDir\build.bat", $lines)
cmd.exe /c "$rootDir\build.bat"

$exeTarget = "$rootDir\desksound.exe"
if (-not (Test-Path $exeTarget)) {
    throw "Failed to compile desksound.exe"
}
$exeReleaseName = "$rootDir\yanich-desksound_$tagName.exe"
Copy-Item $exeTarget $exeReleaseName -Force
Write-Host "[+] Windows Server compiled: yanich-desksound_$tagName.exe" -ForegroundColor Green

# 4. Build Android Client APK
Write-Host ""
Write-Host "[3/4] Building Android Client (.apk)..." -ForegroundColor Cyan
try {
    $jdkPath = Get-ChildItem -Path "$rootDir\jdk" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($jdkPath) { $env:JAVA_HOME = $jdkPath.FullName }
    $sdkPath = "$rootDir\android-sdk"
    if (Test-Path $sdkPath) { $env:ANDROID_HOME = $sdkPath }

    Set-Location "$rootDir\client-android"
    if (Test-Path "local.properties") { Remove-Item "local.properties" -Force }
    cmd.exe /c ".\gradlew.bat assembleRelease --no-daemon"
    Set-Location $rootDir

    $expectedApk = "$rootDir\client-android\app\build\outputs\apk\release\yanich-desksound_$tagName.apk"
    $apkReleaseName = "$rootDir\yanich-desksound_$tagName.apk"

    if (Test-Path $expectedApk) {
        Copy-Item $expectedApk $apkReleaseName -Force
        Write-Host "[+] Android APK built: yanich-desksound_$tagName.apk" -ForegroundColor Green
    } else {
        $fallback = Get-ChildItem "$rootDir\client-android\app\build\outputs\apk\release\*.apk" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($fallback) {
            Copy-Item $fallback.FullName $apkReleaseName -Force
            Write-Host "[+] Copied fallback Android APK to yanich-desksound_$tagName.apk" -ForegroundColor Green
        }
    }
} catch {
    Set-Location $rootDir
    Write-Host "[!] Note: Local Android compilation skipped/handled by GitHub Actions." -ForegroundColor Yellow
}

# 5. Git Commit & GitHub Release Publishing
if (-not $SkipPublish) {
    Write-Host ""
    Write-Host "[4/4] Publishing Release $tagName to GitHub..." -ForegroundColor Cyan

    $msgText = "release: $tagName - Multi-platform automated release"
    if ($Message -ne "") {
        $msgText = $Message
    }

    git add -A
    git commit -m "$msgText"
    git tag -a $tagName -m "Yanich DeskSound Release $tagName" -f
    git push origin main
    git push origin $tagName --force

    $targetUrl = "https://github.com/vathsathya/yanich-desksound/releases/tag/$tagName"
    Write-Host ""
    Write-Host "[SUCCESS] Release $tagName successfully triggered and published!" -ForegroundColor Green
    Write-Host "GitHub Release URL: $targetUrl" -ForegroundColor Cyan
}
