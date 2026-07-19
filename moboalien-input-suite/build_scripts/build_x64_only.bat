@echo off
setlocal
set ROOT_DIR=%~dp0..
set DIST_DIR=%ROOT_DIR%\dist

:: Clean previous builds
if exist "%ROOT_DIR%\build_x64" rmdir /s /q "%ROOT_DIR%\build_x64"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"

:: Test basic compilation first
echo Testing basic compilation...
cd /d "%ROOT_DIR%"
g++ -std=c++17 -O2 test.cpp -o test_x64.exe
if errorlevel 1 (
    echo Basic compilation test failed!
    pause
    exit /b 1
)
echo ✓ Basic compilation test passed

:: Build x64 only
echo Building x64 with MinGW...
mkdir "%ROOT_DIR%\build_x64"
cd /d "%ROOT_DIR%\build_x64"
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

:: Copy to dist
mkdir "%DIST_DIR%"
copy "%ROOT_DIR%\build_x64\bin\*" "%DIST_DIR%\" >nul 2>&1

echo Build complete. x64 binaries in %DIST_DIR%
pause