@echo off
setlocal enabledelayedexpansion

REM Auto-install missing tools
echo Checking for required tools and installing if missing...

REM Git
where git >nul 2>&1
if errorlevel 1 (
    echo git not found. Attempting to install...
    where choco >nul 2>&1 && (
        choco install git --yes
    ) || (
        where winget >nul 2>&1 && (
            winget install --id Git.Git -e --source winget --silent
        ) || (
            echo Please install git manually
            exit /b 1
        )
    )
)

REM CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake not found. Attempting to install...
    where choco >nul 2>&1 && (
        choco install cmake --yes
    ) || (
        where winget >nul 2>&1 && (
            winget install --id Kitware.CMake -e --source winget --silent
        ) || (
            echo Please install CMake manually
            exit /b 1
        )
    )
)

REM C++ Compiler
where cl >nul 2>&1
if errorlevel 1 (
    where g++ >nul 2>&1
    if errorlevel 1 (
        echo C++ compiler not found. Attempting to install...
        where choco >nul 2>&1 && (
            choco install visualstudio2019buildtools --yes --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools"
        ) || (
            where winget >nul 2>&1 && (
                winget install --id Microsoft.VisualStudio.2019.BuildTools -e --source winget --silent
            ) || (
                echo Please install a C++ compiler manually
                exit /b 1
            )
        )
    )
)

REM Ninja
where ninja >nul 2>&1
if errorlevel 1 (
    echo Ninja not found. Attempting to install...
    where choco >nul 2>&1 && (
        choco install ninja --yes
    ) || (
        where winget >nul 2>&1 && (
            winget install --id ninja-build.ninja -e --source winget --silent
        ) || (
            echo Please install Ninja manually
            exit /b 1
        )
    )
)

REM Bootstraps building and installing CForge on Windows via batch script

REM Determine project root directory
d:
cd /d "%~dp0..\"
set "PROJECT_ROOT=%cd%"

REM Create and enter build directory
set "BUILD_DIR=%PROJECT_ROOT%\build"
REM Clear existing build directory to avoid stale files
if exist "%BUILD_DIR%" (
  echo Removing existing build directory: "%BUILD_DIR%"
  rmdir /s /q "%BUILD_DIR%"
)
REM Recreate build directory
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Setup Visual Studio Build Tools environment
set "VSPATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSPATH%" (
    for /f "usebackq tokens=*" %%I in (`"%VSPATH%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
) else (
    REM Fallback to VS 2022 Community, Professional, Enterprise, or BuildTools
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
    if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
    if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
    if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
)
if defined VSINSTALL (
    echo Configuring Visual Studio environment from %VSINSTALL%...
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
) else (
    echo Warning: Visual Studio Build Tools not found; C++ compiler may not be on PATH
)

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