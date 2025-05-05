@echo off
setlocal enabledelayedexpansion

REM Bootstraps building and installing CForge on Windows via batch script

REM Determine project root directory
d:
cd /d "%~dp0..\"
set "PROJECT_ROOT=%cd%"

REM Check for cmake
where cmake >nul 2>&1 || (
  echo Error: cmake not found in PATH
  exit /b 1
)

REM Create and enter build directory
set "BUILD_DIR=%PROJECT_ROOT%\build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Configure the project
cmake .. -DCMAKE_BUILD_TYPE Release
if errorlevel 1 exit /b 1

REM Build the project
cmake --build . --config Release -- /m
if errorlevel 1 exit /b 1

REM Determine install prefix
if defined PREFIX (
  set "INSTALL_DIR=%PREFIX%"
) else (
  set "INSTALL_DIR=%BUILD_DIR%\install"
)

REM Install the project
cmake --install . --prefix "%INSTALL_DIR%"
if errorlevel 1 exit /b 1

echo CForge built and installed to "%INSTALL_DIR%"
echo Add "%INSTALL_DIR%\bin" to your PATH

endlocal 