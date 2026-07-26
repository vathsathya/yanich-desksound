@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Yanich DeskSound Automated Build and Release Publisher
echo   Created by Vath Sathya
echo ============================================================
echo.

REM 0. Read single source of truth version.txt
set /p APP_VER=<version.txt
set "APP_VER=%APP_VER: =%"
set "TAG_NAME=v%APP_VER%"

echo Syncing version %TAG_NAME% across C++ and Gradle...
powershell -ExecutionPolicy Bypass -File "%~dp0sync_version.ps1"

REM 1. Setup Java 17 Environment for Android Gradle
set "JAVA_HOME=%~dp0jdk\jdk-17.0.10+7"
set "PATH=%JAVA_HOME%\bin;%PATH%"

REM 2. Compile Windows Desktop Server GUI (desksound.exe & yanich-desksound_vX.X.X.exe)
echo [1/4] Compiling Windows Server GUI (yanich-desksound_%TAG_NAME%.exe)...
taskkill /F /IM desksound.exe >nul 2>&1
taskkill /F /IM yanich-desksound_%TAG_NAME%.exe >nul 2>&1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rc.exe /fo resource.res resource.rc
cl.exe /EHsc /std:c++17 main.cpp resource.res /Fe:desksound.exe /link /subsystem:windows
if %ERRORLEVEL% NEQ 0 (
    echo ❌ ERROR: Failed to compile desksound.exe!
    pause
    exit /b 1
)
copy /Y "desksound.exe" "yanich-desksound_%TAG_NAME%.exe" >nul
echo ✅ yanich-desksound_%TAG_NAME%.exe compiled successfully!
echo.

REM 3. Compile Android Receiver Client App (yanich-desksound_vX.X.X.apk)
echo [2/3] Compiling Android Receiver App (yanich-desksound_%TAG_NAME%.apk)...
cd android
call .\gradlew.bat assembleRelease
if %ERRORLEVEL% NEQ 0 (
    echo ❌ ERROR: Failed to compile Android APK!
    cd ..
    pause
    exit /b 1
)
cd ..
copy /Y "android\app\build\outputs\apk\release\yanich-desksound_%TAG_NAME%.apk" "yanich-desksound_%TAG_NAME%.apk" >nul
copy /Y "android\app\build\outputs\apk\release\yanich-desksound_%TAG_NAME%.apk" "app-release.apk" >nul
echo ✅ yanich-desksound_%TAG_NAME%.apk compiled successfully!
echo.

REM 4. Git Tag & Publish Source Code Release to GitHub
echo [3/4] Pushing Source Code and Tag to GitHub...
git add README.md main.cpp resource.rc version.txt version.h sync_version.ps1 android/ build_and_publish.bat publish_release.ps1 .gitignore
git commit -m "Release build %TAG_NAME%: Versioned binaries output"
git tag -a %TAG_NAME% -m "Yanich DeskSound Release %TAG_NAME%" -f
git push origin main --force
git push origin %TAG_NAME% --force

REM 5. Create Official GitHub Release & Upload Binary Assets via API
echo.
echo [4/4] Creating GitHub Release and Uploading Binary Assets...
powershell -ExecutionPolicy Bypass -File "%~dp0publish_release.ps1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================================
    echo   🎉 SUCCESS! Yanich DeskSound %TAG_NAME% Published!
    echo   - Release Link: https://github.com/vathsathya/yanich-desksound/releases/tag/%TAG_NAME%
    echo   - Server GUI: yanich-desksound_%TAG_NAME%.exe
    echo   - Android App: yanich-desksound_%TAG_NAME%.apk
    echo ============================================================
) else (
    echo ⚠️ WARNING: Release API step encountered an issue.
)
