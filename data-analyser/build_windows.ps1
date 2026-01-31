# PowerShell build script for Seizure Analyzer
# Requires: Qt (qmake), MinGW or MSVC, HDF5

Write-Host "Building Seizure Analyzer for Windows..." -ForegroundColor Cyan

# Check if qmake is available
$qmakePath = Get-Command qmake -ErrorAction SilentlyContinue
if (-not $qmakePath) {
    Write-Host "ERROR: qmake not found in PATH" -ForegroundColor Red
    Write-Host "Please install Qt and add it to your PATH, or set QTDIR environment variable" -ForegroundColor Yellow
    Write-Host "Example: `$env:QTDIR = 'C:\Qt\6.5.0\mingw_64'" -ForegroundColor Yellow
    exit 1
}

# Create build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location "src\gui"

# Run qmake
Write-Host "Running qmake..." -ForegroundColor Cyan
& qmake seizure_analyzer.pro
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: qmake failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

# Build with make (MinGW) or nmake (MSVC)
Write-Host "Building..." -ForegroundColor Cyan
$makeTool = $null

$mingwMake = Get-Command mingw32-make -ErrorAction SilentlyContinue
if ($mingwMake) {
    Write-Host "Using MinGW make..." -ForegroundColor Green
    & mingw32-make
    $makeTool = "mingw32-make"
} else {
    $nmake = Get-Command nmake -ErrorAction SilentlyContinue
    if ($nmake) {
        Write-Host "Using MSVC nmake..." -ForegroundColor Green
        & nmake
        $makeTool = "nmake"
    } else {
        $make = Get-Command make -ErrorAction SilentlyContinue
        if ($make) {
            Write-Host "Using make..." -ForegroundColor Green
            & make
            $makeTool = "make"
        } else {
            Write-Host "ERROR: No make tool found (mingw32-make, nmake, or make)" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# Copy executable to build directory
$exeFound = $false
$exePaths = @(
    "src\gui\seizure_analyzer.exe",
    "src\gui\debug\seizure_analyzer.exe",
    "src\gui\release\seizure_analyzer.exe"
)

foreach ($exePath in $exePaths) {
    if (Test-Path $exePath) {
        Copy-Item $exePath "build\seizure_analyzer.exe" -Force
        Write-Host ""
        Write-Host "Build successful! Executable: build\seizure_analyzer.exe" -ForegroundColor Green
        $exeFound = $true
        break
    }
}

if (-not $exeFound) {
    Write-Host "WARNING: Executable not found in expected locations" -ForegroundColor Yellow
    Write-Host "Please check src\gui directory for the executable" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Note: Make sure okFrontPanel.dll is in the lib\ directory" -ForegroundColor Yellow
Write-Host "      and HDF5 DLLs are in your PATH or next to the executable" -ForegroundColor Yellow

