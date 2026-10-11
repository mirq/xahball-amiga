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
- Player 1 (KB1): arrow keys to move, RIGHT SHIFT to kick
- Player 2 (KB2): WSAD to move, LEFT SHIFT to kick
- Joysticks 1-4 (JOY3/JOY4 on the parallel port) also supported
- ESC pauses / quits, fire confirms in menus

Menu options
------------
- TEAM SIZE: 1vs1 to 3vs3
- MATCH TIME: 1/2/3/5 minutes
- BALL FREEZE: ON freezes the ball where it crossed the line on a goal
  (default OFF - the ball rolls into the net)
- KICKOFF LOCK: ON bars the non-kickoff team from the center circle until
  the kickoff team first touches the ball; the conceding team kicks off
  (default ON)
- The match clock starts only when the ball is first touched in each kickoff
  (kickoff countdown, goal celebrations and the wait before the first touch
  do not consume match time).
- CONTROLLERS: per-slot device assignment. Duplicates are allowed while
  cycling and resolve to AI BOT (first slot keeps the device) when leaving
  the screen.
