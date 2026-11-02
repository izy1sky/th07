#!/bin/sh
# Serves the web build in dist/ over HTTP (browsers block file:// loads). Build it with build-web.sh.
cd "$(dirname "$0")/dist" || { echo "dist/ not found. Build the web version first: ./build-web.sh <game folder>" >&2; exit 1; }
echo "Open http://localhost:${1:-8123}/ in your browser."
exec python3 -m http.server "${1:-8123}"
