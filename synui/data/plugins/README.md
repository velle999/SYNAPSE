# Bar plugins

SynapseOS's bar takes third-party widgets in **[Omarchy's shell-plugin
format](https://omarchy.org/manual/shell-plugins/)**.

## Why that format and not one of our own

Omarchy's desktop is a single long-lived quickshell process in which the bar,
the panels and the overlays are all plugins. synui's bar is quickshell too. That
makes their format the only one already describing *"a QML widget you can drop
into a quickshell bar"* — so synui reads it, and a widget written once loads on
either desktop, instead of each project growing an incompatible directory layout
for the same idea.

## What a plugin is

A directory with a `manifest.json` and some QML:

```json
{
  "schemaVersion": 1,
  "id": "example.uptime",
  "name": "Uptime",
  "version": "1.0.0",
  "kinds": ["bar-widget"],
  "entryPoints": { "barWidget": "Uptime.qml" },
  "barWidget": { "displayName": "Uptime", "category": "System",
                 "allowMultiple": false }
}
```

> ⚠ `kinds` is hyphenated (`bar-widget`) and `entryPoints` is camelCase
> (`barWidget`). That asymmetry is Omarchy's spelling, not a typo here, and
> getting it wrong is the likeliest reason a hand-written manifest lists but
> loads nothing.

Searched, in order — the first is Omarchy's own, so a plugin installed with
`omarchy plugin add <git-url>` is found without copying it:

1. `~/.config/omarchy/plugins/`
2. `~/.config/synui/plugins/`
3. `/usr/share/synui/plugins/`

## The widget

Root at `BarWidget` and you are handed the contract Omarchy documents:

| | |
|---|---|
| `bar` | the host bar |
| `moduleName` | this plugin's manifest id, filled in by the host |
| `settings` | per-widget overrides — empty on synui today |
| `vertical` | is the bar a column (false: synui's bar is a strip) |
| `barSize` | the bar's thickness in pixels |
| `broadcast(m)` | run method `m` on every instance, one per screen |
| `setting(n, f)` | one settings value, with a fallback |

`synapse.uptime/` beside this file is the worked example — heavily commented,
shipped off, and the thing to copy.

> ⚠ Set `implicitWidth`. The bar lays widgets out by it, and one that leaves it
> at 0 is loaded, running and invisible.

## What SynapseOS cannot host

**An arbitrary Omarchy widget will not run here**, and the plugin system says so
rather than showing an empty space.

Their shipped widgets root at `BarWidget` — that part is portable and is
implemented in `quickshell/Ui/BarWidget.qml` — but they also:

- `import qs.Commons`, Omarchy's own singletons. `Style.qml` alone is 23 KB of
  API; reproducing it would be reimplementing their desktop.
- `import Quickshell.Hyprland`, which talks to Hyprland's IPC socket. synui is
  its own wlroots compositor and there is nothing to shim.

`synui-plugins` refuses those **before** they reach the bar, naming the import.
The check is asked of the filesystem — quickshell resolves `import qs.Foo` to
`<shell root>/Foo` — so `qs.Ui` passes because it is genuinely provided, and
anything else fails because it genuinely is not.

## Using them

```
synui-plugins browse                  widgets you can install, and where from
synui-plugins add omarchy.spacer      install one of them
synui-plugins add <git-url>           install a plugin repository
synui-plugins list                    what is installed, and why anything is refused
synui-plugins synapse.uptime on       turn one on
synui-plugins remove <id>             delete one you installed
synui-plugins scan                    the TSV the bar reads
```

`add` takes a catalogue id or a git URL. A catalogue id is one widget out of a
repository that holds many, so it is a partial + sparse checkout of that one
path rather than a clone of somebody's whole desktop — and the repository's
LICENSE comes with it, because MIT wants the notice in every copy and a file put
on your disk is one.

> `remove` only deletes out of `~/.config/synui/plugins`. The other two search
> paths are Omarchy's (theirs, with their own command) and the package's
> (pacman's) — turn those off instead.

Everything is off until asked for: a plugin is third-party code running inside
the bar's own process.
