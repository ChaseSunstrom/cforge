param(
  [string]$Version = "latest"
)
$ErrorActionPreference = "Stop"

$Arch = if ([System.Environment]::Is64BitOperatingSystem) { "x86_64" } else { "x86" }
$Asset = "cforge-windows-${Arch}.zip"
$Url = "https://github.com/ChaseSunstrom/cforge/releases/download/v${Version}/${Asset}"
$Dest = "$env:LOCALAPPDATA\cforge\installed\cforge\bin"

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

Write-Host "Downloading cforge ${Version} for windows-${Arch}..."
try {
  Invoke-WebRequest -Uri $Url -OutFile "$env:TEMP\cforge.zip" -UseBasicParsing
  Expand-Archive -Path "$env:TEMP\cforge.zip" -DestinationPath "$env:TEMP\cforge-bin" -Force
  Copy-Item "$env:TEMP\cforge-bin\cforge.exe" -Destination $Dest -Force
} catch {
  Write-Host "No pre-built binary; building from source..."
  git clone --depth 1 --branch "v${Version}" `
    https://github.com/ChaseSunstrom/cforge.git "$env:TEMP\cforge-src"
  cmake -B "$env:TEMP\cforge-src\build" -S "$env:TEMP\cforge-src" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$env:TEMP\cforge-src\build" --config Release
  Copy-Item "$env:TEMP\cforge-src\build\Release\cforge.exe" -Destination $Dest -Force
}
