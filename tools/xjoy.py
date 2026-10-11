#!/usr/bin/env python3
"""Virtual Amiga joystick for FS-UAE testing via uinput.

Unlike tools/xkey.py this never touches the X server - it creates a real
kernel joystick device, so it works while other windows have focus and
never disturbs the desktop user.

Usage:
  xjoy.py daemon               # background: owns the uinput device, listens
                               # on /tmp/xjoy.sock
  xjoy.py <cmds>               # e.g. "down! sleep:0.3 right+ sleep:1 right- fire!"
  xjoy.py --status             # check daemon is alive

Key syntax (same spirit as xkey.py):
  tap: "fire!" long tap, "fire" short tap
  hold: "right+", release: "right-"
  "sleep:0.5" waits
"""
import os
import socket
import sys
import time

from evdev import AbsInfo
from evdev import UInput
from evdev import ecodes as e

SOCK = "/tmp/xjoy.sock"


def make_ui():
    return UInput(
        name="XAHBall Virt Joy", vendor=0x1234, product=0x9ABC, version=1,
        events={
            e.EV_KEY: [e.BTN_TRIGGER],
            e.EV_ABS: [
                (e.ABS_X, AbsInfo(-1, -1, 1, 0, 0, 0)),
                (e.ABS_Y, AbsInfo(-1, -1, 1, 0, 0, 0)),
            ],
        },
    )


def daemon():
    ui = make_ui()
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(SOCK)
    s.listen(4)
    print(f"xjoy daemon ready on {SOCK}", flush=True)
    while True:
        conn, _ = s.accept()
        conn.settimeout(10)
        try:
            f = conn.makefile()
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line.startswith("sleep:"):
                    time.sleep(float(line[6:]))
                    continue
                release = line.endswith("-")
                hold = line.endswith("+")
                name = line.rstrip("+-!").lower()
                if name in ("left", "right", "up", "down"):
                    code = e.ABS_X if name in ("left", "right") else e.ABS_Y
                    val = -1 if name in ("left", "up") else 1
                    if release:
                        ui.write(e.EV_ABS, code, 0)
                        ui.syn()
                    else:
                        ui.write(e.EV_ABS, code, val)
                        ui.syn()
                        time.sleep(0.12)
                        if not hold:
                            ui.write(e.EV_ABS, code, 0)
                            ui.syn()
                elif name == "fire":
                    if release:
                        ui.write(e.EV_KEY, e.BTN_TRIGGER, 0)
                        ui.syn()
                    else:
                        ui.write(e.EV_KEY, e.BTN_TRIGGER, 1)
                        ui.syn()
                        time.sleep(0.12)
                        if not hold:
                            ui.write(e.EV_KEY, e.BTN_TRIGGER, 0)
                            ui.syn()
        except (socket.timeout, BrokenPipeError, ConnectionResetError):
            pass
        finally:
            conn.close()


def send(cmds):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    # Daemon parses one command per line - split the whitespace-separated
    # token string so "down! down! fire!" actually sends three commands
    s.sendall(("\n".join(cmds.split()) + "\n").encode())
    s.close()


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == "daemon":
        daemon()
    elif args[0] == "--status":
        try:
            send("sleep:0")
            print("alive")
        except Exception as ex:
            print(f"dead: {ex}")
            sys.exit(1)
    else:
        send(" ".join(args))


if __name__ == "__main__":
    main()
