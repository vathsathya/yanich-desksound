# =======================================================================
# 🔊 Yanich DeskSound - Unified Master Build & Publish Pipeline
# =======================================================================

param (
    [switch]$SkipPublish = $false
)

$ErrorActionPreference = "Stop"
$rootDir = Resolve-Path "$PSScriptRoot\.."
$versionPath = "$rootDir\version.txt"

# 1. Sync Version
Write-Host "`n[1/4] Syncing project version..." -ForegroundColor Cyan
if (-not (Test-Path $versionPath)) { throw "version.txt not found!" }
$versionStr = (Get-Content $versionPath -Raw).Trim()
$tagName = "v$versionStr"

$parts = $versionStr.Split('.')
$major = if ($parts.Length -gt 0) { $parts[0] } else { "1" }
$minor = if ($parts.Length -gt 1) { $parts[1] } else { "0" }
$patch = if ($parts.Length -gt 2) { $parts[2] } else { "0" }

$headerPath = "$rootDir\server\include\version.h"
$hContent = @"
#ifndef VERSION_H
#define VERSION_H

#define APP_VERSION_MAJOR $major
#define APP_VERSION_MINOR $minor
#define APP_VERSION_PATCH $patch
#define APP_VERSION_BUILD 0

#define APP_VERSION_STRING "$versionStr"
#define APP_VERSION_TAG "v$versionStr"

#endif // VERSION_H
"@
[System.IO.File]::WriteAllText($headerPath, $hContent.Replace("`r`n", "`n").Replace("`n", "`r`n"))
Write-Host "[+] Synced version.h to $versionStr at $headerPath" -ForegroundColor Green

# 2. Compile Unified Windows Server (DirectX 11 + WASAPI + ImGui)
Write-Host "`n[2/4] Compiling Windows Server (server/)..." -ForegroundColor Cyan
Get-Process | Where-Object { $_.Name -like "*desksound*" } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$msvcCmd = 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d "' + "$rootDir\server" + '" && rc.exe /nologo /fo resource.res resource.rc && cl.exe /utf-8 /EHsc /std:c++17 src\main.cpp src\gui_app.cpp src\custom_widgets.cpp src\config_manager.cpp src\logger.cpp src\network_server.cpp src\audio_wasapi.cpp thirdparty\imgui\imgui.cpp thirdparty\imgui\imgui_draw.cpp thirdparty\imgui\imgui_widgets.cpp thirdparty\imgui\imgui_tables.cpp thirdparty\imgui\imgui_impl_win32.cpp thirdparty\imgui\imgui_impl_dx11.cpp resource.res /Iinclude /Ithirdparty\imgui /Fe:..\desksound.exe /link /subsystem:windows d3d11.lib d3dcompiler.lib Ws2_32.lib Advapi32.lib Ole32.lib'

cmd /c $msvcCmd

$exeTarget = "$rootDir\desksound.exe"
if (-not (Test-Path $exeTarget)) {
    throw "Failed to compile desksound.exe"
}
$exeReleaseName = "$rootDir\yanich-desksound_$tagName.exe"
Copy-Item $exeTarget $exeReleaseName -Force
Write-Host "[+] Windows Server compiled: yanich-desksound_$tagName.exe" -ForegroundColor Green

# 3. Compile Android Client APK
Write-Host "`n[3/4] Building Android Client Release APK..." -ForegroundColor Cyan
try {
    if (Test-Path "$rootDir\jdk\jdk-17.0.10+7") {
        $env:JAVA_HOME = "$rootDir\jdk\jdk-17.0.10+7"
    }
    Set-Location "$rootDir\client-android"
    if (Test-Path "local.properties") { Remove-Item "local.properties" -Force }
    cmd /c ".\gradlew.bat assembleRelease --no-daemon"
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
    Write-Host "[!] Note: Local Android APK compilation skipped or requires Android SDK environment. (GitHub Actions builds APK automatically)." -ForegroundColor Yellow
}

# 4. Git Commit & GitHub Release Publishing
if (-not $SkipPublish) {
    Write-Host "`n[4/4] Publishing Release $tagName to GitHub..." -ForegroundColor Cyan
    git add -A
    git commit -m "Automated build and release $tagName"
    git tag -a $tagName -m "Yanich DeskSound Release $tagName" -f
    git push origin main --force
    git push origin $tagName --force

    # Publish to GitHub API
    $credInput = "protocol=https`nhost=github.com`n"
    $credOutput = $credInput | git credential fill 2>$null
    $tokenLine = $credOutput | Select-String "password="
    if ($tokenLine) {
        $token = ($tokenLine -split "password=")[1].Trim()
        $headers = @{ "Authorization" = "Bearer $token"; "Accept" = "application/vnd.github+json"; "User-Agent" = "YanichDeskSound-Publisher" }
        $releasesUrl = "https://api.github.com/repos/vathsathya/yanich-desksound/releases"

        try {
            $allReleases = Invoke-RestMethod -Uri $releasesUrl -Headers $headers -Method Get
            $existing = $allReleases | Where-Object { $_.tag_name -eq $tagName }
            if ($existing) {
                Invoke-RestMethod -Uri "$releasesUrl/$($existing.id)" -Headers $headers -Method Delete | Out-Null
            }
        } catch {}

        $bodyJson = @{ tag_name = $tagName; target_commitish = "main"; name = "Yanich DeskSound $tagName"; body = "## Release $tagName`n- Windows Server (.exe)`n- Android App (.apk)"; draft = $false; prerelease = $false } | ConvertTo-Json
        $newRel = Invoke-RestMethod -Uri $releasesUrl -Headers $headers -Method Post -Body $bodyJson -ContentType "application/json"
        $uploadUrlBase = $newRel.upload_url.Substring(0, $newRel.upload_url.IndexOf('{'))

        # Upload assets
        foreach ($assetPath in @($exeReleaseName, $apkReleaseName)) {
            if (Test-Path $assetPath) {
                $fName = [System.IO.Path]::GetFileName($assetPath)
                $bytes = [System.IO.File]::ReadAllBytes($assetPath)
                Invoke-RestMethod -Uri "$uploadUrlBase`?name=$fName" -Headers $headers -Method Post -Body $bytes -ContentType "application/octet-stream" | Out-Null
                Write-Host "[+] Uploaded asset to GitHub: $fName" -ForegroundColor Green
            }
        }
        Write-Host "`n🎉 Release $tagName published: $($newRel.html_url)" -ForegroundColor Green
    }
}
