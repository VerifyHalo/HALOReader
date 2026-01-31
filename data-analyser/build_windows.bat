@echo off
REM Windows build script for Seizure Analyzer
REM Requires: Qt (qmake), MinGW or MSVC, HDF5

echo Building Seizure Analyzer for Windows...

REM Check if qmake is available
where qmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: qmake not found in PATH
    echo Please install Qt and add it to your PATH, or set QTDIR environment variable
    echo Example: set QTDIR=C:\Qt\6.5.0\mingw_64
    exit /b 1
)

REM Create build directory
if not exist build mkdir build
cd src\gui

REM Run qmake
echo Running qmake...
qmake seizure_analyzer.pro
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: qmake failed
    exit /b 1
)

REM Build with make (MinGW) or nmake (MSVC)
echo Building...
where mingw32-make >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Using MinGW make...
    mingw32-make
) else (
    where nmake >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        echo Using MSVC nmake...
        nmake
    ) else (
        where make >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            echo Using make...
            make
        ) else (
            echo ERROR: No make tool found (mingw32-make, nmake, or make)
            exit /b 1
        )
    )
)

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

REM Copy executable to build directory
cd ..\..
if exist src\gui\seizure_analyzer.exe (
    copy src\gui\seizure_analyzer.exe build\seizure_analyzer.exe
    echo.
    echo Build successful! Executable: build\seizure_analyzer.exe
) else if exist src\gui\debug\seizure_analyzer.exe (
    copy src\gui\debug\seizure_analyzer.exe build\seizure_analyzer.exe
    echo.
    echo Build successful! Executable: build\seizure_analyzer.exe
) else if exist src\gui\release\seizure_analyzer.exe (
    copy src\gui\release\seizure_analyzer.exe build\seizure_analyzer.exe
    echo.
    echo Build successful! Executable: build\seizure_analyzer.exe
) else (
    echo WARNING: Executable not found in expected locations
    echo Please check src\gui directory for the executable
)

echo.
echo Note: Make sure okFrontPanel.dll is in the lib\ directory
echo       and HDF5 DLLs are in your PATH or next to the executable

