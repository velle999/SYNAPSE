#!/usr/bin/env python3
"""Try to make a wedged Steam main window paint, least-invasive first.

Success = the window shows up in `synctl clients`, i.e. wlroots finally saw a
buffer and emitted map. Stops at the first stage that works.
"""
import ctypes, ctypes.util, json, subprocess, sys, time

WIN = 0x1e00046

xlib = ctypes.CDLL(ctypes.util.find_library("X11"))

# Every call gets argtypes/restype. Without this the 64-bit Display* is
# truncated to int and the process segfaults (bitten before).
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XFlush.argtypes = [ctypes.c_void_p]
xlib.XFlush.restype = ctypes.c_int
xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xlib.XSync.restype = ctypes.c_int
xlib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                               ctypes.c_uint, ctypes.c_uint]
xlib.XResizeWindow.restype = ctypes.c_int
xlib.XClearArea.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                            ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
                            ctypes.c_int]
xlib.XClearArea.restype = ctypes.c_int
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
    """Did wlroots emit map for a steam view?"""
    try:
        out = subprocess.run(["synctl", "clients"], capture_output=True,
                             text=True, timeout=5).stdout
        for v in json.loads(out):
            if v.get("app_id") == "steam":
                return v
    except Exception as e:
        print(f"    (synctl read failed: {e})")
    return None


def settle(label, secs=6):
    for _ in range(secs):
        time.sleep(1)
        v = painted()
        if v:
            print(f"\n*** {label} WORKED — steam mapped ***")
            print("    " + json.dumps(v))
            return True
    print(f"    {label}: still no steam view after {secs}s")
    return False


print("baseline:", "steam present?!" if painted() else "no steam view (wedged)")

# Stage A: a real geometry change. Chromium re-lays-out on ConfigureNotify and
# should produce a frame even if its occlusion logic thinks it is hidden.
print("\n[A] XResizeWindow 2540x1366 -> 2539x1365 -> back")
xlib.XResizeWindow(dpy, WIN, 2539, 1365)
xlib.XSync(dpy, 0)
time.sleep(2)
xlib.XResizeWindow(dpy, WIN, 2540, 1366)
xlib.XSync(dpy, 0)
if settle("stage A"):
    xlib.XCloseDisplay(dpy); sys.exit(0)

# Stage B: force an Expose over the whole window.
print("\n[B] XClearArea with exposures=True")
xlib.XClearArea(dpy, WIN, 0, 0, 0, 0, 1)
xlib.XSync(dpy, 0)
if settle("stage B"):
    xlib.XCloseDisplay(dpy); sys.exit(0)

# Stage C: full unmap/map. Most invasive — makes Xwayland tear down and rebuild
# the wl_surface, which is the thing that is failing to carry a buffer.
print("\n[C] XUnmapWindow + XMapWindow")
xlib.XUnmapWindow(dpy, WIN)
xlib.XSync(dpy, 0)
time.sleep(2)
xlib.XMapWindow(dpy, WIN)
xlib.XSync(dpy, 0)
if settle("stage C", 10):
    xlib.XCloseDisplay(dpy); sys.exit(0)

print("\nAll stages failed: Steam never produced a buffer. Unrecoverable.")
xlib.XCloseDisplay(dpy)
sys.exit(1)
