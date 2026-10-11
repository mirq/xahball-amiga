# AI Agent Instructions — ACE (Amiga C Engine) Programming

Hard-won findings and tricks from building **XAHBall** (HaxBall clone) with ACE
on an Amiga 500 target. Read this before touching the code.

---

## 1. Project layout & build

```
amiga-xahball/
├── AmigaCMakeCrossToolchains/   # cmake toolchain files (m68k-bartman.cmake)
├── game/
│   ├── CMakeLists.txt           # project + asset conversion rules
│   ├── deps/ace -> /home/mirek/ace/ACE   # symlink to the engine
│   ├── res/                     # source assets (png, gpl, sfx wav, font glyphs)
│   └── src/                     # game source (flat, no subdirs)
├── tools/                       # host-side helpers (python)
└── game-build/                  # cmake build dir (out of repo)
```

Build (Bartman GCC from the VSCode amiga-debug extension):

```sh
# 1. host asset tools (once, after ACE update)
cmake -S /home/mirek/ace/ACE/tools -B /home/mirek/ace/ACE/tools/build -DCMAKE_BUILD_TYPE=Release
cmake --build /home/mirek/ace/ACE/tools/build -j 8     # -> ACE/tools/bin/*

# 2. game
cmake -B game-build -S game \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/AmigaCMakeCrossToolchains/m68k-bartman.cmake \
  -DVSCODE_AMIGA_EXTENSION_PATH=/home/mirek/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2 \
  -DM68K_CPU=68000
cmake --build game-build -j 8
# -> game-build/xahball.exe + game-build/data/
```

- Keep the toolchain file path **absolute** (relative path broke configuration).
- Asset converters are found via `ACE_DIR/tools/bin` — `add_subdirectory(deps/ace)`
  sets `ACE_DIR`, so the symlink layout matters.
- Deploy = copy `xahball.exe` + `data/` into `/home/mirek/Amiberry/share/Dev/…`
  (appears as `Dev:` inside FS-UAE). Remember `chmod +x` for files the Amiga
  must execute (fs-uae maps the unix x-bit to the Amiga "e" protection bit).
- ACE is fast-moving: `git -C /home/mirek/ace/ACE pull --ff-only` before starting.

## 2. Display & double buffering — THE most important rules

Standard setup (320×256 lores, 4bpp, 16 colors):

```c
s_pView = viewCreate(0, TAG_VIEW_GLOBAL_PALETTE, 1, TAG_DONE);
s_pVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 4,
    TAG_VPORT_HIRES, 1,          // 640px wide; OMIT for 320 lores
    TAG_DONE);
s_pBfr = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED, // interleaved REQUIRED for BOBs
    TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
    TAG_DONE);
```

**Frame-end sequence (required in EVERY state loop, every frame):**

```c
viewProcessManagers(s_pView);   // swaps pBack/pFront + rewrites copper block
copProcessBlocks();             // applies copper block updates
vPortWaitForEnd(s_pVPort);      // waits for frame end
```

Without `viewProcessManagers` + `copProcessBlocks` the buffers **never actually
swap** — the game silently runs single-buffered even with `IS_DBLBUF,1`, and
the beam catches every draw → flicker/blink.

**Never draw on the displayed page.** After the swap, `pBfr->pBack` is the only
safe draw target; `pBfr->pFront` is on screen. Drawing on `pFront` makes the
beam catch partial erases/draws → blinking sprites.

**Content lifecycle rules:**
- Static content (backgrounds): draw **once on both pages** at state create.
- Dynamic content (sprites, timers): redraw **every frame on pBack only**.
  Anything redrawn only "when it changes" will blink at 25 Hz once double
  buffering actually works.
- Per-page bookkeeping for manually animated objects: each page must erase
  **its own** last drawn position (the previous frame's erase hit the other
  page). Track last position per page, keyed by comparing `pBack` against the
  two known page pointers. (The BOB system does this for you — use it.)
- Overlay texts (countdown/GOAL!/pause) drawn right **after
  `vPortWaitForEnd`** return on both pages: you are in/near vblank, both pages
  stay consistent, nothing flickers. Restore the area underneath from a kept
  copy of the background (`blitCopyAligned` from a full-screen bg bitmap).

