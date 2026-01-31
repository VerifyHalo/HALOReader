@echo off
REM Run HALOReader and log all output to a file
set PATH=C:\Qt\6.5.3\mingw_64\bin;%PATH%
cd /d "%~dp0build"

set LOGFILE=%~dp0crash_log_%date:~-4,4%%date:~-10,2%%date:~-7,2%_%time:~0,2%%time:~3,2%%time:~6,2%.txt
set LOGFILE=%LOGFILE: =0%

echo Logging to: %LOGFILE%
echo Starting HALOReader...
echo.

REM Redirect both stdout and stderr to log file, and also show on console
HALOReader.exe > "%LOGFILE%" 2>&1

echo.
echo Program exited with code: %ERRORLEVEL%
echo Log saved to: %LOGFILE%
echo.
echo Showing last 50 lines of log:
powershell -Command "Get-Content '%LOGFILE%' -Tail 50"
pause

