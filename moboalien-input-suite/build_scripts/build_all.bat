@echo off
setlocal
set ROOT_DIR=%~dp0..
set DIST_DIR=%ROOT_DIR%\dist

:: Clean previous builds
if exist "%ROOT_DIR%\build_x64" rmdir /s /q "%ROOT_DIR%\build_x64"
if exist "%ROOT_DIR%\build_x86" rmdir /s /q "%ROOT_DIR%\build_x86"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"

:: Build x64
echo Building x64...
mkdir "%ROOT_DIR%\build_x64"
cd /d "%ROOT_DIR%\build_x64"
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

:: Build x86
echo Building x86...
mkdir "%ROOT_DIR%\build_x86"
cd /d "%ROOT_DIR%\build_x86"
cmake .. -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

:: Copy to dist
mkdir "%DIST_DIR%\x64" "%DIST_DIR%\x86"
copy "%ROOT_DIR%\build_x64\bin\*" "%DIST_DIR%\x64\" >nul 2>&1
copy "%ROOT_DIR%\build_x86\bin\*" "%DIST_DIR%\x86\" >nul 2>&1

echo Build complete. Binaries in %DIST_DIR%\{x64,x86}
pause