## 3. BOB system (blitter objects)

- Requires **interleaved** display buffers AND interleaved frame/mask data.
- With double buffering pass both: `bobManagerCreate(pFront, pBack, height)`.
- Sync with the display swap each frame: `bobSetCurrentBuffer(s_pBfr->pBack)`.
- Frame: `bobBegin(pBack)` → set `sPos` / `bobSetFrame` → `bobPush` per bob →
  `bobEnd()`. No other blitter use between `bobBegin` and `bobEnd`.
- Masks: the blit runs `height × bpp` lines, so the mask must repeat the
  silhouette **once per bitplane** (4bpp → 4 identical planes), interleaved.
  `bitmap_conv -mc` produces exactly this when the palette is 4bpp.
- Frame/mask addresses: `bobCalcFrameAddress(pBitMap, 0)`.

## 4. Zero-width blits corrupt memory (crash-class bug)

- `blitRect(bm, x, y, 0, h, color)` writes a **huge row across memory**
  (blit size 0 wraps to 1024 words). Guard every computed-width rect:
  `if(wHalf < 1) continue;`
- Same for fonts: `fontDrawStr(..., "")` = zero-width text blit = corruption.
  Guard: `if(szText[0]) fontDrawStr(...)`.
- Symptom seen: random horizontal streaks of palette colors across the screen.

## 5. Fixed point (68000 — no floats, ever)

- Use bundled libfixmath: `fix16_t`, `F16(1.5f)` **only for compile-time
  constants**. `F16(variable)` expands to runtime float math → linker errors
  (`__fixdfsi` etc.) → use `fix16_from_int()` / `fix16_div` instead.
- **All fix16 arithmetic goes through fix16_add/sub/mul/div** — never raw C
  operators on fix16_t values. Raw `*`/`/` are outright wrong (no `>>16`/`<<16`
  scaling); raw `+`/`-` happen to be value-identical but skip the library's
  overflow guard. Comparisons (`<`, `>`, `==`) and representation tricks
  (delta `>> 13`, rounding bias `+ 0x8000 >> 16`) are fine.
- fix16 range is ±32768. **Never multiply two large coordinates** —
  `fix16_mul` returns `fix16_overflow` (INT_MIN!) when the product exceeds
  fix16 range, it does NOT wrap (`fix16_mul(276<<16, 276<<16)` = negative
  garbage). This includes squaring position deltas in vecLength — that's why
  `vecmath.c` computes lengths on 1/8px integers (delta >> 13). Dots on unit
  vectors and velocities (< 8 px/f) are safe. For collision distances convert
  to integer pixels first and use an integer sqrt:

```c
static UWORD uwIntSqrt(ULONG ulVal) { /* bit-by-bit sqrt, no FPU */ }
```

- Speed clamping: `fwSpeed = fix16_sqrt(fix16_mul(vx,vx)+fix16_mul(vy,vy))`
  is fine for speeds ≤ ~7.0 (result stays in range).

## 6. Sprites as assets (bitmap_conv) — preferred over runtime generation

Pipeline (see `tools/make_sprites.py` + CMakeLists):

```cmake
convertPalette(${GAME_LINKED} ${RES_DIR}/game.gpl ${DATA_DIR}/game.plt)
convertBitmaps(TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/game.gpl
    MASK_COLOR "#ff00ff" INTERLEAVED
    SOURCES ${RES_DIR}/sprites/teamA.png ... DESTINATIONS ${DATA_DIR}/teamA.bm ...
    MASKS ${DATA_DIR}/teamA.msk ...)
```

- PNG: indexed color, **width must be divisible by 16** for interleaved mode.
- Mask color `#ff00ff` must be **outside the .gpl palette**; transparent pixels
  must be exactly that color.
- **CMake gotcha:** `MASK_COLOR "#ff00ff"` needs quotes — unquoted `#` starts a
  comment and the arg silently vanishes. `convertPalette` takes **positional**
  args (`convertPalette(TARGET in.gpl out.plt)`), `convertBitmaps` keyword args.
