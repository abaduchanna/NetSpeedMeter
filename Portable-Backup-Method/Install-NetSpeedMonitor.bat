@echo off
REM ============================================================
REM  NetSpeedMonitor 2.5.4.0 - manual installer
REM  for Windows 10 / Windows 11 (no MSI required)
REM  Performs the same steps as the original MSI:
REM    copy files -> regsvr32 nsm.dll -> run configuration
REM ============================================================
title NetSpeedMonitor 2.5.4.0 - Win10/11 installer

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo  [!] Administrator rights are required.
    echo      RIGHT-CLICK this file and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

set "ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set "ARCH=x86"

set "DEST=%ProgramFiles%\NetSpeedMonitor"

echo.
echo  Installing NetSpeedMonitor (%ARCH%) into: %DEST%
if not exist "%DEST%" mkdir "%DEST%"

copy /y "%~dp0bin\%ARCH%\nsm.dll"  "%DEST%\nsm.dll"  >nul
copy /y "%~dp0bin\%ARCH%\nsmc.exe" "%DEST%\nsmc.exe" >nul

echo  Registering taskbar module (nsm.dll) ...
regsvr32 /s "%DEST%\nsm.dll"
if %errorlevel% neq 0 (
    echo.
    echo  [!] Registration failed. Make sure you ran this as Administrator.
    echo.
    pause
    exit /b 1
)

echo.
echo  ============================================================
echo   NetSpeedMonitor installed successfully.
echo.
echo   NEXT STEP (Windows 10):
echo     1. Right-click an EMPTY area of the taskbar
echo     2. Choose  Toolbars  -  NetSpeedMonitor
echo.
echo   The configuration window opens now - pick your language,
echo   font and tooltip options, then click OK.
echo  ============================================================
echo.
echo   NOTE (Windows 11): Microsoft removed taskbar "Toolbars" in
echo   Windows 11, so the speed text cannot be shown inside the
echo   taskbar there. On Windows 11 use a docking alternative such
echo   as TrafficMonitor.
echo.
start "" "%DEST%\nsmc.exe"
pause
exit /b 0
