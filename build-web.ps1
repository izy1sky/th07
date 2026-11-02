# Builds the browser version into dist\, ready to serve or upload to Bilibili Toy.
# Windows version of build-web.sh.
#
#   powershell -ExecutionPolicy Bypass -File build-web.ps1 -Game C:\games\th07 -Emsdk C:\emsdk
#
# -Game    your game folder: th07.dat, thbgm.dat and msgothic.ttc; they are only read
# -Emsdk   the emsdk folder; not needed if it is in C:\emsdk or %USERPROFILE%\emsdk, or emsdk_env.ps1 already ran
#
# Needs CMake, Ninja, Python 3 and ffmpeg (for the music, converted once into build\web-bgm\):
# winget install Kitware.CMake Ninja-build.Ninja Gyan.FFmpeg Python.Python.3.12
param(
    [Parameter(Mandatory = $true)][string]$Game,
    [string]$Emsdk
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Assets = Join-Path $Root "assets"
$Build = Join-Path $Root "build-web"
$Out = Join-Path $Root "dist"
$Cache = Join-Path $Root "build\web-bgm"

function Fail($Message) { Write-Host "error: $Message" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Game -PathType Container)) { Fail "$Game is not a folder" }
$Game = (Resolve-Path $Game).Path.TrimEnd("\")
if ($Game -eq $Assets) { Fail "pass the folder with your original game files, not assets\ (this script fills assets\)" }

if ($Emsdk) {
    if (-not (Test-Path (Join-Path $Emsdk "emsdk_env.ps1"))) { Fail "$Emsdk has no emsdk_env.ps1" }
} elseif (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
    # Not activated in this window: look in the usual places.
    $Emsdk = @($env:EMSDK, (Join-Path $HOME "emsdk"), "C:\emsdk") |
        Where-Object { $_ -and (Test-Path (Join-Path $_ "emsdk_env.ps1")) } | Select-Object -First 1
}
if ($Emsdk) {
    $env:EMSDK_QUIET = "1"
    & (Join-Path $Emsdk "emsdk_env.ps1") | Out-Null
}

# Report everything that is missing at once rather than one thing per run.
$Problems = @()
if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
    $Problems += "emsdk not found: pass -Emsdk <emsdk folder>, or install it in C:\emsdk (https://emscripten.org/docs/getting_started/downloads.html)"
}
$Python = (Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
# emcmake on Windows builds with Ninja (or MinGW make); Visual Studio can't build for the web.
$Tools = [ordered]@{ cmake = "Kitware.CMake"; ninja = "Ninja-build.Ninja"; ffmpeg = "Gyan.FFmpeg"; python = "Python.Python.3.12" }
$Missing = @($Tools.Keys | Where-Object {
    if ($_ -eq "python") { -not $Python } else { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
})
if ($Missing) {
    $Problems += "$($Missing -join ', ') not found: winget install $(($Missing | ForEach-Object { $Tools[$_] }) -join ' ')"
}
$MissingFiles = @("th07.dat", "thbgm.dat", "msgothic.ttc" | Where-Object { -not (Test-Path (Join-Path $Game $_)) })
if ($MissingFiles) {
    $Problems += "$Game has no $($MissingFiles -join ', '): pass the folder with your game files (th07.dat, thbgm.dat, msgothic.ttc)"
}
if ($Problems) {
    foreach ($p in $Problems) { Write-Host "error: $p" -ForegroundColor Red }
    exit 1
}

# The music conversion leaves a 16-byte thbgm.dat in assets\; a big one there is someone's
# original, which the copy below would delete.
$OldBgm = Join-Path $Assets "thbgm.dat"
if ((Test-Path $OldBgm) -and (Get-Item $OldBgm).Length -gt 1024) {
    Fail "assets\thbgm.dat is the original music file. Move it to your game folder and pass that folder"
}

if (-not (Test-Path (Join-Path $Root "vendored\SDL\CMakeLists.txt")) -or
    -not (Test-Path (Join-Path $Root "vendored\SDL_ttf\external\freetype\CMakeLists.txt"))) {
    Write-Host "Fetching the vendored SDL libraries"
    git -C $Root submodule update --init --depth 1
    if ($LASTEXITCODE -ne 0) { Fail "git submodule update failed" }
    git -C (Join-Path $Root "vendored\SDL_ttf") submodule update --init --depth 1 external/freetype external/harfbuzz external/plutosvg external/plutovg
    if ($LASTEXITCODE -ne 0) { Fail "git submodule update failed" }
}

# The raw music (444 MB) is too big to ship, so it becomes one MP3 per track. That takes a few
# minutes, so it is done once into build\web-bgm\ and redone only when the game files change.
$Stub = Join-Path $Cache "thbgm.dat"
$Stale = -not (Test-Path $Stub)
if (-not $Stale) {
    $StubTime = (Get-Item $Stub).LastWriteTime
    $Stale = (Get-Item (Join-Path $Game "thbgm.dat")).LastWriteTime -gt $StubTime -or
        (Get-Item (Join-Path $Game "th07.dat")).LastWriteTime -gt $StubTime
}
if ($Stale) {
    Write-Host "Converting the music to MP3 (a few minutes, done once)"
    if (Test-Path $Cache) { Remove-Item -Recurse -Force $Cache }
    New-Item -ItemType Directory -Force $Cache | Out-Null
    # The converter replaces thbgm.dat with a stub, so it works on copies.
    Copy-Item (Join-Path $Game "th07.dat"), (Join-Path $Game "thbgm.dat") $Cache
    & $Python (Join-Path $Root "tools\convert_bgm_web.py") --assets $Cache --bitrate 192k
    if ($LASTEXITCODE -ne 0) { Fail "the music conversion failed" }
    Remove-Item (Join-Path $Cache "th07.dat")
}

# Everything in assets\ is packed into th07.data.
Write-Host "Copying game files from $Game"
if (Test-Path $Assets) { Remove-Item -Recurse -Force $Assets }
New-Item -ItemType Directory -Force $Assets | Out-Null
Copy-Item (Join-Path $Game "th07.dat"), (Join-Path $Game "msgothic.ttc"), $Stub $Assets
Copy-Item -Recurse (Join-Path $Cache "bgm") $Assets

$Generator = @()
if (-not (Test-Path (Join-Path $Build "CMakeCache.txt"))) { $Generator = @("-G", "Ninja") }
Write-Host "Compiling (a few minutes the first time)"
emcmake cmake -S $Root -B $Build @Generator -DCMAKE_BUILD_TYPE=Release | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "cmake could not configure the build" }
cmake --build $Build
if ($LASTEXITCODE -ne 0) { Fail "the build failed" }

# Toy opens index.html, and the build folder also holds CMake's own files, so the page and the
# four files it loads go into dist\.
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item (Join-Path $Build "th07.html") (Join-Path $Out "index.html")
Copy-Item ("th07.js", "th07.wasm", "th07.data", "toy-bridge.js" | ForEach-Object { Join-Path $Build $_ }) $Out

$DataMB = [int]((Get-Item (Join-Path $Out "th07.data")).Length / 1MB)
Write-Host ""
Write-Host "Done: $Out (th07.data is $DataMB MB)" -ForegroundColor Green
Write-Host "Play it locally:  powershell -ExecutionPolicy Bypass -File serve-web.ps1   then open http://localhost:8123/"
Write-Host "To publish it, upload everything in dist\."
