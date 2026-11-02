# Pointdevice Mode (TH15-style checkpoints) for th07

This repository adds a Pointdevice Mode like the one in Touhou 15. When you get hit you go back to the last checkpoint
instead of losing a life, as many times as you need.

## How it plays

- **Choosing the mode:** after you pick difficulty, character and shot type,
  the game asks for **Pointdevice Mode** or **Legacy Mode** (the original
  rules), styled like the Choose Level screen (完全無欠 / 従来 on white wisps), with a short description of each. The hit and pause menus
  use the look of the game's own pause menu. Menus fade and slide in, and
  the selection cross-fades as you move the cursor. The art comes from
  `tools/pointdevice-art/` (see its README to change it). Pointdevice is preselected the
  first time; after that the game remembers your last choice.
- **Lives are not used:** in a Pointdevice run the side panel shows no lives,
  1UPs and extends give a bomb instead, and stage clears give no life bonus.
- **Checkpoints** are taken automatically:
  - at the start of every stage,
  - at the start of every boss and midboss attack (spell cards and non-spells),
  - every 15 seconds during the stage, but only at a calm moment (no bullets
    close to you, no lasers, no bomb or dialogue in progress).
  "Checkpoint" flashes at the bottom of the play area when one is taken.
- **When you get hit** the game freezes and asks:
  - **Continue from checkpoint**: everything (score, power, bombs, cherry,
    enemies, bullets, boss HP) goes back to the checkpoint, and you get exactly
    2 seconds of invulnerability. The music goes back to where it was
    at the checkpoint, too.
  - **Give up**: ends the run and shows the result screen.
- **Pause (ESC, or four fingers on touch)** opens the Pointdevice pause menu:
  - **Resume**
  - **Save and quit**: saves the stage you are on and the score, power,
    bombs, cherry and retry count that stage started with, then returns to the
    title screen. Not offered in practice runs.
  - **Quit without saving**
- **Resuming:** there is one saved run per difficulty, character and shot
  type. When you pick Pointdevice Mode for a combination that has one, the
  game shows its stage and score and asks **Resume this run** or **Start a
  new run**. Resuming starts at the beginning of the saved stage and uses the
  save up; starting a new run leaves it in place until you save and quit again.
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
| Back (mode choice) | X | Two-finger tap |

The menus ignore input for the first third of a second, so mashing Z through
a death never picks an option by accident.

## Where the saved run is stored

`pointdevice-<difficulty>-<character>-<shot>.sav` (one file per combination),
next to `score.dat` in the game's pref path:

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

You need emsdk, CMake, Python 3 and ffmpeg (for the music); Ninja is used if
installed. Keep your game files (`th07.dat`, `thbgm.dat`, `msgothic.ttc`) in a
folder of their own and pass that folder to the build script. It only reads
them, fetches the vendored SDL libraries the first time, converts the music to
MP3 once (into `build/web-bgm/`, so the package fits Toy's 140 MB limit) and
puts the finished page in `dist/`.

macOS / Linux:

```bash
git clone https://github.com/izy1sky/th07.git
cd th07
source ~/emsdk/emsdk_env.sh          # wherever emsdk is installed
./build-web.sh ~/games/th07
./serve-web.sh
```

Windows (PowerShell):

```powershell
git clone https://github.com/izy1sky/th07.git
cd th07
powershell -ExecutionPolicy Bypass -File build-web.ps1 -Game C:\games\th07 -Emsdk C:\emsdk
powershell -ExecutionPolicy Bypass -File serve-web.ps1
```

The tools can be installed with winget if you don't have them:
`winget install Kitware.CMake Ninja-build.Ninja Gyan.FFmpeg Python.Python.3.12`.

Open `http://localhost:8123/`. For Toy, upload everything in `dist/`
(`index.html`, `th07.js`, `th07.wasm`, `th07.data`, `toy-bridge.js`).

The page shows a loading screen while `th07.data` downloads, Shift+Enter
toggles fullscreen, and touch screens use the game's own touch controls.

## Building for desktop (quickest way to test)

```bash
cmake -S . -B build -DTH_EXTERNAL_ASSETS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then put `th07.dat`, `thbgm.dat` (the original, not the stub) and
`msgothic.ttc` next to the built `th07` executable and run it from that folder.
Uses the system SDL3 if found (`brew install sdl3 sdl3_image sdl3_ttf`),
otherwise the vendored copies.

On Windows, `build-windows.ps1` does all of this with Visual Studio 2022 or 2026
("Desktop development with C++") and copies `th07.exe` and the SDL DLLs into
your game folder:

```powershell
powershell -ExecutionPolicy Bypass -File build-windows.ps1 -Game C:\games\th07
```

## Test helpers (desktop only)

- `TH07_POINTDEVICE=0` or `--legacy` preselects Legacy Mode; `--pointdevice` preselects Pointdevice.
- `TH07_POINTDEVICE_LOG=1` prints checkpoint, death, rewind, save and resume events to stderr.
- `TH07_POINTDEVICE_AUTOTEST=1` starts an autopilot that clicks through the menus,
  plays Stage 1 and picks Continue after every death. `=giveup` gives up at the
  first death; `=savequit` plays on (always invulnerable), saves and quits early
  in Stage 2, then resumes the saved run; `=hold` leaves
  the first menu open so you can look at it.
