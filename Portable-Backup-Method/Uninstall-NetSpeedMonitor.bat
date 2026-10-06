@echo off
REM ============================================================
REM  NetSpeedMonitor uninstaller (Windows 10 / 11 manual install)
REM ============================================================
title NetSpeedMonitor uninstaller

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo  [!] Administrator rights are required.
    echo      RIGHT-CLICK this file and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

set "DEST=%ProgramFiles%\NetSpeedMonitor"

if exist "%DEST%\nsm.dll" regsvr32 /u /s "%DEST%\nsm.dll"

echo.
echo  If the speed display is still visible, right-click the taskbar,
echo  open "Toolbars" and uncheck NetSpeedMonitor first.
echo.
rmdir /s /q "%DEST%" 2>nul

echo  NetSpeedMonitor removed.
echo.
pause
exit /b 0
