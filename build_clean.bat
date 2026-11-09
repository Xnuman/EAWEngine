@echo off
setlocal

echo ============================================
echo Rebuilding EAWEngineProject (Clean)
echo ============================================

if exist build (
    echo.
    echo [1/3] Removing old build directory...
    rmdir /S /Q build
)

echo.
echo [2/3] Running CMake configure...

cmake ^
-S ./ ^
-B ./build ^
-G "Visual Studio 17 2022" ^
-A x64 ^
-DCMAKE_CXX_STANDARD=20

if errorlevel 1 (
    echo.
    echo CMake configuration FAILED.
    pause
    exit /b 1
)

echo.
echo [3/3] Building...

cmake --build ./build --config Release

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    pause
    exit /b 1
)

echo.
echo BUILD SUCCEEDED!
pause