@echo off
setlocal
set ROOT_DIR=%~dp0..
set DIST_DIR=%ROOT_DIR%\dist

:: Create build directories if they don't exist
if not exist "%ROOT_DIR%\build_vs_x64" mkdir "%ROOT_DIR%\build_vs_x64"
if not exist "%ROOT_DIR%\build_vs_x86" mkdir "%ROOT_DIR%\build_vs_x86"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

:: Find Visual Studio
call :find_vs
if "%VCVARS_PATH%"=="" (
    echo Visual Studio not found!
    echo Please install Visual Studio 2019 or 2022 with C++ support
    pause
    exit /b 1
)

echo Found Visual Studio %VS_VERSION%

:: Check for NASM (Required for SIMD acceleration in libjpeg-turbo)
where nasm >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] NASM not found in PATH!
    echo libjpeg-turbo will be DISABLED. Falling back to GDI+ encoding.
    echo To enable fast JPEG encoding, install NASM from https://www.nasm.us/ and add it to your PATH.
    timeout /t 5
)

:: Force clean dependencies to ensure fresh download of libjpeg-turbo.
:: This fixes the "Compatibility with CMake < 3.5" error caused by cached old versions.
@REM if exist "%ROOT_DIR%\build_vs_x64\_deps" (
@REM     echo Cleaning x64 dependencies...
@REM     rmdir /s /q "%ROOT_DIR%\build_vs_x64\_deps"
@REM     if exist "%ROOT_DIR%\build_vs_x64\CMakeCache.txt" del "%ROOT_DIR%\build_vs_x64\CMakeCache.txt"
@REM )
@REM if exist "%ROOT_DIR%\build_vs_x86\_deps" (
@REM     echo Cleaning x86 dependencies...
@REM     rmdir /s /q "%ROOT_DIR%\build_vs_x86\_deps"
@REM     if exist "%ROOT_DIR%\build_vs_x86\CMakeCache.txt" del "%ROOT_DIR%\build_vs_x86\CMakeCache.txt"
@REM )

:: Test basic compilation first
echo Testing basic compilation with MSVC...
cd /d "%ROOT_DIR%"
call "%VCVARS_PATH%" x64
cl /EHsc /O2 test.cpp /Fetest_msvc.exe >nul 2>&1
if errorlevel 1 (
    echo Basic compilation test failed!
    pause
    exit /b 1
)
echo Basic compilation test passed
del test_msvc.exe >nul 2>&1

:: Generate compile_commands.json for IDE support (clangd / Ctrl+Click)
:: Visual Studio generators don't produce this, so we do a quick Ninja configure
echo Generating compile_commands.json for IDE...
if not exist "%ROOT_DIR%\build_ide" mkdir "%ROOT_DIR%\build_ide"
cmake -S "%ROOT_DIR%" -B "%ROOT_DIR%\build_ide" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >nul 2>&1
if exist "%ROOT_DIR%\build_ide\compile_commands.json" (
    copy /Y "%ROOT_DIR%\build_ide\compile_commands.json" "%ROOT_DIR%\compile_commands.json" >nul
    echo compile_commands.json generated at project root
) else (
    echo [WARNING] Failed to generate compile_commands.json - Ctrl+Click may not work in IDE
)

:: Build x64 with Visual Studio
echo Building x64 with Visual Studio %VS_VERSION%...
cd /d "%ROOT_DIR%\build_vs_x64"
call "%VCVARS_PATH%" x64
if "%VS_VERSION%"=="2012" (
    cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
) else if "%VS_VERSION%"=="2019" (
    cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
) else (
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
if errorlevel 1 (
    echo x64 build configuration failed
    goto skip_x64
)
if "%VS_VERSION%"=="2012" (
    nmake
) else (
    cmake --build . --config Release
)
if errorlevel 1 (
    echo x64 build failed
    goto skip_x64
)
echo x64 build successful

:skip_x64

:: Build x86 with Visual Studio
echo Building x86 with Visual Studio %VS_VERSION%...
cd /d "%ROOT_DIR%\build_vs_x86"
call "%VCVARS_PATH%" x86
if "%VS_VERSION%"=="2012" (
    cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
) else if "%VS_VERSION%"=="2019" (
    cmake .. -G "Visual Studio 16 2019" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
) else (
    cmake .. -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
if errorlevel 1 (
    echo x86 build configuration failed
    goto skip_x86
)
if "%VS_VERSION%"=="2012" (
    nmake
) else (
    cmake --build . --config Release
)
if errorlevel 1 (
    echo x86 build failed
    goto skip_x86
)
echo x86 build successful

:skip_x86

:: Copy to dist
mkdir "%DIST_DIR%\x64"
if exist "%ROOT_DIR%\build_vs_x64\bin\Release" (
    copy "%ROOT_DIR%\build_vs_x64\bin\Release\*" "%DIST_DIR%\x64\" >nul 2>&1
) else if exist "%ROOT_DIR%\build_vs_x64\Release" (
    copy "%ROOT_DIR%\build_vs_x64\Release\*" "%DIST_DIR%\x64\" >nul 2>&1
)

if exist "%ROOT_DIR%\build_vs_x86\bin\Release" (
    mkdir "%DIST_DIR%\x86"
    copy "%ROOT_DIR%\build_vs_x86\bin\Release\*" "%DIST_DIR%\x86\" >nul 2>&1
    echo Build complete. Binaries in %DIST_DIR%\{x64,x86}
) else if exist "%ROOT_DIR%\build_vs_x86\Release" (
    mkdir "%DIST_DIR%\x86"
    copy "%ROOT_DIR%\build_vs_x86\Release\*" "%DIST_DIR%\x86\" >nul 2>&1
    echo Build complete. Binaries in %DIST_DIR%\{x64,x86}
) else (
    echo Build complete. x64 binaries in %DIST_DIR%\x64 (x86 skipped)
)

echo.
pause
goto :eof

:find_vs
:: Look for Visual Studio 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022"
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022"
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2022"
    goto :eof
)

:: Look for Visual Studio 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2019"
    goto :eof
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2019"
    goto :eof
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_VERSION=2019"
    goto :eof
)

:: Look for Visual Studio 2012
if exist "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
    set "VS_VERSION=2012"
    goto :eof
)

set "VCVARS_PATH="
set "VS_VERSION="
goto :eof