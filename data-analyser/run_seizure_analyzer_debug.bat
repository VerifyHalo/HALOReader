@echo off
REM Debug launcher that shows console output and error messages
REM Allocate a console for the GUI application
title HALOReader Debug Console

set PATH=C:\Qt\6.5.3\mingw_64\bin;%PATH%
cd /d "%~dp0build"

echo ========================================
echo HALOReader Debug Console
echo ========================================
echo.
echo Starting HALOReader with debug output...
echo Log file location will be shown below.
echo.
echo NOTE: This console will show stderr output.
echo Full logs are saved to: %%APPDATA%%\HALO Reading Controller\
echo.
echo ========================================
echo.

REM Run the application - stderr will show in console
HALOReader.exe 2>&1

echo.
echo ========================================
echo Program exited with code: %ERRORLEVEL%
echo ========================================
echo.
echo To view full log file:
echo 1. Press Win+R
echo 2. Type: %%APPDATA%%\HALO Reading Controller
echo 3. Press Enter
echo.
pause

