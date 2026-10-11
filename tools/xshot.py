#!/usr/bin/env python3
"""Screenshot helper for the FS-UAE test window on a shared desktop.

Briefly activates the FS-UAE window (EWMH), captures the root screen, then
restores focus to whatever window was active before - so the desktop user
gets a ~1 second flicker instead of losing keyboard focus permanently.

Usage: xshot.py <pid> <out.png>
"""
import sys
import time

from Xlib import X, display
from Xlib.protocol import event as xevent


def find_window_by_pid(dpy, root, pid):
    hits = []

    def walk(w):
        try:
            prop = w.get_full_property(dpy.intern_atom("_NET_WM_PID"), 0)
            if prop and prop.value[0] == pid:
                hits.append(w)
                return
        except Exception:
            pass
        for c in w.query_tree().children:
            walk(c)

    walk(root)
    return hits[0] if hits else None


def get_active_window(dpy):
    root = dpy.screen().root
    prop = root.get_full_property(dpy.intern_atom("_NET_ACTIVE_WINDOW"), 0)
    if prop and prop.value:
        return prop.value[0]
    return 0


def activate(dpy, w):
    root = dpy.screen().root
    ev = xevent.ClientMessage(
        window=w, client_type=dpy.intern_atom("_NET_ACTIVE_WINDOW"),
        data=(32, [1, 0, 0, 0, 0]),  # source: application
    )
    root.send_event(
        ev, event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask
    )
    dpy.flush()


def main():
    pid = int(sys.argv[1])
    out = sys.argv[2]
    dpy = display.Display()
    win = find_window_by_pid(dpy, dpy.screen().root, pid)
    if not win:
        print("window not found")
        sys.exit(1)
    prev = get_active_window(dpy)
    for attempt in range(4):
        activate(dpy, win)
        time.sleep(0.6)
        active = get_active_window(dpy)
        if active == win.id:
            break
        time.sleep(0.3)
    time.sleep(0.4)
    import subprocess
    rc = subprocess.run(["import", "-window", "root", out])
    if prev:
        prevWin = dpy.create_resource_object("window", prev)
        try:
            activate(dpy, prevWin)
        except Exception:
            pass
    sys.exit(rc.returncode if hasattr(rc, "returncode") else 0)


if __name__ == "__main__":
    main()
