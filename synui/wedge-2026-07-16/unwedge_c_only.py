#!/usr/bin/env python3
"""Stage C ONLY: XUnmapWindow + XMapWindow on the wedged Steam main window.

Last session proved "A->B->C recovers" but not "C alone recovers". A and B are
deliberately NOT run here so a success isolates C as the sole cause.

Success = a steam view appears in `synctl clients`.
"""
import ctypes, ctypes.util, json, subprocess, sys, time

WIN = 0x1e00046

xlib = ctypes.CDLL(ctypes.util.find_library("X11"))

# argtypes on EVERY call: without them the 64-bit Display* truncates and segfaults.
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xlib.XSync.restype = ctypes.c_int
xlib.XUnmapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
xlib.XUnmapWindow.restype = ctypes.c_int
xlib.XMapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
xlib.XMapWindow.restype = ctypes.c_int
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
xlib.XCloseDisplay.restype = ctypes.c_int

dpy = xlib.XOpenDisplay(b":0")
if not dpy:
    sys.exit("cannot open :0")


def painted():
    try:
        out = subprocess.run(["synctl", "clients"], capture_output=True,
                             text=True, timeout=5).stdout
        for v in json.loads(out):
            if v.get("app_id") == "steam":
                return v
    except Exception as e:
        print(f"    (synctl read failed: {e})")
    return None


base = painted()
print("baseline:", "steam ALREADY present - not wedged, abort" if base else "no steam view (wedged, as expected)")
if base:
    xlib.XCloseDisplay(dpy)
    sys.exit(2)

print("\n[C-alone] XUnmapWindow + XMapWindow on 0x%x" % WIN)
xlib.XUnmapWindow(dpy, WIN)
xlib.XSync(dpy, 0)
time.sleep(2)
xlib.XMapWindow(dpy, WIN)
xlib.XSync(dpy, 0)

for i in range(12):
    time.sleep(1)
    v = painted()
    if v:
        print(f"\n*** STAGE C ALONE WORKED (after {i+1}s) ***")
        print("    " + json.dumps(v))
        xlib.XCloseDisplay(dpy)
        sys.exit(0)

print("\nStage C alone FAILED after 12s -- C is not sufficient on its own.")
xlib.XCloseDisplay(dpy)
sys.exit(1)
