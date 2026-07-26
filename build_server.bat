@echo off
taskkill /F /IM desksound.exe >nul 2>&1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
rc.exe /fo resource.res resource.rc
cl.exe /EHsc /std:c++17 main.cpp resource.res /Fe:desksound.exe /link /subsystem:windows
if %ERRORLEVEL% EQU 0 (
    echo BUILD_SERVER_SUCCESS
) else (
    echo BUILD_SERVER_FAILED
)
