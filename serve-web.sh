#!/bin/sh
# Serves the Emscripten web build over HTTP (browsers block file:// loads).
cd "$(dirname "$0")/build-web" || exit 1
exec python3 -m http.server "${1:-8123}"
