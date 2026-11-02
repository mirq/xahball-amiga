XAHBall - HaxBall-style football for the Amiga
==============================================

Requirements
------------
- Amiga 500/500+/600/A1200 or emulator: 68000, 1 MB RAM (512K chip + 512K slow
  or fast), Kickstart 1.3 or newer, PAL (50 Hz).

Install
-------
Copy the whole "xahball" folder anywhere on your hard drive (e.g. DH0:Games/)
and run "xahball.exe" from its own directory - it loads assets from "data/"
relative to the executable. Keep the unix executable bit / set the Amiga "e"
protection flag on xahball.exe after transferring (many transfer tools strip
it: "protect xahball.exe e add" in a shell fixes it).

Controls
--------
- Joysticks are named by the Amiga port they are plugged into: PORT 2 is
  the joystick port, PORT 1 the mouse port, PORTS 3/4 need a parallel-port
  4-player adapter
- Arrows: move, RIGHT SHIFT kicks
- W S A D: move, LEFT SHIFT kicks
- Joystick: move, fire kicks (hold fire to charge, one kick per press)
- Menus: arrows or WSAD + RETURN/SPACE/X, or the joystick in port 2;
  ESC goes back. In a match ESC/P pauses, ESC again quits to the menu.

Main menu
---------
- START MATCH: pick the left and right team, then kick off
- TEAM SIZE: 1 vs 1 up to 3 vs 3
- CONTROLS: ready-made setups for the team size (left/right to change):
    1 vs 1: 1P VS CPU (port 2), JOY VS JOY (port 2 vs port 1),
            JOY VS KEYS (port 2 vs WSAD)
    2 vs 2: 1P VS CPU, 2 JOY 2 KEYS (port 2 + WSAD vs port 1 + arrows),
            4 JOYSTICKS (ports 2 + 4 vs ports 1 + 3)
    3 vs 3: 1P VS CPU, 4 JOY 2 KEYS (ports 2 + 4 + WSAD vs 1 + 3 + arrows)
  Stock A500 note: 3 vs 3 with 5 CPU players (1P VS CPU) is too much for a
  plain 7 MHz A500 - the game slows down in crowded moments. 3 vs 3 with
  6 human players (4 JOY 2 KEYS) runs at full speed, as do 1 vs 1 and
  2 vs 2 with any mix of humans and CPU.
  Press fire on CONTROLS for the custom setup: any device per player.
  A device picked twice is shown red and falls back to CPU for the later
  player. Every player without a device is played by the computer.
- CPU LEVEL: NORMAL or HARD
- MATCH TIME: 1/2/3/5 minutes. The clock starts only when the ball is
  first touched after each kickoff.
- RULES:
    BALL FREEZE: ON stops the ball where it crossed the line on a goal
    (default OFF - it rolls into the net)
    KICKOFF LOCK: ON keeps the other team out of the center circle until
    the kickoff team touches the ball (default ON)

Leave the title screen or the menu alone for a while and a demo match
starts - any key or fire button brings you back.

Credits
-------
- Menu music: "triangles" by X-Type
- Built with ACE (Amiga C Engine)
