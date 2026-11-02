param(
    [int]$Port = 8123
)

# Serves the web build in dist\ over HTTP. Browsers refuse to load
# th07.js/th07.wasm/th07.data from file://, so an HTTP server is required.

$dir = Join-Path $PSScriptRoot "dist"
if (-not (Test-Path (Join-Path $dir "index.html"))) {
    Write-Error "dist\index.html not found. Build the web version first: build-web.ps1 -Game <game folder>"
    exit 1
}

$Python = (Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
Write-Host "Serving $dir"
Write-Host "Open http://localhost:$Port/ in your browser."
& $Python -m http.server $Port --directory $dir
