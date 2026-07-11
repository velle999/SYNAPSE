#!/usr/bin/env python3
"""Fail if syn-install.sh's embedded waybar files have drifted from the repo's.

syn-install.sh writes the new user's ~/.config/waybar by heredoc, so it carries
its own copy of synapse-menu-gen.py and network-menu.xml. A comment asking the
next person to "keep this in sync" is not a mechanism — the copies drifted, and
the drift is what shipped a menu whose items launched the wrong application (the
generator could not parse a config.jsonc that had grown comments, failed
silently, and left the XML and menu-actions describing different app lists).

So the invariant gets a check. Run from build-all.sh; exits non-zero on drift.
"""

import pathlib
import sys

BASE = pathlib.Path(__file__).resolve().parent.parent
INSTALLER = BASE / "syn-install" / "syn-install.sh"

# heredoc marker -> repo file that must match it byte for byte
EMBEDDED = {
    "GENEOF": BASE / "waybar" / "synapse-menu-gen.py",
    "NETMENUEOF": BASE / "waybar" / "network-menu.xml",
}


def extract(text, marker):
    """The body of `<< 'MARKER' … MARKER`, or None if it isn't there."""
    open_tag = f"<< '{marker}'\n"
    try:
        start = text.index(open_tag) + len(open_tag)
        end = text.index(f"\n{marker}\n", start) + 1
    except ValueError:
        return None
    return text[start:end]


def main():
    text = INSTALLER.read_text(encoding="utf-8")
    drifted = []

    for marker, path in EMBEDDED.items():
        body = extract(text, marker)
        if body is None:
            drifted.append(f"{INSTALLER.name}: no <<'{marker}' heredoc found")
            continue
        want = path.read_text(encoding="utf-8")
        if body != want:
            drifted.append(
                f"{path.relative_to(BASE)} != the {marker} heredoc in "
                f"{INSTALLER.relative_to(BASE)}"
            )

    if drifted:
        print("waybar: installer copies have drifted from the repo:", file=sys.stderr)
        for d in drifted:
            print(f"  - {d}", file=sys.stderr)
        print(
            "\nThe installer writes these files verbatim into a new user's\n"
            "~/.config/waybar. Re-splice them (they must match byte for byte):\n"
            "  python3 waybar/sync-installer.py",
            file=sys.stderr,
        )
        return 1

    print("waybar: installer copies match the repo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
