@echo off
REM Build the launcher. Requires Visual Studio 2019/2022 with the
REM "Desktop development with C++" workload (MSVC + Windows SDK).
REM CMake does NOT need to be on PATH: this script auto-locates the copy
REM bundled with Visual Studio, so a plain double-click works.
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM 1) CMake already on PATH?
REM ---------------------------------------------------------------------------
set "CMAKE=cmake"
where cmake >nul 2>nul
if not errorlevel 1 goto have_cmake

REM ---------------------------------------------------------------------------
REM 2) CMake bundled with Visual Studio (found via vswhere)
REM ---------------------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
)
if defined VSPATH echo Detected Visual Studio: "!VSPATH!"
if defined VSPATH if exist "!VSPATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE=!VSPATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    goto have_cmake
)

REM ---------------------------------------------------------------------------
REM 3) Standalone CMake install locations
REM ---------------------------------------------------------------------------
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe" & goto have_cmake
if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles(x86)%\CMake\bin\cmake.exe" & goto have_cmake

goto no_cmake

:have_cmake
echo Using CMake: "!CMAKE!"
"!CMAKE!" --version
echo.

if not exist build mkdir build
"!CMAKE!" -S . -B build -A x64
if errorlevel 1 goto cfg_fail

"!CMAKE!" --build build --config Release
if errorlevel 1 goto build_fail

echo.
echo Built: build\Release\launcher.exe
echo.
pause
exit /b 0

:no_cmake
echo.
echo [ERROR] Could not find CMake anywhere.
echo   CMake ships with Visual Studio - install "Visual Studio 2019/2022"
echo   and tick the "Desktop development with C++" workload, then rerun this.
echo   Or install CMake standalone from https://cmake.org/download/ and add it to PATH.
echo.
pause
exit /b 1

:cfg_fail
echo.
echo [ERROR] CMake configure failed - see the messages above.
echo   Most common cause: Visual Studio with the C++ workload is not installed.
echo.
pause
exit /b 1

:build_fail
echo.
echo [ERROR] Build failed - see the compiler messages above.
echo.
pause
exit /b 1
