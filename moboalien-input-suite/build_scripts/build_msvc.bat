@echo off
setlocal
set ROOT_DIR=%~dp0..

:: Test basic compilation with MSVC first
echo Testing basic compilation with MSVC...
cd /d "%ROOT_DIR%"
echo Current directory: %CD%
echo Looking for test.cpp...
if not exist test.cpp (
    echo ERROR: test.cpp not found in %CD%
    pause
    exit /b 1
)
echo Found test.cpp

:: Try to find Visual Studio
call :find_vs
if "%VCVARS_PATH%"=="" (
    echo Visual Studio not found!
    echo Please install Visual Studio 2019 or 2022 with C++ support
    pause
    exit /b 1
)

echo Found Visual Studio %VS_VERSION%

:: Clean up old files first
if exist test_x86.exe del test_x86.exe
if exist test_x64.exe del test_x64.exe

:: Build both x86 and x64 versions
echo Using Visual Studio %VS_VERSION% for compilation...

echo Setting up x86 environment...
call "%VCVARS_PATH%" x86
if errorlevel 1 (
    echo ERROR: Failed to setup x86 environment
    goto skip_x86
)

echo Compiling x86 version...
cl /EHsc /O2 test.cpp /Fetest_x86.exe
if errorlevel 1 (
    echo ERROR: x86 compilation failed!
) else (
    echo SUCCESS: x86 compilation successful
)

:skip_x86

:: Compile x64 version
echo Setting up x64 environment...
call "%VCVARS_PATH%" x64
if errorlevel 1 (
    echo ERROR: Failed to setup x64 environment
    goto results
)

echo Compiling x64 version...
cl /EHsc /O2 test.cpp /Fetest_x64.exe
if errorlevel 1 (
    echo ERROR: x64 compilation failed!
) else (
    echo SUCCESS: x64 compilation successful
)

:results
echo.
echo Try test_x86.exe first
echo.
pause
goto :eof

:find_vs
:: Look for Visual Studio 2012
if exist "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
    set "VS_VERSION=2012"
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
    set "VS_VERSION=2012"
    goto :eof
)

:: Look for Visual Studio 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_VERSION=2022"
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_VERSION=2022"
    goto :eof
)

:: Look for Visual Studio 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_VERSION=2019"
    goto :eof
)

set "VCVARS_PATH="
set "VS_VERSION="
goto :eof