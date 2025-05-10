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
# Clear existing build directory to avoid stale files
if exist "%BUILD_DIR%" (
  echo Removing existing build directory: "%BUILD_DIR%"
  rmdir /s /q "%BUILD_DIR%"
)
# Recreate build directory
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Clone Git dependencies if not already cloned
cd /d "%PROJECT_ROOT%\vendor"
if not exist "fmt" (
  echo Cloning fmt...
  git clone https://github.com/fmtlib/fmt.git fmt
  cd fmt && git checkout 11.1.4 && cd ..
)
if not exist "tomlplusplus" (
  echo Cloning tomlplusplus...
  git clone https://github.com/marzer/tomlplusplus.git tomlplusplus
  cd tomlplusplus && git checkout v3.4.0 && cd ..
)
cd /d "%BUILD_DIR%"

REM Configure the project (clear vcpkg, header-only fmt, static libs)
cmake -G "Ninja Multi-Config" -U CMAKE_TOOLCHAIN_FILE -U VCPKG_CHAINLOAD_TOOLCHAIN_FILE -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -DCMAKE_TOOLCHAIN_FILE="" -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="" -DCMAKE_BUILD_TYPE=Release -DFMT_HEADER_ONLY=ON -DBUILD_SHARED_LIBS=OFF
if errorlevel 1 exit /b 1

REM Build the project
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

REM Install via build system target
cmake --build "%BUILD_DIR%" --config Release --target install
if errorlevel 1 exit /b 1

REM Run cforge install with built executable
cd /d "%PROJECT_ROOT%"
"%BUILD_DIR%\bin\Release\cforge.exe" install

echo CForge built and installed to "%BUILD_DIR%"
echo Add "%BUILD_DIR%\bin" to your PATH

endlocal 