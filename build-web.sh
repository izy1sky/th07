#!/usr/bin/env bash
# Builds the browser version into dist/, ready to serve or upload to Bilibili Toy.
#
#   ./build-web.sh <game folder>
#
# <game folder> holds th07.dat, thbgm.dat and msgothic.ttc; they are only read. Needs emsdk
# (in ~/emsdk or $EMSDK, or already activated), CMake, python3 and ffmpeg (for the music,
# converted once into build/web-bgm/). Ninja is used if installed. Windows: build-web.ps1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSETS="$ROOT/assets"
BUILD="$ROOT/build-web"
OUT="$ROOT/dist"
CACHE="$ROOT/build/web-bgm"

die() { echo "error: $*" >&2; exit 1; }

[ $# -eq 1 ] || die "usage: ./build-web.sh <game folder>   (the folder with th07.dat, thbgm.dat and msgothic.ttc)"
GAME="$(cd "$1" 2>/dev/null && pwd)" || die "$1 is not a folder"
[ "$GAME" != "$ASSETS" ] || die "pass the folder with your original game files, not assets/ (this script fills assets/)"

# emsdk installed in $EMSDK or ~/emsdk but not activated in this terminal: activate it here.
if ! command -v emcmake >/dev/null && [ -f "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" ]; then
  set +u
  EMSDK_QUIET=1 source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null
  set -u
fi

# Report everything that is missing at once rather than one thing per run.
problems=()
command -v emcmake >/dev/null ||
  problems+=("emsdk not found: install it in ~/emsdk (https://emscripten.org/docs/getting_started/downloads.html), or source <emsdk>/emsdk_env.sh first")
missing_tools=()
for tool in cmake python3 ffmpeg; do
  command -v "$tool" >/dev/null || missing_tools+=("$tool")
done
[ ${#missing_tools[@]} -eq 0 ] ||
  problems+=("${missing_tools[*]} not found: brew install ${missing_tools[*]} ninja   (Linux: your package manager)")
missing_files=()
for f in th07.dat thbgm.dat msgothic.ttc; do
  [ -f "$GAME/$f" ] || missing_files+=("$f")
done
[ ${#missing_files[@]} -eq 0 ] ||
  problems+=("$GAME has no ${missing_files[*]}: pass the folder with your game files (th07.dat, thbgm.dat, msgothic.ttc)")
if [ ${#problems[@]} -gt 0 ]; then
  for p in "${problems[@]}"; do echo "error: $p" >&2; done
  exit 1
fi

# The music conversion leaves a 16-byte thbgm.dat in assets/; a big one there is someone's
# original, which the copy below would delete.
if [ -f "$ASSETS/thbgm.dat" ] && [ "$(wc -c < "$ASSETS/thbgm.dat")" -gt 1024 ]; then
  die "assets/thbgm.dat is the original music file. Move it to your game folder and pass that folder"
fi

if [ ! -f "$ROOT/vendored/SDL/CMakeLists.txt" ] || [ ! -f "$ROOT/vendored/SDL_ttf/external/freetype/CMakeLists.txt" ]; then
  echo "Fetching the vendored SDL libraries"
  git -C "$ROOT" submodule update --init --depth 1
  git -C "$ROOT/vendored/SDL_ttf" submodule update --init --depth 1 \
    external/freetype external/harfbuzz external/plutosvg external/plutovg
fi

# The raw music (444 MB) is too big to ship, so it becomes one MP3 per track. That takes a few
# minutes, so it is done once into build/web-bgm/ and redone only when the game files change.
if [ ! -f "$CACHE/thbgm.dat" ] || [ "$GAME/thbgm.dat" -nt "$CACHE/thbgm.dat" ] || [ "$GAME/th07.dat" -nt "$CACHE/thbgm.dat" ]; then
  echo "Converting the music to MP3 (a few minutes, done once)"
  rm -rf "$CACHE"
  mkdir -p "$CACHE"
  # The converter replaces thbgm.dat with a stub, so it works on copies.
  cp "$GAME/th07.dat" "$GAME/thbgm.dat" "$CACHE/"
  python3 "$ROOT/tools/convert_bgm_web.py" --assets "$CACHE" --bitrate 192k
  rm "$CACHE/th07.dat"
fi

# Everything in assets/ is packed into th07.data.
echo "Copying game files from $GAME"
rm -rf "$ASSETS"
mkdir -p "$ASSETS"
cp "$GAME/th07.dat" "$GAME/msgothic.ttc" "$CACHE/thbgm.dat" "$ASSETS/"
cp -R "$CACHE/bgm" "$ASSETS/"

GENERATOR=()
if [ ! -f "$BUILD/CMakeCache.txt" ] && command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi
echo "Compiling (a few minutes the first time)"
emcmake cmake -S "$ROOT" -B "$BUILD" ${GENERATOR[@]+"${GENERATOR[@]}"} -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD"

# Toy opens index.html, and the build folder also holds CMake's own files, so the page and the
# four files it loads go into dist/.
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BUILD/th07.html" "$OUT/index.html"
cp "$BUILD/th07.js" "$BUILD/th07.wasm" "$BUILD/th07.data" "$BUILD/toy-bridge.js" "$OUT/"

echo
echo "Done: $OUT (th07.data is $(( $(wc -c < "$OUT/th07.data") / 1048576 )) MB)"
echo "Play it locally:  ./serve-web.sh   then open http://localhost:8123/"
echo "To publish it, upload everything in dist/."
