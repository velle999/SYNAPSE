# synui

The SynapseOS Wayland compositor: a desktop that draws its own bar, dock,
panels and lock screen rather than assembling them from other projects. Built
on wlroots and scenefx, so the blur, shadows and rounded corners are the
scene graph's rather than a shader bolted on afterwards.

It installs a `wayland-sessions` entry, so any display manager offers it as a
session to log in to.

## What is in it

- **A bar and a dock**, both first-party, with pinned applications, a start
  menu, a searchable shortcut palette (`Super+/`) and a control panel
  (`Super+C`) that holds every setting the desktop has.
- **Window management** — tiling snaps, a grid, monocle, per-monitor
  workspaces, and an Alt-Tab switcher that draws live thumbnails.
- **Panels for the things a desktop needs**: display arrangement, audio and
  an equaliser, Bluetooth, Wi-Fi, notifications, a calculator, a clock and
  calendar, screen recording, and a screensaver with a lock screen behind it.
- **Effects that can be turned off**: glass, blur, animations, a CRT filter,
  colour grading, night light, and HDR10 output on a display that advertises
  it.
- **An AI command bar** (`Super+Space`) and a neural overlay, both of which
  are off unless a backend is present.
- **Its own words, in fourteen languages** — the control panel, dock, desktop
  menu, lock screen, screensaver and effect panels, 820 strings, complete in
  all thirteen catalogs and following the locale with nothing to configure.
  Arabic and Hindi are shaped and reordered rather than merely translated:
  synui draws its own text, so it carries HarfBuzz and FriBidi to join Arabic,
  run it right-to-left, and put Devanagari matras where they belong. The bar
  and the desktop widgets are quickshell surfaces and are still English.

`synctl` is the command line to a running compositor — keybindings, outputs,
the recent-application list, and the state the panels read.

## Before you install it on a machine you did not build

synui ships system-wide files, not only its own. Among them is
`/etc/xdg/kdeglobals`, which re-themes every Qt and KDE application on the
machine, and `/etc/xdg/menus/applications.menu`. That is deliberate for a
distribution and worth knowing about for anything else.

It also pulls in a wide dependency set — a terminal, a portal stack, a
screenshot and recording pair, an icon theme and fonts — because the desktop
is expected to work the moment it is logged into.

## Running it

```bash
synui --help          # options and the default keybindings
synui --no-ai         # without the AI command bar and layout hints
synctl binds          # what the running compositor is bound to
```

Configuration is `~/.config/synui/synuirc`, falling back to
`/etc/synui/synuirc`. `Super+/` lists every binding and runs one; F2 in that
palette moves the launcher keys and F3 moves what a tapped `Super` opens.
