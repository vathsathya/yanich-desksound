@echo off
set VERSION=%1
if "%VERSION%"=="" (
    powershell -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1"
) else (
    powershell -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -NewVersion "%VERSION%"
)
