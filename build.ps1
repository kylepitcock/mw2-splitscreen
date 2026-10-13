# Builds build\Release\dsound.dll (the split screen mod).
# Requires Visual Studio 2022 or Build Tools with the C++ x86 toolset.
param([switch]$Install)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with C++ tools not found" }
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

& $cmake -S $root -B "$root\build" -G "Visual Studio 17 2022" -A Win32 | Out-Null
if ($LASTEXITCODE) { throw "cmake configure failed" }
& $cmake --build "$root\build" --config Release -- /v:minimal /nologo
if ($LASTEXITCODE) { throw "build failed" }

if ($Install) {
    $game = (Get-Content "$root\game-path.txt" -ErrorAction SilentlyContinue)
    if (-not $game) { throw "Put your MW2 folder path in game-path.txt" }
    Copy-Item "$root\build\Release\dsound.dll" (Join-Path $game "dsound.dll") -Force
    Write-Host "installed to $game"
}
