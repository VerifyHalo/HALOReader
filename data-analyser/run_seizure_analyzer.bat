@echo off
REM Launcher script that sets up PATH for Qt DLLs
set PATH=C:\Qt\6.5.3\mingw_64\bin;%PATH%
cd /d "%~dp0build"
start HALOReader.exe

