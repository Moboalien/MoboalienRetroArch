@echo off
setlocal
set ROOT_DIR=%~dp0..
set DIST_DIR=%ROOT_DIR%\dist

:: Clean previous builds
if exist "%ROOT_DIR%\build_x64" rmdir /s /q "%ROOT_DIR%\build_x64"
if exist "%ROOT_DIR%\build_x86" rmdir /s /q "%ROOT_DIR%\build_x86"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"

:: Test basic compilation first
echo Testing basic compilation with MinGW64...
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%ROOT_DIR%"
g++ -std=c++17 -O2 test.cpp -o test_mingw64.exe
if errorlevel 1 (
    echo Basic compilation test failed!
    pause
    exit /b 1
)
echo ✓ Basic compilation test passed

:: Build x64 with MinGW64
echo Building x64 with MinGW64...
mkdir "%ROOT_DIR%\build_x64"
cd /d "%ROOT_DIR%\build_x64"
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

:: Check if MinGW32 is available
echo Checking for MinGW32...
if not exist "C:\msys64\mingw32\bin\gcc.exe" (
    echo MinGW32 not installed, skipping x86 build
    echo Install with: pacman -S mingw-w64-i686-toolchain
    goto skip_x86
)

:: Build x86 with MinGW32
echo Building x86 with MinGW32...
mkdir "%ROOT_DIR%\build_x86"
cd /d "%ROOT_DIR%\build_x86"
set PATH=C:\msys64\mingw32\bin;%PATH%
set CC=C:\msys64\mingw32\bin\gcc.exe
set CXX=C:\msys64\mingw32\bin\g++.exe
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo x86 build configuration failed
    goto skip_x86
)
cmake --build . --config Release
if errorlevel 1 (
    echo x86 build failed
    goto skip_x86
)

:skip_x86

:: Copy to dist
mkdir "%DIST_DIR%\x64"
copy "%ROOT_DIR%\build_x64\bin\*" "%DIST_DIR%\x64\" >nul 2>&1

if exist "%ROOT_DIR%\build_x86\bin" (
    mkdir "%DIST_DIR%\x86"
    copy "%ROOT_DIR%\build_x86\bin\*" "%DIST_DIR%\x86\" >nul 2>&1
    echo Build complete. Binaries in %DIST_DIR%\{x64,x86}
) else (
    echo Build complete. x64 binaries in %DIST_DIR%\x64 (x86 skipped)
)

pause