- Load with `bitmapCreateFromPath("data/x.bm", 0)`; isFast must be 0 (chip).
- One sprite serves all teams via **palette swap**: draw the disc with reserved
  indices (4/5 for team A, 6/7 for team B), then at match start write kit RGBs:

```c
s_pVPort->pPalette[COLOR_KIT_A] = kitRGB;   // before viewLoad, or via copper
```

## 7. Palette

- 16 colors (4bpp) total for everything: pitch, UI, sprites. Budget carefully.
- **Per-state palettes**: menus/team-select/winner use `gamePaletteLoad` (the
  original 16 flag/UI colors, `COLOR_*` indices); the match screen uses
  `matchPaletteLoad` (`MCOLOR_*` indices, game.h) with dedicated dynamic slots
  for kit fills and the ball flag bands. Never mix the two index sets.
- **bitmap_conv maps sprite pixels to palette entries by COLOR VALUE** — a
  dynamic slot that shares its RGB with a static slot (black, white, red…) is
  unreachable from sprites: the converter collapses it onto the first match.
  Every dynamic slot therefore carries a *unique placeholder RGB* in the
  palette table (e.g. kitA `0xE10`, bands `0xB8C/0x7A4/0x4A7`); the real
  colors are written at runtime via `matchPaletteWrite()` (game.c pattern in
  match.c) which sets both `pVPort->pPalette` (for viewLoad) and
  `g_pCustom->color[n]` (for mid-match changes — viewLoad only programs the
  hardware once at state entry).
- Palette changes must happen **before `viewLoad`** (or via copper mid-frame).
- `tools/make_sprites.py` must stay in sync with `matchPaletteLoad` — it draws
  with `rgb12()` tuples, never raw PNG indices (the PNG palette has the mask
  color at index 0, which shifts everything by one).

## 8. Fonts & text

- silkscreen 8px font ships in ACE showcase (`res/silkscreen`); convert with
  `convertFont`. Scratch: `fontCreateTextBitMap(320, fontHeight + 8)` covers
  cookie+shadow; `fontDrawStr(..., FONT_COOKIE|FONT_HCENTER|FONT_SHADOW, ...)`.
- Erase-before-redraw for changing numbers (score/timer): blit a bg rect over
  the **text area only** — redrawing a full HUD strip every frame costs ~50 KB
  of blitter traffic (~10 ms on 68000). Only the digits change → erase small
  rects.
- Static HUD parts (panel, team codes): draw once on both pages at create.

## 9. Input

- `keyProcess()`/`joyProcess()` once per frame (genericProcess does it), then
  `keyUse()`/`joyUse()` are one-shots, `keyCheck()`/`joyCheck()` are held.
- Opposite-direction rule (input_slots.c): while both ends of an axis read
  held - impossible legitimately (one finger per axis / one stick gimbal) -
  the most recently pressed end wins and the other is suppressed; releasing
  the winner RESUMES a still-held opposite (hold D, tap A, release A ->
  back to moving right). Also covers stuck input: a key whose release was
  lost keeps the player drifting, but pressing the opposite moves the player
  correctly while held (instead of the two canceling). NEVER clear the key
  manager state for this - it loses legitimately held keys until re-pressed
  (the "D then A, release A, no right" regression). Unified for KB + JOY.
- F1 in-match toggles an input-state overlay (real-HW debugging, no bridge):
  row 1 = raw key/joy states (lit = held), row 2 = per-slot polled dx/dy/kick.
  Lit letter with control physically released = state layer stuck; nothing
  lit but a player drifts = game-logic bug.
- `joyEnableParallel()` enables JOY3/JOY4 on the parallel port (returns 0 if
  absent — ignore).
- Keyboard player 1: arrows + Right Shift (kick). Player 2: WSAD + Left Shift
  (kick). Menu navigation accepts BOTH keyboard sets + JOY1.
- Controller setup screen: NO uniqueness check while cycling a slot's device
  (duplicates display freely); both exit paths (fire/ESC) run
  `setupResolveDuplicates()` — each non-AI device stays with its first slot in
  slot order, later duplicates revert to AI.
