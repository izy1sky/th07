# Pointdevice Mode (TH15-style checkpoints) for th07

This branch (`feat/pointdevice-mode`, based on `web`) adds a Pointdevice Mode
like the one in Touhou 15. When you get hit you go back to the last checkpoint
instead of losing a life, as many times as you need.

## How it plays

- **On by default.** On the title screen press **P** to turn it on or off.
  The current setting shows at the bottom left.
- **Checkpoints** are taken automatically:
  - at the start of every stage,
  - at the start of every boss attack (spell cards and non-spells),
  - every 15 seconds during the stage, but only at a calm moment (no bullets
    close to you, no lasers, no bomb or dialogue in progress).
  "Checkpoint" flashes at the bottom of the play area when one is taken.
- **When you get hit** the game freezes and asks:
  - **Continue from checkpoint**: everything (score, power, bombs, cherry,
    enemies, bullets, boss HP, music) goes back to the checkpoint, and you get
    2 seconds of invulnerability.
  - **Give up**: ends the run and shows the result screen.
- **Pause (ESC, or four fingers on touch)** opens the Pointdevice pause menu:
  - **Resume**
  - **Save and quit**: saves your difficulty, character and shot type, the
    stage you are on, and the score, power, bombs, cherry and retry count that
    stage started with, then returns to the title screen.
  - **Quit without saving**
- **Resuming:** the title screen shows `Saved run: Stage N ... (R)`. Press **R**
  to carry on from the start of that stage. A saved run is used up when you
  resume it; save and quit again to keep a new one.
- **Extends give a bomb** instead of a life.
- **Records stay clean:** Pointdevice runs never save a replay, never write
  `score.dat`, and never go on the Toy leaderboard. The Toy result panel still
  shows the run (marked "Pointdevice") and adds it to your history (marked
  "PD"), but the upload button is disabled.

### Menu controls

| | Keyboard | Touch |
|---|---|---|
| Move cursor | Up / Down | Swipe up / down |
| Pick | Z or Enter | Tap |
| Close pause menu | ESC or X | Two-finger tap |
| Toggle mode (title) | P | not available |
| Resume saved run (title) | R | not available |

The menus ignore input for the first third of a second, so mashing Z through
a death never picks an option by accident.

## Where the saved run is stored

`pointdevice.sav`, next to `score.dat` in the game's pref path:

- Web: the IndexedDB mount `/savesth07` (survives page reloads; cleared if the
  player clears site data).
- Desktop builds with `-DTH_EXTERNAL_ASSETS=ON`: next to the executable.
- macOS / iOS / Android app bundles: the SDL pref path
  (`~/Library/Application Support/TeamShanghaiAlice/th07/` on macOS).
- Windows and Linux: the working directory.

Note for desktop testing: with no `th07.cfg` the game starts full screen. Run it
once, set "Window" in the in-game Option menu, or start from a `th07.cfg` with
`windowed = 1`.

## Building for the web (Bilibili Toy)

You need: emsdk (activated), CMake 3.16+, Ninja (optional), Python 3, and
ffmpeg (for the BGM conversion). From a fresh clone:

```bash
git clone -b feat/pointdevice-mode https://github.com/izy1sky/th07.git
cd th07
git submodule update --init --depth 1
# SDL_ttf builds FreeType and PlutoSVG from its own submodules:
git -C vendored/SDL_ttf submodule update --init --depth 1 \
    external/freetype external/harfbuzz external/plutosvg external/plutovg
```

Put the game files in `assets/` (git ignores this folder):

```
assets/th07.dat
assets/thbgm.dat
assets/msgothic.ttc
```

Convert the BGM to MP3 so the package fits Toy's 140 MB limit (this replaces
`assets/thbgm.dat` with a stub, so keep your original copy elsewhere):

```bash
python3 tools/convert_bgm_web.py --bitrate 192k
```

Build and serve:

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
./serve-web.sh            # Windows: .\serve-web.ps1
```

Open `http://localhost:8123/th07.html`. For Toy, upload the contents of
`build-web/` the same way as before (`th07.html`, `th07.js`, `th07.wasm`,
`th07.data`, `toy-bridge.js`).

## Building for desktop (quickest way to test)

```bash
cmake -S . -B build -DTH_EXTERNAL_ASSETS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then put `th07.dat`, `thbgm.dat` (the original, not the stub) and
`msgothic.ttc` next to the built `th07` executable and run it from that folder.
Uses the system SDL3 if found (`brew install sdl3 sdl3_image sdl3_ttf`),
otherwise the vendored copies.

## Test helpers (desktop only)

- `TH07_POINTDEVICE=0` or `--legacy` starts with the mode off; `--pointdevice` forces it on.
- `TH07_POINTDEVICE_LOG=1` prints checkpoint, death, rewind, save and resume events to stderr.
- `TH07_POINTDEVICE_AUTOTEST=1` starts an autopilot that clicks through the menus,
  plays Stage 1 and picks Continue after every death. `=giveup` gives up at the
  first death; `=savequit` plays on (always invulnerable), saves and quits early
  in Stage 2, then resumes the saved run from the title screen; `=hold` leaves
  the first menu open so you can look at it.
