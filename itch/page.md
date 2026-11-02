# XAHBall - itch.io page kit

Everything needed to fill in itch.io's "Create a new project" form. Images
are in this folder; the downloads are built by `tools/dist.sh --no-upload`
(`dist/xahball.lha`, `dist/xahball.zip`).

## Form fields

| Field | Value |
|---|---|
| Title | XAHBall |
| Project URL | `https://<your-itch-name>.itch.io/xahball` |
| Short description / tagline | HaxBall-style top-down football for the Amiga 500 - up to 6 players on one machine |
| Classification | Games |
| Kind of project | Downloadable |
| Release status | Released (or "In development" if you plan more updates soon) |
| Pricing | No payments, or $0 / donate - "Name your own price" with $0 minimum |
| Uploads | `xahball.lha` (Amiga-native archive) and `xahball.zip` - leave the Windows/macOS/Linux/Android boxes unticked, these are Amiga files. Optional: add the version (0.4) in the upload's "Version" field |
| Description | see `description.html` (open it in a browser, select all, copy, paste into the itch editor) |
| Genre | Sports |
| Tags (max 10) | `amiga`, `amiga-500`, `retro`, `football`, `soccer`, `local-multiplayer`, `top-down`, `pixel-art`, `haxball`, `2d` (itch suggests more as you type) |
| AI generation disclosure | Answer to match how the art, code and music were made (itch asks this on every page) |
| App store links | none |
| Custom noun | (leave empty, or "game") |
| Community | Comments |
| Visibility | Draft first - check the preview - then Public |

### Metadata (the "More details" section)

| Field | Value |
|---|---|
| Platforms | (none of the presets - mention Amiga in the description) |
| Made with | ACE (Amiga C Engine) - type it as a custom "made with" tool, or leave empty |
| Inputs | Keyboard, Joystick |
| Multiplayer | Local multiplayer; Player count: 1-6 |
| Average session | A few minutes |
| Languages | English |
| Accessibility | (none) |
| Links | Source code: https://github.com/mirq/xahball-amiga |

## Media

| itch slot | File |
|---|---|
| Cover image (630x500) | `cover.png` |
| Screenshots (shown in this order) | `gameplay.gif`, then `screenshots/01-title.png` ... `06-custom-controls.png` |
| Gameplay video | itch only embeds YouTube/Vimeo links - upload `gameplay.mp4` there if you want a video; the GIF already shows play in the screenshot strip |
| Theme banner (Edit theme -> Banner) | `banner.png` (960x240) |

Suggested theme colors (Edit theme) to match the art: background `#112255`
(night blue), text `#ffffff`, links `#ffaa00` (amber), buttons `#ffaa00`.

All screenshots are pixel-exact captures of the real game in FS-UAE
(A500, 512K chip + 512K slow), scaled 3x with nearest neighbour. The two
match shots come from the demo mode with its "DEMO - PRESS FIRE" label
blanked (that strip is empty in a normal match). The GIF runs at real game
speed.

## Description (source of description.html)

XAHBall is HaxBall-style football for the classic Amiga: one screen, a
round-cornered pitch, discs for players, a ball that pings off the posts,
and up to six people crowded around one A500.

Move, hold fire, kick. Shots are one kick per press, the ball keeps its
momentum, and a post can save you or ruin you.

Features:
- 1 vs 1, 2 vs 2 and 3 vs 3 - everyone on one Amiga
- Ready-made controller setups: joystick in port 2 vs CPU, two joysticks,
  joystick vs keyboard, up to four joysticks (with a parallel-port 4-player
  adapter) plus two players on the keyboard - or set every player up yourself
- CPU players fill every empty slot, NORMAL or HARD (the hard ones read
  where the ball is going and put a keeper in goal)
- 12 national teams with flags and kit colors, away kits when colors clash
- Match time 1-5 minutes, the clock only runs while the ball is in play
- Optional rules: kickoff circle lock, ball freeze on goals
- Demo mode - leave it alone and the computer plays a match
- 50 fps on a stock Amiga 500

Controls:
- Joystick: move, fire to kick (hold to charge, one kick per press)
- Keyboard player 1: arrows + RIGHT SHIFT
- Keyboard player 2: W S A D + LEFT SHIFT
- Menus: joystick in port 2, or arrows/WSAD + RETURN; ESC goes back.
  ESC pauses a match, ESC again quits.

Requirements:
- Amiga 500/500+/600/1200 or an emulator (FS-UAE, WinUAE, Amiberry)
- 68000, 1 MB RAM (512K chip + 512K slow or fast), Kickstart 1.3 or newer,
  PAL
- Hard drive or a folder (no floppy image yet)
- Optional: parallel-port 4-player adapter for joysticks in ports 3 and 4

Install: unpack `xahball.lha` anywhere on your hard drive, open the
`xahball` drawer and run `xahball.exe` from a shell inside that drawer (it
loads its data from `data/` next to it). If the executable lost its "e"
flag on the way: `protect xahball.exe e add`.

Credits:
- Game by mirq
- Built with ACE (Amiga C Engine)
- Menu music: "triangles" by X-Type
- Silkscreen font from the ACE showcase assets
- Intro technique and "Powered by ACE" logo after AMIner by
  Last-Minute-Creations
- Thanks to Tehkain and all ACE developers, and to everyone who tested
