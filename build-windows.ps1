# Builds th07.exe with Visual Studio.
#
#   powershell -ExecutionPolicy Bypass -File build-windows.ps1 -Game C:\games\th07
#
# -Game    your game folder (the one with th07.dat, thbgm.dat and msgothic.ttc); th07.exe and the
#          SDL DLLs are copied there. Without it they stay in build-windows\Release\.
# -Config  Release (default) or Debug
#
# Needs Visual Studio 2022 or 2026 with "Desktop development with C++" (it includes CMake) and git.
# The vendored SDL libraries are fetched and built the first time.
param(
    [string]$Game,
    [ValidateSet("Release", "Debug")][string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
# Its own folder: a Visual Studio build can't reuse a build\ set up with another generator.
$Build = Join-Path $Root "build-windows"

function Fail($Message) { Write-Host "error: $Message" -ForegroundColor Red; exit 1 }

if ($Game -and -not (Test-Path $Game -PathType Container)) { Fail "$Game is not a folder" }

$Vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $Vswhere)) { Fail "Visual Studio not found. Install it with 'Desktop development with C++'" }
$Vs = & $Vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $Vs) { Fail "the C++ tools are missing. Add 'Desktop development with C++' in the Visual Studio Installer" }

# Visual Studio's own CMake always knows its generator; a separately installed one may be too old
# for a newer Visual Studio.
$CMake = Join-Path $Vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) {
    $CMake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $CMake) { Fail "CMake not found. Add 'C++ CMake tools for Windows' in the Visual Studio Installer" }
}

if (-not (Test-Path (Join-Path $Root "vendored\SDL\CMakeLists.txt")) -or
    -not (Test-Path (Join-Path $Root "vendored\SDL_ttf\external\freetype\CMakeLists.txt"))) {
    Write-Host "Fetching the vendored SDL libraries"
    git -C $Root submodule update --init --depth 1
    if ($LASTEXITCODE -ne 0) { Fail "git submodule update failed" }
    git -C (Join-Path $Root "vendored\SDL_ttf") submodule update --init --depth 1 external/freetype external/harfbuzz external/plutosvg external/plutovg
    if ($LASTEXITCODE -ne 0) { Fail "git submodule update failed" }
}

# TH_EXTERNAL_ASSETS: th07.exe reads the game files from the folder it runs in.
Write-Host "Configuring"
& $CMake -S $Root -B $Build -DTH_EXTERNAL_ASSETS=ON | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "cmake could not configure the build" }
Write-Host "Building ($Config, a few minutes the first time)"
& $CMake --build $Build --config $Config --parallel -- /nologo /v:minimal "/clp:ErrorsOnly"
if ($LASTEXITCODE -ne 0) { Fail "the build failed" }

$Exe = Join-Path $Build "$Config\th07.exe"
if (-not (Test-Path $Exe)) { Fail "the build finished but $Exe is missing" }
# The vendored SDL libraries are built as DLLs in their own folders; th07.exe needs them next to it.
$Dlls = @(Get-ChildItem -Recurse -Filter *.dll $Build |
    Where-Object { $_.Directory.Name -eq $Config -and $_.FullName -notmatch "\\CMakeFiles\\" } |
    ForEach-Object { $_.FullName })
$Dest = Split-Path -Parent $Exe
$Elsewhere = @($Dlls | Where-Object { (Split-Path -Parent $_) -ne $Dest })
if ($Elsewhere) { Copy-Item $Elsewhere $Dest }
if ($Game) {
    $Dest = (Resolve-Path $Game).Path
    Copy-Item (@($Exe) + $Dlls) $Dest
}

Write-Host ""
Write-Host "Done: th07.exe is in $Dest" -ForegroundColor Green
if (-not $Game) { Write-Host "Copy it and the SDL*.dll files into your game folder, or run again with -Game <game folder>." }
