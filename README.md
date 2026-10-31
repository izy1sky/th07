# th07 with Pointdevice Mode

A playable, portable build of Touhou 7 (東方妖々夢 ～ Perfect Cherry Blossom) for Windows, macOS,
Linux, mobile and the browser, with a TH15-style **Pointdevice Mode** and a web build made for
[Bilibili Toy](toy/README.md).

This repository does not include any game data. You need `th07.dat` and `thbgm.dat` from your own
copy of the game, plus `msgothic.ttc` for text.

## What this repository adds

- **Pointdevice Mode**: when you get hit you go back to the last checkpoint instead of losing a
  life. Checkpoints are taken at the start of each stage, at the start of every boss and midboss
  attack, and every 15 seconds at a calm moment; the music rewinds with the game. You pick
  Pointdevice or Legacy after choosing your shot type, can give up or continue after each hit, and
  can save a run from the pause menu and resume it later. See [POINTDEVICE.md](POINTDEVICE.md).
- **Bilibili Toy web build**: background music streamed as MP3 (about 60 MB instead of the 444 MB
  `thbgm.dat`), and a result-screen panel with score upload, leaderboard, run history and a score
  image. Pointdevice runs are kept off the leaderboard. See [toy/README.md](toy/README.md).
- **Lower input latency in the browser**: key presses between two 60 Hz ticks are no longer lost,
  game keys can't scroll the page, and the FPS counter shows the logic rate.
- **F9 practice panel** (web build): jump to any stage, wave, frame, midboss, boss or spell card,
  set score, lives, bombs and power, or go straight to the result screen. Also available from the
  browser console as `window.__th07Debug`.
- **Windows desktop builds with MSVC** (Visual Studio), and fixes for a crash and a double free
  when `score.dat` is missing or too short.

Release notes are in [CHANGELOG.md](CHANGELOG.md).

## Building

You need CMake, a C++17 compiler and the game files above.

**Desktop:**

```bash
cmake -S . -B build -DTH_EXTERNAL_ASSETS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Put `th07.dat`, `thbgm.dat` and `msgothic.ttc` next to the built executable and run it from that
folder. SDL3, SDL3_image and SDL3_ttf are used from the system if all three are installed,
otherwise from `vendored/` (clone with `--recursive`).

**Web:** build with Emscripten, convert the music with `tools/convert_bgm_web.py`, and serve the
output over HTTP with `serve-web.sh` or `serve-web.ps1`; opening `th07.html` directly doesn't
work. Step-by-step instructions for macOS, Linux and Windows are in
[POINTDEVICE.md](POINTDEVICE.md#building-for-the-web-bilibili-toy).

Touch controls work on any touch screen: drag to move, hold a second finger to focus, tap the side
bars to bomb, and put four fingers on the play area to pause.

## Upstream

This repository was forked from [some100/th07](https://github.com/some100/th07), the Touhou 7
decompilation, and is built on its
[`reallyportable` branch](https://github.com/some100/th07/tree/reallyportable), which ports the
decompiled game to SDL3 and OpenGL ES with web, mobile and high-refresh-rate support. It is no
longer linked to that repository on GitHub and doesn't send changes back to it. The game code and
the port itself come from that project, which in turn builds on the
[th06](https://github.com/GensokyoClub/th06) and [th08](https://github.com/GensokyoClub/th08)
decompilations. Fixes from `reallyportable` are merged in from time to time.

Like upstream, this repository is released under CC0 1.0 (see [LICENSE](LICENSE)).
