#!/usr/bin/env python3
"""Replay synui's phosphor pass offline — the shipping hot core vs whitening.

Kept in the tree because this replay keeps being re-derived by hand every time
the phosphor is argued about, and every re-derivation is a chance to fit the
wrong reference. Run it; do not reason about the shader in prose.

The pass is a pure function of one scalar `e` (the phosphor drive), so the whole
hot-core question can be answered without a GPU, a compositor, or a rebuild.
That is the discipline the curve work already established: replay offline.

    ph = mix(tint, HOT, smoothstep(PH_HOT, 1.0, e) * PH_HOTMAX) * e

Only HOT differs between the two versions.
"""

PH_HOT, PH_HOTMAX = 0.55, 0.75
AMBER = (1.00, 0.70, 0.12)
GREEN = (0.32, 1.00, 0.20)
WHITE = (1.00, 0.97, 0.92)


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def phosphor(e, tint, hot):
    f = smoothstep(PH_HOT, 1.0, e) * PH_HOTMAX
    return tuple(min(c * e, 1.0) for c in mix(tint, hot, f))


def ratios(c):
    r = c[0] if c[0] > 1e-6 else 1e-6
    return c[1] / r, c[2] / r


def hexs(c):
    return "#%02x%02x%02x" % tuple(int(round(min(max(x, 0), 1) * 255)) for x in c)


# What velle measured off a photograph of a real amber tube.
REAL = {"mid": (0.687, 0.230), "hot": (0.933, 0.079)}
# ...and off our filter.
OURS = {"mid": (0.719, 0.266), "hot": (0.934, 0.737)}

print("The hot core, amber, at several drive levels")
print("  e     WHITENING (the old fault)   SHIPPING hot=(1,1,tint.b)")
print("        hex      G/R    B/R        hex      G/R    B/R")
for e in (0.40, 0.55, 0.70, 0.85, 1.00):
    cur = phosphor(e, AMBER, (1.0, 1.0, 1.0))
    new = phosphor(e, AMBER, (1.0, 1.0, AMBER[2]))
    gc, bc = ratios(cur)
    gn, bn = ratios(new)
    print(f"  {e:.2f}  {hexs(cur)}  {gc:.3f}  {bc:.3f}      "
          f"{hexs(new)}  {gn:.3f}  {bn:.3f}")

print()
print("Against the measurements (ratios are what survive exposure/backdrop):")
print(f"  real tube  mid  G/R {REAL['mid'][0]:.3f}  B/R {REAL['mid'][1]:.3f}")
print(f"  real tube  hot  G/R {REAL['hot'][0]:.3f}  B/R {REAL['hot'][1]:.3f}")
print(f"  ours       mid  G/R {OURS['mid'][0]:.3f}  B/R {OURS['mid'][1]:.3f}")
print(f"  ours       hot  G/R {OURS['hot'][0]:.3f}  B/R {OURS['hot'][1]:.3f}   <-- the fault")
cur_hot = ratios(phosphor(1.0, AMBER, (1.0, 1.0, 1.0)))
new_hot = ratios(phosphor(1.0, AMBER, (1.0, 1.0, AMBER[2])))
print(f"  predicted, current   G/R {cur_hot[0]:.3f}  B/R {cur_hot[1]:.3f}"
      f"   (measured {OURS['hot'][0]:.3f} / {OURS['hot'][1]:.3f})")
print(f"  predicted, proposed  G/R {new_hot[0]:.3f}  B/R {new_hot[1]:.3f}"
      f"   (target   {REAL['hot'][0]:.3f} / {REAL['hot'][1]:.3f})")

print()
print("The other two phosphors must not regress:")
for name, t in (("green", GREEN), ("white", WHITE)):
    cur = phosphor(1.0, t, (1.0, 1.0, 1.0))
    new = phosphor(1.0, t, (1.0, 1.0, t[2]))
    print(f"  {name:<5} hot  current {hexs(cur)}   proposed {hexs(new)}")
