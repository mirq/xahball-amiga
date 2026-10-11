#!/usr/bin/env python3
"""X11 keyboard injection + window capture for FS-UAE testing.

The local MCP bridge client can't run on the A500 (68000) profile, so this
drives the emulator directly: focuses the FS-UAE window, injects Amiga key
presses via XTEST, and captures the window content.

Usage:
  xkey.py keys "<seq>"        e.g. "up+ down+ return!" or "rshift!200"
  xkey.py capture <out.png>
"""
import sys
import time

from Xlib import X, display, Xatom
from Xlib.ext import xtest

# evdev -> X keycode map (standard x86 X.org)
KEYS = {
    "escape": 9, "return": 36, "enter": 36, "space": 65, "tab": 23,
    "up": 111, "down": 116, "left": 113, "right": 114,
    "lshift": 50, "rshift": 62, "lctrl": 37, "rctrl": 105,
    "lalt": 64, "ralt": 108, "lamiga": 204, "ramiga": 206,
    "backspace": 22, "delete": 119,
    "f1": 67, "f2": 68, "f3": 69, "f4": 70,
    "kp0": 90, "kp1": 87, "kp2": 88, "kp3": 89, "kpenter": 104,
    "w": 25, "a": 38, "s": 39, "d": 40, "x": 27, "p": 33, "q": 24, "z": 52,
    "c": 54, "m": 58, "n": 57, "t": 28, "h": 43, "l": 46, "k": 45, "j": 44,
    "y": 29, "u": 30, "i": 31, "o": 32, "e": 26, "r": 27, "f": 41, "g": 42,
    "v": 55, "b": 56,
    "semicolon": 47, "apostrophe": 48, "comma": 59, "dot": 60, "slash": 61,
}

# Characters that need Shift + base key when typing text
SHIFT_CHARS = {":": "semicolon", '"': "apostrophe", "?": "slash", ".": "dot", "/": "slash"}



def find_window(dpy, root, name_part, depth=0):
    """Recursively find a window whose WM_NAME contains name_part."""
    if depth > 6:
        return None
    wname = root.get_wm_name()
    if wname and name_part.lower() in str(wname).lower():
        return root
    net_name = root.get_full_property(dpy.intern_atom("_NET_WM_NAME"), 0)
    if net_name and name_part.lower() in str(net_name.value).lower():
        return root
    for child in root.query_tree().children:
        found = find_window(dpy, child, name_part, depth + 1)
        if found:
            return found
    return None


def find_window_by_pid(dpy, root, pid):
    """Find top-level FS-UAE window owned by given process pid."""
    for w in root.query_tree().children:
        prop = w.get_full_property(dpy.intern_atom("_NET_WM_PID"), 0)
        if prop and prop.value and int(prop.value[0]) == pid:
            return w
    return None


def focus_window(dpy, win):
    try:
        win.set_input_focus(X.RevertToParent, X.CurrentTime)
    except Exception:
        pass
    try:
        dpy.intern_atom("_NET_ACTIVE_WINDOW")
        ev = win.get_full_property(dpy.intern_atom("_NET_WM_DESKTOP"), 0)
        win.configure(stack_mode=X.Above)
    except Exception:
        pass
    dpy.flush()


def win_geometry(dpy, win):
    g = win.get_geometry()
    # top-level windows: geometry is already root-relative
    return g.x, g.y, g.width, g.height


def tap(dpy, keycode, delay=0.06):
    xtest.fake_input(dpy, X.KeyPress, keycode)
    dpy.sync()
    time.sleep(delay)
    xtest.fake_input(dpy, X.KeyRelease, keycode)
    dpy.sync()
    time.sleep(0.08)


def run_seq(dpy, seq):
    """Sequence syntax: space-separated tokens.
    'name'        - tap key
    'name!'       - tap with long press (200ms)
    'name+ name-' - hold / release
    'text:ABC'    - type letters (lowercase a-z only)
    'sleep:0.5'   - wait
    """
    tokens = seq.split()
    held = []
    for tok in tokens:
        if tok.startswith("sleep:"):
            time.sleep(float(tok[6:]))
            continue
        if tok.startswith("text:"):
            for ch in tok[5:]:
                if ch == "_":
                    ch = " "
                if ch == " ":
                    kc = 65  # space
                    tap(dpy, kc, 0.04)
                    continue
                if ch in SHIFT_CHARS:
                    base = SHIFT_CHARS[ch]
                    if isinstance(base, str):
                        base = KEYS[base]
                    xtest.fake_input(dpy, X.KeyPress, 50)  # left shift
                    dpy.sync()
                    tap(dpy, base, 0.04)
                    xtest.fake_input(dpy, X.KeyRelease, 50)
                    dpy.sync()
                    time.sleep(0.05)
                    continue
                kc = KEYS.get(ch)
                if kc:
                    tap(dpy, kc, 0.04)
            continue
        long_press = tok.endswith("!")
        release = tok.endswith("-")
        hold = tok.endswith("+")
        name = tok.rstrip("+-!").lower()
        kc = KEYS.get(name)
        if not kc:
            print(f"unknown key: {name}")
            continue
        if hold:
            xtest.fake_input(dpy, X.KeyPress, kc)
            dpy.sync()
            held.append(kc)
            time.sleep(0.05)
        elif release:
            if kc in held:
                held.remove(kc)
            xtest.fake_input(dpy, X.KeyRelease, kc)
            dpy.sync()
            time.sleep(0.05)
        else:
            tap(dpy, kc, 0.2 if long_press else 0.06)
    for kc in held:
        xtest.fake_input(dpy, X.KeyRelease, kc)
    dpy.sync()


def main():
    dpy = display.Display()
    root = dpy.screen().root
    win = None
    # xkey.py --pid <pid> keys "..." | capture out.png
    args = sys.argv[1:]
    pid = None
    wid = None
    if args and args[0] == "--pid":
        pid = int(args[1])
        args = args[2:]
    elif args and args[0] == "--wid":
        wid = int(args[1], 16)
        args = args[2:]
    if pid:
        win = find_window_by_pid(dpy, root, pid)
    elif wid:
        win = dpy.create_resource_object("window", wid)
    else:
        win = find_window(dpy, root, "fs-uae")
    if not win:
        print("window not found")
        sys.exit(1)
    cmd = args[0] if args else "capture"

    if cmd == "keys":
        focus_window(dpy, win)
        time.sleep(0.2)
        run_seq(dpy, args[1] if len(args) > 1 else "")
        print("keys sent")
    elif cmd == "capture":
        # Off-screen capture via XComposite - works even when the window
        # is occluded by other windows (requires a compositing WM)
        from Xlib.ext import composite
        from PIL import Image
        out = args[1] if len(args) > 1 else "/tmp/xkey_capture.png"
        try:
            composite.redirect_window(win, composite.RedirectAutomatic)
            dpy.flush()
            pix = composite.name_window_pixmap(win)
            g = win.get_geometry()
            raw = pix.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
            img = Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX")
            img.save(out)
            print(f"{out} ({g.width}x{g.height}) composite")
        except Exception as e:
            # Fallback to root capture
            import subprocess
            x, y, w, h = win_geometry(dpy, win)
            subprocess.run(
                f"xwd -root -silent | convert xwd:- -crop {w}x{h}+{x}+{y} {out}",
                shell=True, check=False,
            )
            print(f"{out} ({w}x{h}+{x}+{y}) root-crop (composite failed: {e})")
    else:
        print(f"unknown cmd {cmd}")


if __name__ == "__main__":
    main()
