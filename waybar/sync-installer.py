#!/usr/bin/env python3
"""Re-splice the repo's waybar files into syn-install.sh's heredocs.

The repo copy is the source of truth; this pushes it into the installer. Run it
after editing network-menu.xml, then let
check-installer-sync.py (which build-all.sh runs) confirm the two agree.
"""

import pathlib
import sys

BASE = pathlib.Path(__file__).resolve().parent.parent
INSTALLER = BASE / "syn-install" / "syn-install.sh"

EMBEDDED = {
    "NETMENUEOF": BASE / "waybar" / "network-menu.xml",
}


def splice(text, marker, body):
    open_tag = f"<< '{marker}'\n"
    start = text.index(open_tag) + len(open_tag)
    end = text.index(f"\n{marker}\n", start) + 1
    return text[:start] + body + text[end:]


def main():
    text = INSTALLER.read_text(encoding="utf-8")
    for marker, path in EMBEDDED.items():
        text = splice(text, marker, path.read_text(encoding="utf-8"))
    INSTALLER.write_text(text, encoding="utf-8")
    print(f"synced {', '.join(EMBEDDED)} into {INSTALLER.relative_to(BASE)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
