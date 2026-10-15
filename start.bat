@echo off
title CurseStone
cd /d "%~dp0"

if not exist "CurseStone.exe" (
    echo [start] CurseStone.exe not found in this folder. Build it and copy it here
    pause
    exit /b 1
)

:loop
CurseStone.exe
if errorlevel 1 (
    echo [start] Server exited with an error, restarting in 5s...
    timeout /t 5 >nul
    goto loop
)