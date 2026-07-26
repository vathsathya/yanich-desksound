@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Yanich DeskSound Automated Build and Release Publisher
echo   Created by Vath Sathya
echo ============================================================
echo.

REM 1. Setup Java 17 Environment for Android Gradle
set "JAVA_HOME=%~dp0jdk\jdk-17.0.10+7"
set "PATH=%JAVA_HOME%\bin;%PATH%"

REM 2. Compile Windows Desktop Server GUI (desksound.exe)
echo [1/4] Compiling Windows Server GUI (desksound.exe)...
taskkill /F /IM desksound.exe >nul 2>&1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rc.exe /fo resource.res resource.rc
cl.exe /EHsc /std:c++17 main.cpp resource.res /Fe:desksound.exe /link /subsystem:windows
if %ERRORLEVEL% NEQ 0 (
    echo ❌ ERROR: Failed to compile desksound.exe!
    pause
    exit /b 1
)
echo ✅ desksound.exe compiled successfully!
echo.

REM 3. Compile Android Receiver Client App (app-release.apk)
echo [2/3] Compiling Android Receiver App (app-release.apk)...
cd android
call .\gradlew.bat assembleRelease
if %ERRORLEVEL% NEQ 0 (
    echo ❌ ERROR: Failed to compile Android APK!
    cd ..
    pause
    exit /b 1
)
cd ..
copy /Y "android\app\build\outputs\apk\release\app-release.apk" "app-release.apk" >nul
echo ✅ app-release.apk compiled successfully!
echo.

REM 4. Git Tag & Publish Source Code Release to GitHub
echo [3/4] Pushing Source Code and Tag to GitHub...
git add README.md main.cpp resource.rc android/ build_and_publish.bat publish_release.ps1 .gitignore
git commit -m "Release build v1.0.1: Refactored Server & Client GUI with bug fixes"
git tag -a v1.0.1 -m "Yanich DeskSound Release v1.0.1" -f
git push origin main --force
git push origin v1.0.1 --force

REM 5. Create Official GitHub Release & Upload Binary Assets via API
echo.
echo [4/4] Creating GitHub Release and Uploading Binary Assets...
powershell -ExecutionPolicy Bypass -File "%~dp0publish_release.ps1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================================
    echo   🎉 SUCCESS! Yanich DeskSound v1.0.1 Published!
    echo   - Release Link: https://github.com/vathsathya/yanich-desksound/releases/tag/v1.0.1
    echo   - Server GUI: desksound.exe
    echo   - Android App: app-release.apk
    echo ============================================================
) else (
    echo ⚠️ WARNING: Release API step encountered an issue.
)
