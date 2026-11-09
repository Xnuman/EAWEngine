@echo off
setlocal

echo ============================================
echo Incremental Build
echo ============================================

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