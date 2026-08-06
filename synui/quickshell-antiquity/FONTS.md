# Fonts shipped with the Antiquity shell

This shell is a port of [linux-antiquity](https://github.com/diinki/linux-antiquity)
(MIT, © 2026 diinki). Upstream bundles nine font files. **Four are
redistributable and ship here; three are not and were removed**; the remaining
two are the italic companions of faces already listed.

Every claim below was read out of the fonts' own OpenType `name` table
(IDs 0, 8, 13, 14), not inferred from filenames.

## Shipped

| File | Foundry | Terms |
|---|---|---|
| `Boska-Variable.ttf`, `Boska-VariableItalic.ttf` | Indian Type Foundry | ITF, via Fontshare |
| `Recia-Variable.ttf`, `Recia-VariableItalic.ttf` | Indian Type Foundry | ITF, via Fontshare |
| `Quilon-Variable.ttf` | Indian Type Foundry | ITF, via Fontshare |
| `MaterialSymbolsSharp_Filled_36pt-Regular.ttf` | Google LLC | Apache-2.0 |

### The Indian Type Foundry credit clause

Boska, Recia and Quilon are Copyright © Indian Type Foundry (Boska 2017–2021,
Recia 2015–2021, Quilon 2020–2021). Their embedded licence string states:

> This Font Software is protected under domestic and international trademark
> and copyright law. You agree to identify the ITF fonts by name and credit the
> ITF's ownership of the trademarks and copyrights in any design or production
> credits.

**This file is that credit.** The obligation is naming, not a copyleft — it does
not reach synui's own GPL-2.0-or-later code, and it is why these fonts may not
be silently renamed or subsetted without carrying the notice along. Full terms:
<https://fontshare.com/terms>.

## Removed, and why

| File | Rights holder | Why it could not ship | What replaced it |
|---|---|---|---|
| `Monaco.ttf` | © 1990–97 Apple Computer / Type Solutions / The Font Bureau | Proprietary; no redistribution grant | `Config.fontMono` → **DejaVu Sans Mono**, already a hard synui dependency (`ttf-dejavu`) |
| `Charcoal.ttf` | © 1995–97 The Font Bureau, Inc. | Proprietary; no redistribution grant | **Nothing** — upstream loads it in `shell.qml` and never references it anywhere, so removing it changes no pixels |
| `DOMINICA.TTF` | © 2002, 2009 Harold Lohner (haroldsfonts.com) | Donationware; no redistribution grant for bundling in an OS | **Recia**, at all three decorative sites — keeps the high-contrast serif set against Boska that the original pairing existed for |

The three replaced call sites are `widgets/ClockWidget.qml` (the date line),
`widgets/WeatherWidget.qml` (the humour name) and
`sidebarPopups/ThemingMenu.qml` (the swatch letter). `taskbar/ClockWidget.qml`
is the single Monaco site.

## Icon theme

Upstream also vendors `buuf-nestort` — 7,552 loose files with **no licence of
any kind** in the tree. Buuf is Paul Davey's artwork and has historically been
non-commercial-only, so SYNAPSE does not redistribute it. `shell.qml` therefore
carries no `IconTheme` pragma and follows the system icon theme. See the comment
at the top of `shell.qml` for how to opt back in locally.

## Wallpapers

The three collages under upstream's `configs/hypr/wallpapers_bundled/` are
diinki's own compositions and fall under the repository's MIT licence. They are
not vendored here; they belong in `synapse-wallpapers` if wanted.
