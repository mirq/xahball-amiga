# XAHBall — a HaxBall clone for Amiga 500

Top-down 2D football for the classic Amiga, built with
[ACE (Amiga C Engine)](https://github.com/AmigaPorts/ACE) by mirq.

![screen](https://img.shields.io/badge/target-Amiga%20500%20(68000%2C%201MB)-orange)

## Features

- 1vs1 / 2vs2 / 3vs3 matches, selectable match time (1/2/3/5 min)
- **Simple controller presets** per team size (joysticks named by the Amiga
  port they're plugged into - port 2 first, the mouse usually lives in
  port 1):
  - 1vs1: `1P VS CPU` (port 2), `JOY VS JOY` (port 2 vs port 1),
    `JOY VS KEYS` (port 2 vs WSAD)
  - 2vs2: `1P VS CPU`, `2 JOY 2 KEYS` (port 2 + WSAD vs port 1 + arrows),
    `4 JOYSTICKS` (ports 2 + 4 vs ports 1 + 3)
  - 3vs3: `1P VS CPU`, `4 JOY 2 KEYS` (ports 2 + 4 + WSAD vs 1 + 3 + arrows)
- **Custom controller setup** (fire on CONTROLS): any device per player slot
  - joysticks in ports 1-4 (3/4 via parallel adapter), arrows, WSAD or CPU;
  duplicates are flagged and fall back to CPU
- CPU level NORMAL / HARD; unused slots are filled with AI bots
- 12 national teams: Poland, Germany, England, Italy, Spain, Netherlands,
  Sweden, Czech Republic, Denmark, Finland, USA, Norway — with runtime-drawn
  flags, automatic away-kit when two teams' colors clash, and **a match ball
  in each country's flag colors** (the conceding side kicks off with its own
  ball)
- HaxBall geometry dressed as a football pitch: mowed stripes, rounded
  corners, center circle, penalty/goal areas with spots and arcs, goal nets,
  red goal frames and posts you can actually ping shots off
- Physics on the amiga-shooter flat-world engine (tVector/vecmath/tFlatBody/
  tFlatWorld, fixed-point 16.16, 50 fps PAL) with full collision response:
  mass-based disc collisions (player 2, ball 1), restitution per body,
  static post bodies, elastic wall/corner/net bounces, one-kick-per-press
  with kick charge, recoil and a white-outline "kick armed" indicator
- Classic Telstar match ball (white with black patches)
- Role-based AI (attacker/defender with hysteresis, goal-corner aiming,
  ball interception, no own-goal clearances); HARD adds longer prediction,
  quicker kicks and a goalkeeper
- GOAL! banner with ball roll-in, kickoff countdown with kickoff-lock rule
  (clock starts at first touch), match timer, pause (ESC), winner screen,
  dedicated banner strip for phase texts (no overlay flicker)
- Intro: "Powered by ACE" logo splash, then the 32-color title artwork as
  the welcome screen; menus sit on a stadium backdrop made from the same art
- Demo (attract) mode: idle on the welcome screen or the menu and an all-AI
  match starts; any key or fire returns to the menu
- Sample-based sound effects (kick, bounce, whistle, goal crowd) and menu
  music ("triangles" by X-Type) via ptplayer

## Controls

| Action | KB1 | KB2 | Joystick |
|--------|-----|-----|----------|
| Move   | arrows | W/A/S/D | stick |
| Kick   | Right Shift / Right Amiga | X / Left Alt | fire |
| Menu   | arrows/WSAD + Return/Space/X, ESC back | same | joystick in port 2 |
| Pause  | ESC or P | | |

## Build (Linux host, Bartman GCC)

See `ai-agent-instructions.md` for the full story. Short version:

```sh
# host asset tools (once)
cmake -S /path/to/ACE/tools -B /path/to/ACE/tools/build -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/ACE/tools/build -j 8

# game
cmake -B game-build -S game \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/AmigaCMakeCrossToolchains/m68k-bartman.cmake \
  -DVSCODE_AMIGA_EXTENSION_PATH=/path/to/bartmanabyss.amiga-debug-1.8.2 \
  -DM68K_CPU=68000
cmake --build game-build -j 8
```

Output: `game-build/dist/` containing `xahball.exe` + `data/`. Copy both to a
mount point (floppy via xdftool, HD folder, etc.) and run the exe. Minimum
target: A500 68000, 512 KB chip + 512 KB slow RAM.

## Regenerating assets & testing

- `python3 tools/make_title.py <master.png>` — title artwork -> 32-color
  OCS-exact `res/logo/title.png/.gpl`
- `python3 tools/make_menu_bg.py <master.png>` — menu stadium backdrop in
  the 16-color menu palette (keep its table in sync with `g_pMenuPalette`)
- `python3 tools/snap_ocs_palette.py img.png pal.gpl` — snaps any indexed
  asset to exact 12-bit OCS colors (ACE's palette_conv rounds up otherwise)
- `python3 tools/make_sfx.py` — synthesizes the sound effect WAVs
- `python3 tools/make_sprites.py` — draws the team/ball sprite PNGs + palette
  (palette must stay in sync with `matchPaletteLoad` in `game.c`)
- `tools/host_sim.c` — host-side physics/AI simulation: builds the real
  `physics.c`/`ai.c` natively and fuzz-tests containment, corner/post bounces,
  goal detection, kicks and full AI-vs-AI matches (see file header for the
  build line). Run after every physics/AI change.
- `tools/emu.sh deploy|start|keys|shot|stop` — runs the game in FS-UAE
  inside a nested Xephyr display (keys/screenshots never touch the desktop)
- `tools/xjoy.py` / `tools/xkey.py` / `tools/xshot.py` — FS-UAE test drivers
  (virtual joystick / keyboard injection / screenshots); `ai-agent-instructions.md`
  §13 explains the test rig.
- Everything else (pitch, flags, fonts) is generated at runtime or converted
  by the CMake asset rules.

## Credits

- **ACE Amiga C Engine**
- Silkscreen font — from the ACE showcase assets
- Intro technique and "Powered by ACE" logo assets after *AMIner* by
  Last-Minute-Creations (MPL-2.0)
- Menu music: "triangles" by X-Type
- Sound effects synthesized in-house, public domain