- **FS-UAE quirk:** arrow keys are swallowed by the emulator (bound to input
  ports) — arrows never reach the game as rawkeys. WSAD/letters/Return work.
  For on-emulator testing assign KB2, not KB1. On real hardware arrows are fine.

## 10. Sound (ptplayer, sfx + menu music)

```c
ptplayerCreate(systemIsPal());                       // CIA interrupt
tPtplayerSfx *p = ptplayerSfxCreateFromPath("data/sfx/kick.sfx", 0); // 0 = CHIP (required)
ptplayerSfxPlay(p, PTPLAYER_SFX_CHANNEL_ANY, vol, 1);
ptplayerProcess();                                    // each frame
```

- Samples: 8-bit mono WAV → `audio_conv in.wav -o out.sfx -fpt -n` (CMake
  `convertAudio(... PTPLAYER NORMALIZE)`), keep total < 100 KB for 1 MB A500.
- **audio_conv bug (fixed locally):** `-fpt` alone crashed (SIGFPE, `% 0` in
  padContents because `-fpt` set `oForcePad = min(x,2) = 0`). Patched to
  `std::max(...,2)` + divide-by-zero guard in `tools/src/common/sfx.cpp`.
- **make_sfx.py scaling bug (fixed):** the float layers were written as
  `int(s + 128)` without scaling to the 0..255 range - every sfx was a +-1 LSB
  square wave which NORMALIZE amplified into a full-scale square buzz ("noisy
  samples"). Always peak-normalize to +-127 BEFORE quantizing in write_wav.
- ptplayer .sfx requirements (audio_conv -fpt handles them): first word must
  be zero (ptplayer parks the channel looping it after playback - otherwise
  the sample hums forever) and length must be even (word-based DMA).
- **Menu music (chaosArena pattern):** `tools/make_music.py` synthesizes a
  ProTracker M.K. mod (4 instruments, 8-bar Am-F-C-G loop, ~19 KB); CMake
  `mergeMods(... SAMPLE_PACK data/menu.samplepack)` runs mod_tool which
  strips instruments into one chip samplepack. Runtime: load
  `ptplayerModCreateFromPath` + `ptplayerSampleDataCreateFromPath` while OS
  is alive, then `ptplayerLoadMod(mod, pack, 0)` + `ptplayerEnableMusic(1)`;
  on state destroy `ptplayerStop()` FIRST, then free each pack sample's chip
  buffer + the pack + the mod. Pitched instruments render at 8363 Hz
  (= ptplayer ref period 424): period(note) = 424 * F0 / note_freq.

## 11. Performance on stock A500 (7 MHz, 1 MB)

- 320×256 lores ≫ 640×256 hires: 4× less blit memory per frame; hires is only
  worth it for static screens. At 640 the per-frame HUD erase alone (~50 KB)
  nearly eats the 20 ms frame.
- Static background drawn once (+ kept copy for restores), incremental sprites
  via BOB, minimal per-frame text. Then 50 fps is comfortable.
- blitCopyAligned: fastest path; it handles mixed interleaved/non-interleaved
  (falls back to per-plane with a warning).
- PAL frame = 20 ms; blitter bus ≈ 3.5 MB/s. A full-screen 4bpp fill ≈ 80 KB ≈
  23 ms — never do full-screen fills per frame.

## 12. State machine

- One `tState` per screen (create/loop/destroy) + `stateManagerCreate`;
  `stateChange()` from inside `cbLoop` is fine (return right after).
- In `cbDestroy`: `systemUse()` first (file/font ops need the OS), destroy
  view last. In `cbCreate`: build view → `viewLoad()` → `systemUnuse()`.
- The generic main comes from `#include <ace/generic/main.h>` with
  `#define GENERIC_MAIN_LOOP_CONDITION gameIsRunning() && pCurrent`.

## 13. Testing loop (no working bridge client needed)

The devbench bridge client (`a.exe`) is built `-m68020` with ~724 KB of BSS —
it cannot run on a 512K+512K A500 (Guru/err 103), so MCP-driven testing on the
A500 profile is unavailable. Working alternatives:

1. Own FS-UAE instance: `fs-uae tools/a500-test.fs-uae` (config = A500 Kick13
   with own DH0 whose startup-sequence runs the game directly; serial removed).
   `tools/a500-test-joy.fs-uae` binds port 1 to the uinput joystick instead.
2. **`tools/xjoy.py` — virtual joystick via uinput** (preferred): a kernel
   joystick device driven over a unix socket, needs no X focus at all, so it
   never disturbs the desktop user. Start `xjoy.py daemon` in the background,
   then `xjoy.py "down! down! fire!"` — same command syntax as xkey.
3. `tools/xkey.py` — XTEST keyboard injection; works only while the FS-UAE
   window has focus, and stray keys land in whatever IS focused (the desktop
   is shared — one stray "return" goes into the user's browser!). Verify what
   has focus before every send.
4. `tools/xshot.py <pid> <out.png>` — screenshots: briefly activates the
   FS-UAE window (EWMH), grabs the root screen, restores the previous focus.
   ~1s flicker per shot; keep the count low. Composite capture (xkey.py
   capture) fails on this desktop - no compositor backing for occluded windows.
5. **`tools/host_sim.c` — the game logic test rig (no emulator at all)**:
   compiles the real `flatworld.c`/`ai.c` + ACE fixmath natively and mirrors
   the match loop to fuzz containment, corner/post bounces, goal detection,
   kick semantics and full AI-vs-AI matches. Run after every physics/AI change.
6. Inject keys via XTEST (`tools/xkey.py`) — FS-UAE reads host keyboard.
7. The `amiga-a1200` MCP works for screenshots if its client is alive; the
   real Amiga bridge (port 3000) is independent.

Environment gotchas:
- Multiple agent sessions share the desktop: windows move/occlude, stray keys
  land in other apps. Prefer xjoy (uinput) over xkey (XTEST) whenever possible.
- fs-uae under CPU contention runs at 4–15 fps real time; countdowns stretch
  accordingly. `powersave` governor can't be changed without root.
- The devbench bridges log to one shared file — heartbeats seen there may come
  from the real Amiga, not the emulator you're poking.

## 14. Debug checklist (symptom → cause)

| Symptom | Cause |
|---|---|
| Blinking/flickering sprites | drawing on the displayed page, or missing per-frame redraw with dblbuf swap active |
| Screen never updates | missing `viewProcessManagers` + `copProcessBlocks` in the loop |
| Horizontal garbage streaks | zero-width blit (blitRect w=0 / empty fontDrawStr) — or a BOB at a negative x/y: **the BOB blitter does not clip**, `sPos.uwX/8` on a negative coordinate blits garbage into memory. Clamp every bob position to `[0, screen-size-16]` |
| Sprite fills with wrong color after palette work | bitmap_conv maps by color VALUE; a dynamic slot's placeholder RGB collided with a static slot (see §7) |
| Overlay text erases sprites / flickers | overlay fighting the BOB system — use a sprite-free banner strip (fixed screen band no bob can reach, redrawn on pBack each frame) instead of restore-from-bg overlays |
| HUD value blinks every other frame | per-page tracker keyed by a page test that's always false — `pBack == pFront` is NEVER true (they are two distinct buffers that swap roles). Key by the two physical bitmaps captured at create |
| Goal never triggers / ball escapes through a goal mouth | order bug: net/wall clamp must not run before the goal check; goal = ball *center* crossing the line inside the mouth, net physics only after the goal phase starts |
| Linker `__fixdfsi` undefined | `F16()` used on a runtime value — use `fix16_from_int()` (F16 is a double-based macro, constants only) |
| Speed clamp silently stops working | `fix16_mul(v,v)` overflows int32 above ~90 px/f — use per-axis clamps (`if(v > MAX) v = MAX`), no squaring |
| Guru at boot on A500 | bridge client built for 68020 (or too much BSS for 1 MB) |
| `Unable to load: err 103` | out of memory — binary+BSS too big for 512K+512K |
| Keys don't work in fs-uae | arrows are swallowed by fs-uae; use WSAD/Return, or inject a uinput joystick (`tools/xjoy.py`) |

## 15. Match physics architecture (flatworld.c / flatbody.c / vecmath.c)

- Entities are `tFlatBody` (fix16 pos/vel + integer radius); all collision
  math lives in flatworld.c, distances computed in **1/8 px units** (fix16 raw
  `>> 13`): small enough to square without overflow, fine enough for smooth
  normals (fix16 unit vectors, not 8-way integer snapping). Inverse mass is
  **precomputed in `flatBodyInit`** (`fwInvMass`) — the solver reads it per
  pair per frame, `fix16_div` there would be wasted 68000 cycles.
- Pitch walls are a rounded rect: 4 corner arcs are inside-out circle
  constraints (`physStaticCircle`-style, `flatCollideStaticDisc` with
  `ubInside`), posts are outside-circle constraints; both push to the boundary
  and reflect the normal velocity with restitution. Wall modes:
  `FLAT_WALL_PLAY` (mouths open, goal check is the caller's),
  `FLAT_WALL_NET` (boxed inside the goal), `FLAT_WALL_PLAYER` (players may
  enter the mouth, net boxes them in).
- `flatWorldStep` order: integrate → bounds+posts per body → every body pair
  → **bounds re-clamp**. The re-clamp pass matters: pair separation can push a
  squeezed body past the wall clamp applied earlier (player pinning the ball
  to a wall trembled ±1 px without it). Velocity was already resolved, so it
  only corrects positions.
- Disc-disc = positional correction + impulse split by inverse mass
  (player mass 2, ball mass 1, HaxBall-style), restitution = product of the
  pair's `fwBounce`. Against a **static disc (post)** the restitution comes
  from the dynamic body's `fwWallBounce × WALL_BOUNCE` instead — players slide
  off posts (bounce 0) while the ball dings (0.7).
- Kick is **edge-triggered once per press**, stays armed while held until it
  connects (HaxBall semantics); range `rP + rB + 4` without contact; holding
  kick reduces player acceleration (HaxBall kickingAccel). The impulse is
  **additive**: `ballVel += n·KICK_POWER + 0.3·playerVel` — a ball already
  moving away gains its speed on top, an incoming one isn't reset (overwrite
  semantics erased momentum and made volleys/passing feel random). The result
  is **capped to `BALL_MAX_SPEED` 4.75 by magnitude** (`fix16_sqrt`, safe at
  ball speeds) — additive kicks on a moving ball would otherwise hit 8.5 px/f
  diagonals, more per-frame travel than 50 Hz renders smoothly.
- Match clock: decrements only in `PHASE_PLAY` **and only while
  `s_ubKickoffLocked` is 0** — the kickoff wait (and the time after each goal
  until the ball is first touched) never eats match time. `s_ubKickoffLocked`
  is set at every kickoff and released by ANY ball movement (kick or dribble
  contact), independent of the KICKOFF LOCK option.
- Bounce sfx: ball velocity baseline is captured **after** friction damping,
  then any world-step change > 0.3 px/f (Manhattan) plays `SFX_BOUNCE` with a
  6-frame retrigger lockout and volume scaled by impact. The naive
  "velocity changed during step" test fired on EVERY frame the ball moved
  (friction alone changes velocity) — a 50 Hz machine gun.
- HUD score/timer redraws are **event-driven per double-buffer page**:
  per-page last-drawn trackers (`s_pHudScoreL[]` etc., reset to 0xFFFF at
  match create) — both pages must catch up after swaps, so "draw only when
  the value changed" needs the page index (`pBack == pFront` test).
- AI: per-frame role assignment per side (nearest bot attacks with 24px
  hysteresis), attacker kicks only when goal-side, in range AND roughly
  aligned with the goal; aim shifts toward a post near the goal; kick pulses
  are phase-shifted per slot so bots never kick in lockstep. Steering presses
  an axis when `|dx| > |dy|/3` (same 3:1 split as the old Manhattan-normalize
  threshold, but no `fix16_div`); 2px-Manhattan deadzone lets bots coast to a
  stop — verified to settle within 1 px of their spot, no brake logic needed.
