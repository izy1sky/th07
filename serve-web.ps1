param(
    [int]$Port = 8123
)

# Serves the Emscripten web build over HTTP. Browsers refuse to load
# th07.js/th07.wasm/th07.data from file://, so an HTTP server is required.

$dir = Join-Path $PSScriptRoot "build-web"
if (-not (Test-Path (Join-Path $dir "th07.html"))) {
    Write-Error "build-web/th07.html not found. Build the web port first (see README)."
    exit 1
}

Write-Host "Serving $dir"
Write-Host "Open http://localhost:$Port/th07.html in your browser."
python -m http.server $Port --directory $dir
