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

## What SynapseOS can and cannot host

synui implements Omarchy's `qs.Ui` and `qs.Commons` over **this** desktop's
theme — the same type names and the same contracts, drawing SynapseOS's font,
spacing and ink. `quickshell/Ui/` and `quickshell/Commons/` are that module.

Measured against 40 of the most-installed community widgets, **39 of 40 resolve
every type they name.** It did not start there:

| the module was | widgets resolving |
|---|---|
| `BarWidget` + `WidgetButton` | 9 of 40 |
| `+ BarIconButton` | 18 of 40 |
| `+ the Panel layer` | 22 of 40 |
| `+ Button, Toggle, ToggleSwitch` | 32 of 40 |
| `+ Dropdown, TextField, the rest` | 40 of 40 |

`tools/plugin-compat.sh` is what produced those numbers and is how to decide
what to build next: it clones a sample off the registry and prints which missing
type blocks the most widgets.

> ⚠ **"Resolves" is not "works."** It is a question about the module, not about
> behaviour. A widget can name every type correctly and still want something
> else — see below.

**What is still refused**, by name, before it reaches the bar:

- `import Quickshell.Hyprland`, which talks to Hyprland's IPC socket. synui is
  its own wlroots compositor and there is nothing to shim.
- A `qs.<Module>` this bar does not ship at all.

**What is not refused and still may not work**: a widget that reaches for a
HOST service rather than a type — `bar.shell.serviceFor(id)` is Omarchy's
service registry, and a widget built on one comes up idle here rather than
broken. Nothing in a manifest or an import can predict that.

`synui-plugins` refuses those **before** they reach the bar, naming the import.
The check is asked of the filesystem — quickshell resolves `import qs.Foo` to
`<shell root>/Foo` — so `qs.Ui` passes because it is genuinely provided, and
anything else fails because it genuinely is not.

### The softer case: a module we have, a type we have not

`import qs.Ui` passes because synui ships a `qs.Ui`. Where a widget names a type
that module still does not have, it clears the refusal and then draws nothing
useful — so the gap is reported rather than guessed at.

That is a **warning, not a refusal**. `add` runs Qt 6's `qmllint` against the
entry point with the bar's own modules on the import path, and prints the types
it could not resolve by name:

```
⚠ sebasgl23.snake uses types this bar does not provide: BarIconButton
  it loads; whatever needed them will not. The bar logs what fails.
```

It is installed and turned on anyway, because a type can be named on a path
nothing ever runs — a popup nobody opens — and a widget with a corner missing is
still a widget. Refusing on that guess would hide ones that work.

## Where the list comes from

Two places, and `browse` reads them as one list.

**Shipped** — `catalogue.tsv` beside this file. Five widgets out of Omarchy's
own repository, and the rule for that file is that *every row has been loaded
into a real bar*, not merely read. Those rows say `shipped`, and it is the only
`trust` value this project puts its own name to.

**The community registry** — [omarchyplugins.com](https://omarchyplugins.com),
around nine hundred bar widgets written for this format by other people, games
included. `synui-plugins refresh` fetches it; `registry.py` reduces their
catalogue to the same twelve columns and drops everything that could not
possibly run here — anything that is not a bar widget, anything their own
harness marks as failing, anything whose repository needs its own installer. The
result is cached under `~/.cache/synui/plugins` and refetched when it is a week
old. `verified` and `unverified` on those rows are **their** judgement about
**their** desktop, carried through unchanged; neither means anybody has loaded
the thing here.

> With no network you get the shipped rows and a line saying why the list is
> short. Nothing errors and nothing is empty.

## Using them

```
synui-plugins browse                  widgets you can install, and where from
synui-plugins browse games            narrow it — every word has to match
synui-plugins refresh                 fetch the community list now
synui-plugins add omarchy.spacer      install one of them
synui-plugins add <git-url>           install a plugin repository
synui-plugins list                    what is installed, and why anything is refused
synui-plugins synapse.uptime on       turn one on
synui-plugins remove <id>             delete one you installed
synui-plugins scan                    the TSV the bar reads
```

`browse` searches the id, the name, the description, the category, the tags and
the author. The tags are why `browse games` finds two dozen widgets whose
category is "Widgets" and whose names say nothing about games. It prints a page;
`--all` prints the lot.

`add` takes a catalogue id or a git URL. A shipped id is one widget out of a
repository that holds many, so it is a partial + sparse checkout of that one
path rather than a clone of somebody's whole desktop — and the repository's
LICENSE comes with it, because MIT wants the notice in every copy and a file put
on your disk is one. A registry id is a repository that *is* one plugin, so that
one is a plain shallow clone.

> `remove` only deletes out of `~/.config/synui/plugins`. The other two search
> paths are Omarchy's (theirs, with their own command) and the package's
> (pacman's) — turn those off instead.

Everything is off until asked for: a plugin is third-party code running inside
the bar's own process.
