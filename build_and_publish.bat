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
echo [1/3] Compiling Windows Server GUI (desksound.exe)...
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
echo [3/3] Publishing Release to GitHub Repository...
git add README.md main.cpp resource.rc android/ build_and_publish.bat .gitignore
git commit -m "Release build v1.0.0: Fresh server & client compilation"
git tag -a v1.0.0 -m "Yanich DeskSound Release v1.0.0" -f
git push origin main --force
git push origin v1.0.0 --force

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================================
    echo   🎉 SUCCESS! Yanich DeskSound v1.0.0 Released!
    echo   - Server GUI: desksound.exe
    echo   - Android Receiver: app-release.apk
    echo   - GitHub Repo: https://github.com/vathsathya/yanich-desksound
    echo ============================================================
) else (
    echo ⚠️ WARNING: Git push encountered an issue. Check network / git config.
)

pause
