@echo off
setlocal enabledelayedexpansion

echo =======================================================================
echo  🔊 Yanich DeskSound - All-in-One Master Build ^& Publish Pipeline (Windows)
echo =======================================================================

cd /d "%~dp0"

rem 1. Sync Version
echo.
echo [1/5] Syncing project version...
powershell -ExecutionPolicy Bypass -File .\scripts\sync_version.ps1
if errorlevel 1 (
    echo [-] Version sync failed. Exiting.
    exit /b 1
)

set /p VERSION_STR=<version.txt
set TAG_NAME=v%VERSION_STR%

echo [!] Target Release Version: %TAG_NAME%

rem 2. Build Windows Server
echo.
echo [2/5] Compiling Unified Cross-Platform Server GUI (server/)...
taskkill /F /IM desksound.exe >nul 2>&1
taskkill /F /IM yanich-desksound_%TAG_NAME%.exe >nul 2>&1

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd server
cl.exe /utf-8 /EHsc /std:c++17 src\main.cpp src\gui_app.cpp src\config_manager.cpp src\logger.cpp src\network_server.cpp src\audio_wasapi.cpp thirdparty\imgui\imgui.cpp thirdparty\imgui\imgui_draw.cpp thirdparty\imgui\imgui_widgets.cpp thirdparty\imgui\imgui_tables.cpp thirdparty\imgui\imgui_impl_win32.cpp thirdparty\imgui\imgui_impl_dx11.cpp /Iinclude /Ithirdparty\imgui /Fe:..\desksound.exe /link /subsystem:windows d3d11.lib d3dcompiler.lib Ws2_32.lib Advapi32.lib Ole32.lib
cd ..

if not exist desksound.exe (
    echo [-] ERROR: Failed to compile desksound.exe.
    exit /b 1
)
copy /Y desksound.exe yanich-desksound_%TAG_NAME%.exe >nul
echo [+] Windows Server compiled successfully: yanich-desksound_%TAG_NAME%.exe

rem 3. Build Android Client APK
echo.
echo [3/5] Building Android Receiver Client Release APK (client-android/)...
set "JAVA_HOME=%~dp0jdk\jdk-17.0.10+7"
cd client-android
call gradlew.bat assembleRelease
cd ..

if exist "client-android\app\build\outputs\apk\release\yanich-desksound_%TAG_NAME%.apk" (
    copy /Y "client-android\app\build\outputs\apk\release\yanich-desksound_%TAG_NAME%.apk" "yanich-desksound_%TAG_NAME%.apk" >nul
    copy /Y "client-android\app\build\outputs\apk\release\yanich-desksound_%TAG_NAME%.apk" "app-release.apk" >nul
    echo [+] Android Release APK built successfully: yanich-desksound_%TAG_NAME%.apk
) else (
    echo [!] Warning: APK output file not found at expected path.
)

rem 4. Git Commit ^& Tag
echo.
echo [4/5] Committing changes ^& pushing git tag %TAG_NAME%...
git add .
git commit -m "Automated build and release %TAG_NAME%"
git tag -a %TAG_NAME% -m "Yanich DeskSound Release %TAG_NAME%" -f
git push origin main --force
git push origin %TAG_NAME% --force

rem 5. Publish to GitHub Releases
echo.
echo [5/5] Publishing assets to GitHub Releases...
powershell -ExecutionPolicy Bypass -File .\scripts\publish_release.ps1 -TagName %TAG_NAME%

echo.
echo =======================================================================
echo  🎉 SUCCESS: Complete Server ^& Client Build + GitHub Release Published!
echo  Release Tag: %TAG_NAME%
echo =======================================================================
