@echo off
setlocal
set ROOT_DIR=%~dp0..

echo Testing static linking for compatibility...
cd /d "%ROOT_DIR%"

:: Test MinGW with static linking
echo Building with MinGW static linking...
set PATH=C:\msys64\mingw64\bin;%PATH%
g++ -std=c++17 -O2 -static-libgcc -static-libstdc++ -static test.cpp -o test_static_mingw.exe
if errorlevel 1 (
    echo MinGW static build failed!
) else (
    echo ✓ MinGW static build successful
)

:: Test MinGW x86 with static linking 
if exist "C:\msys64\mingw32\bin\g++.exe" (
    echo Building x86 with MinGW32 static linking...
    set PATH=C:\msys64\mingw32\bin;%PATH%
    C:\msys64\mingw32\bin\g++.exe -std=c++17 -O2 -static-libgcc -static-libstdc++ -static test.cpp -o test_static_mingw32.exe
    if errorlevel 1 (
        echo MinGW32 static build failed!
    ) else (
        echo ✓ MinGW32 static build successful
    )
)

echo.
echo Static executables created (no DLL dependencies):
if exist test_static_mingw.exe echo   test_static_mingw.exe (x64, static)
if exist test_static_mingw32.exe echo   test_static_mingw32.exe (x86, static)
echo.
pause