# Installing the desktop and its apps on plain Arch

Everything in this directory is about one thing: using `synui` and the `syn-*`
applications on a machine that is **not** running SynapseOS.

Nothing here is required to build SynapseOS itself. `./build-all.sh` behaves
exactly as it did before this existed.

---

## What you can install

| | |
|---|---|
| **The desktop** | `synui` (the wlroots compositor, its bar, dock, panels and lock screen) and `syntty` (the terminal it hard-depends on) |
| **The applications** | `syn-cal` `syn-clean` `syn-disks` `syn-edit` `synfiles` `syn-play` `syn-settings` `synsh` `synstudio` `syn-vault` |

Deliberately **not** here: `synapd` and `synapse-llama` (a pinned llama.cpp
build), `synapse_kmod` (DKMS against a running kernel), `synguard`, and
`syn-install` / `syn-update`, which manage a SynapseOS installation. Each of
those means something different, or nothing, on a machine this project does not
own.

`scenefx0.5` is a component of this repo too, but it is not in the list because
it needs nothing from it — its `PKGBUILD` already fetches from
[wlrfx/scenefx](https://github.com/wlrfx/scenefx) and builds anywhere.

## Requirements

An Arch or Arch-derived system with `base-devel`. **Every dependency except
`scenefx0.5` is in Arch's own repositories** — `wlroots0.20`, `quickshell`,
`rofi`, `cava`, `wtype`, `wf-recorder`, `wlr-randr`, `gocryptfs` and the rest
are all in `extra`. You do not need to add a repository, and there is no binary
package to trust: `makepkg` builds from source in front of you.

## Installing an application

Each application is one package repository, and none of them needs the
compositor:

```bash
git clone https://github.com/velle999/syn-play
cd syn-play && makepkg -si
```

That is the whole of it. The `PKGBUILD` fetches its own source tarball from the
release matching its version, and a build leaves the clone clean, so `git pull`
later gets the next version.

Without cloning anything, the `PKGBUILD` alone is enough:

```bash
curl -LO https://github.com/velle999/SYNAPSE/raw/main/syn-play/PKGBUILD
makepkg -si
```

Building from a checkout of this repository works too, and is the same two
commands with the source assembled locally instead of downloaded:

```bash
git clone --depth 1 https://github.com/velle999/SYNAPSE
cd SYNAPSE && tools/collect-source.sh syn-play
cd syn-play && makepkg -si
```

## Installing the desktop

Order matters — `synui` depends on both of the packages before it:

```bash
for c in syntty synui; do
    ( git clone "https://github.com/velle999/$c" && cd "$c" && makepkg -si )
done
```

`scenefx0.5` comes first and lives in the monorepo, since it only wraps an
upstream release:

```bash
curl -LO https://github.com/velle999/SYNAPSE/raw/main/scenefx0.5/PKGBUILD
makepkg -si
```

`synui` installs `/usr/share/wayland-sessions/synui.desktop`, so any display
manager that reads that directory — GDM, SDDM, greetd, ly — offers **synui** as
a session at the next login. Nothing else has to be configured.

## What works, and what quietly does not

The applications are plain C plus a `quickshell` window and they do not require
the compositor. Their windows read `~/.config/synui/theme.json` for the accent,
the panel colour and the UI font, and **fall back to built-in values when it is
absent** — so on GNOME or KDE they come up in the default purple rather than
looking broken.

What you will not get outside SynapseOS:

- **Anything that asks the AI daemon.** `synapd` is not in this set, so
  synui's assistant, the AI pane in `syn-settings` and `synsh`'s natural-language
  half have nothing to talk to. They report that rather than hanging.
- **`syn-update`.** Upgrading is `makepkg -si` again, per package.
- **The kernel telemetry** `synguard` and the bar's security readout use.

## ⚠ synui installs files that belong to the whole machine

`synui` is written on the assumption that it *is* the desktop, and it ships
system-wide configuration to make that true:

```
/etc/xdg/kdeglobals                        Qt/KDE palette and font
/etc/xdg/menus/applications.menu           the XDG menu KDE apps index
/etc/xdg/kservicemenurc                    Dolphin service menus
/etc/xdg/xdg-desktop-portal-wlr/config     the portal's screencast backend
/etc/MangoHud.conf                         the game overlay's defaults
/etc/pam.d/synui-lock, synui-lock-fprint   the lock screen's PAM stacks
/etc/systemd/logind.conf.d/synapse-lid.conf   lid-close behaviour
```

Two of those are not private to synui. **`/etc/xdg/kdeglobals` re-themes every
Qt and KDE application on the machine**, and
`/etc/systemd/logind.conf.d/synapse-lid.conf` changes what closing a laptop lid
does. If you already run Plasma, installing `synui` will change how it looks.

pacman will refuse to install over a file another package owns, so a genuine
conflict stops the install rather than silently taking something over — but a
file **nothing** owns (one you wrote yourself) it will also refuse, and one this
package owns it will simply replace. Read the list above before installing
`synui` beside another desktop. The applications ship nothing outside their own
namespace and are safe anywhere.

## Updating

A component's real version here is its `pkgrel`: `synui` is `0.1.0-561`, and
every change to what it builds bumps it (`tools/preflight.sh` refuses a commit
that does not). So re-fetching the `PKGBUILD` and running `makepkg -si` gets the
next version, and `pacman` will tell you it is newer.

---

## For maintainers

### ⛔ Why the sources are NOT on this repository's releases page

They were, for about an hour, and it was wrong twice over. Twelve component
tarballs per release round buried the ISO downloads — and because GitHub calls
the newest release "Latest", the project's release badge and every
`/releases/latest` link resolved to a source tarball instead of the operating
system.

A package's sources belong with its PKGBUILD, so both live at
`github.com/velle999/<pkgname>` and SYNAPSE's releases page is ISOs only.

### How one `source=()` line serves both

Every in-house `PKGBUILD` reads:

```bash
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
```

`build-all.sh` runs `tools/collect-source.sh`, which drops
`$pkgname-$pkgver.tar.gz` beside the `PKGBUILD`. **makepkg uses a source file
that is already present and never touches the URL** — it prints `-> Found
syn-play-0.1.0.tar.gz`. Somebody without this checkout has no such file, so the
same line downloads it.

That is one file, one set of `depends`, one set of install rules. A second
`PKGBUILD` maintained for outside use would be free to drift from this one, and
the person it broke for could not see this file at all.

The tag carries the `pkgrel`, so the URL cannot point at the wrong source, and
`tools/preflight.sh`'s `external` check holds the two ends together: a component
in `publish-sources.sh`'s `EXTERNAL` whose `source=()` lacks the URL, or a
`PKGBUILD` carrying a URL nothing publishes, is a finding. It checks the
dependency closure too — publishing `synui` without `syntty` would hand somebody
a package they cannot install.

### ⛔ Why `sha256sums=('SKIP')` and not a real checksum

Because a real checksum would break every **local** build. `build-all.sh`
regenerates that tarball from the working tree, so the moment anybody edits a
source file its hash changes and `makepkg` rejects it — the local-file path and
a pinned hash cannot both be true.

The published asset is **reproducible** instead, which is the property that
actually matters to somebody downloading it. `collect-source.sh` sorts entries
and zeroes timestamps and ownership, so at the tagged commit:

```bash
git checkout <the commit that published it>
tools/collect-source.sh syn-play
sha256sum syn-play/syn-play-0.1.0.tar.gz     # == the released asset
```

### Publishing

```bash
tools/publish-sources.sh --list      # the external set and what is published
tools/publish-sources.sh --dry-run
tools/publish-sources.sh             # create what is missing
```

It refuses to run on a dirty tree: the tarball is built from the working tree,
so an uncommitted edit would ship inside an asset nobody can re-derive from its
tag. Re-run it after any `pkgrel` bump — the new tag is a new release, and the
old one stays valid for the `PKGBUILD` that pointed at it.

### Per-package git repositories

`packaging/git-export.sh` writes each component as its own git repository —
`PKGBUILD`, a generated `.SRCINFO`, its `.install` scriptlet if it has one, and
a `.gitignore` so a build does not leave the clone dirty:

```bash
packaging/git-export.sh                 # all of them, into packaging/out/
packaging/git-export.sh syn-play        # just one
```

It copies rather than rewrites — the exported `PKGBUILD` is byte-for-byte the
one in the tree, so there is still only one of them — and it is idempotent:
re-running after a `pkgrel` bump makes one commit per changed package and says
`unchanged` for the rest.

⚠ **Identity is set per repository, on purpose.** `~/.gitconfig` on this machine
carries credential helpers and no `user.email`, so a freshly created repo has
none; commits then either fail or land unattributed, and GitHub — which matches
commits to accounts by email — shows an unmatched one as an anonymous
contributor.

**This is the AUR's own layout.** Nothing about it is AUR-specific, so these
push to any git host, but the `aur` remote is already recorded on every one:

```bash
git -C packaging/out/syn-play push origin main   # github.com/velle999/syn-play
git -C packaging/out/syn-play push aur main      # if and when the AUR opens up
```

`origin` defaults to `github.com/velle999/<pkgname>`, which is also where that
package's source releases live. Nothing in this script pushes;
`tools/publish-sources.sh` is what creates the repository, pushes it and
attaches the release.

⚠ `packaging/out/` is gitignored — it is regenerated from the PKGBUILDs, and
committing it would be a second copy of every one of them, free to go stale
against the one that is actually built. The repositories in it are real, though:
delete the directory and their local history goes with it, so push them
somewhere before relying on it.
