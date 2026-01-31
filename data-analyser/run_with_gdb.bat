@echo off
REM Run HALOReader with GDB debugger to catch segfaults
set PATH=C:\Qt\6.5.3\mingw_64\bin;%PATH%

REM Check if gdb is available
where gdb >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo GDB not found in PATH. Trying to find it...
    REM Try common MinGW locations
    if exist "C:\Qt\Tools\mingw_64\bin\gdb.exe" (
        set PATH=C:\Qt\Tools\mingw_64\bin;%PATH%
    ) else if exist "C:\mingw64\bin\gdb.exe" (
        set PATH=C:\mingw64\bin;%PATH%
    ) else (
        echo ERROR: GDB not found. Please install MinGW or add gdb to PATH.
        echo.
        echo You can install MinGW via: winget install mingw-w64
        pause
        exit /b 1
    )
)

cd /d "%~dp0build"
echo Starting HALOReader with GDB debugger...
echo When a crash occurs, GDB will show a backtrace.
echo Type 'bt' to see the full stack trace.
echo Type 'quit' to exit GDB.
echo.
gdb --args HALOReader.exe
pause

