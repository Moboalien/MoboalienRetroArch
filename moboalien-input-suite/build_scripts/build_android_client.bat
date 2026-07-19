@echo off
REM Android ARM64 build script for input_client (Windows version)
REM Requires Android NDK to be installed and ANDROID_NDK_HOME set

setlocal enabledelayedexpansion

REM Check if Android NDK is available
set NDK_PATH=%ANDROID_NDK_HOME%
if "%NDK_PATH%"=="" set NDK_PATH=C:\Android\sdk\ndk-bundle

if not exist "%NDK_PATH%" (
    echo Error: Android NDK not found at %NDK_PATH%
    echo Please install Android NDK or set ANDROID_NDK_HOME
    exit /b 1
)

REM Build configuration
set API_LEVEL=21
set ARCH=aarch64
set TARGET_TRIPLE=aarch64-linux-android%API_LEVEL%

REM NDK toolchain paths
set TOOLCHAIN_PATH=%NDK_PATH%\toolchains\llvm\prebuilt\windows-x86_64
set CXX=%TOOLCHAIN_PATH%\bin\%TARGET_TRIPLE%-clang++

REM Check if compiler exists
if not exist "%CXX%" (
    echo Error: Android NDK compiler not found at %CXX%
    exit /b 1
)

echo Building for Android ARM64...
echo NDK: %NDK_PATH%
echo API Level: %API_LEVEL%
echo Architecture: %ARCH%
echo Compiler: %CXX%

REM Build test program first to verify basic compilation
cd /d "%~dp0\.."
set OUTPUT_DIR=%CD%\build_android
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo Testing basic compilation...
"%CXX%" ^
    -std=c++17 ^
    -O2 ^
    -DANDROID ^
    -fPIE ^
    -pie ^
    test.cpp ^
    -o "%OUTPUT_DIR%\test_android_arm64"

if %errorlevel% equ 0 (
    echo ✓ Basic compilation test passed
) else (
    echo ✗ Basic compilation test failed!
    exit /b 1
)

REM Build input_client
echo Building input_client...
"%CXX%" ^
    -std=c++17 ^
    -O2 ^
    -DANDROID ^
    -fPIE ^
    -pie ^
    -I./shared ^
    network/input_client.cpp ^
    -o "%OUTPUT_DIR%\input_client_android_arm64"

if %errorlevel% equ 0 (
    echo ✓ Build successful!
    echo Outputs:
    echo   Test: %OUTPUT_DIR%\test_android_arm64
    echo   Client: %OUTPUT_DIR%\input_client_android_arm64
    echo.
    echo To deploy to Android device:
    echo   adb push %OUTPUT_DIR%\test_android_arm64 /data/local/tmp/
    echo   adb push %OUTPUT_DIR%\input_client_android_arm64 /data/local/tmp/
    echo   adb shell chmod +x /data/local/tmp/test_android_arm64
    echo   adb shell chmod +x /data/local/tmp/input_client_android_arm64
    echo   adb shell /data/local/tmp/test_android_arm64  # Test basic execution
    echo   adb shell /data/local/tmp/input_client_android_arm64 ^<server_ip^>
) else (
    echo ✗ Build failed!
    exit /b 1
)