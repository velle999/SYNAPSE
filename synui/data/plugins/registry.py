#!/usr/bin/env python3
"""
registry.py — omarchyplugins.com's catalogue, reduced to the widgets this
desktop can actually offer.

Reads their catalog.json on stdin and writes `synui-plugins`' catalogue TSV on
stdout. Nothing else in the plugin system speaks JSON: the shell script reads
tab-separated rows from a file, the window reads tab-separated rows from the
script, and this is the one place their shape becomes ours.

── WHY A FILTER AND NOT THE WHOLE LIST ─────────────────────────────────────

Their catalogue is every community plugin for Omarchy Quattro — around 1,200 of
them, and most are not bar widgets at all. An overlay, a service, a panel or a
whole shell suite has no host on synui: there is no place to put it and no
contract for it to keep. Listing one would be offering an install that cannot
end in a widget on screen, which is the single failure the plugin system is
built not to have.

So a row survives only if ALL of these hold, and each drop is counted and
reported on stderr so a filter that suddenly keeps nothing is visible rather
than silent:

  kind contains "Bar widget"   the only kind synui hosts
  status is "Available"        theirs; "Compatibility failed" is their own
                               harness saying it does not load
  installAvailable is true     "Manual setup" means the repository has its own
                               installer and `git clone` is not it
  repositoryLayout root-plugin one plugin at the repository root, which is the
                               shape `synui-plugins add <git-url>` fetches. A
                               monorepo needs a path this listing does not give
  sourceType is community      the built-ins are Omarchy's own shipped widgets;
                               ours are in catalogue.tsv, hand-tested, and a
                               second row for the same widget with a different
                               install path is two answers to one question

⚠ THAT IS EVERY INCOMPATIBILITY KNOWABLE FROM A LISTING, AND NO MORE. Whether a
widget reaches for Quickshell.Hyprland or a qs.Ui type synui does not ship is a
question about its source, and the answer arrives at install time from
`synui-plugins`' own refusal check — by name, with the import that did it. This
filter is not that check and must not pretend to be one: dropping a row here for
a guess would hide a widget that runs.

── The `trust` column ──────────────────────────────────────────────────────

`verified` and `unverified` are THEIR verification status, carried through
unchanged. Neither means the widget was loaded into a synui bar — only the
`shipped` rows in catalogue.tsv have been, and that distinction is the whole
reason the column exists rather than a badge that reads the same either way.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import json
import re
import sys

# The kind synui hosts. Their `kind` is a display string, so a widget that is
# also a service reads "Service + Bar widget" — a substring test, not equality.
HOSTABLE_KIND = "Bar widget"

# ⚠ THE URL REACHES `git clone`. It comes out of somebody else's JSON, so it is
# checked against what a git remote may look like rather than trusted: https
# only, no whitespace, no shell metacharacter, nothing that could be read as an
# option. A row whose URL fails this is dropped, not sanitised — rewriting an
# address quietly is how you clone something other than what was listed.
URL_OK = re.compile(r"\Ahttps://[A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%-]+\Z")

COLUMNS = ("id", "name", "description", "repo", "ref", "path", "base",
           "category", "tags", "author", "stars", "trust")


def clean(value):
    """One TSV field: no tab, no newline, no leading or trailing space.

    ⚠ A TAB IN A DESCRIPTION WOULD SHIFT EVERY COLUMN AFTER IT, and a
    description is third-party prose. Collapsing the whitespace is not
    cosmetic — it is what keeps the row parseable at all.
    """
    if value is None:
        return ""
    return re.sub(r"\s+", " ", str(value)).strip()


def repo_url(plugin):
    """The git URL, preferring the one their own install command names.

    `installCommand` is "omarchy plugin add <url> --enable", and that URL is the
    one they tested with. `repo` is the browsable repository page and is usually
    the same thing; it is the fallback rather than the first choice because a
    listing may point its command at a mirror or a fork.
    """
    for candidate in (plugin.get("installCommand") or "", plugin.get("repo") or ""):
        match = re.search(r"https://\S+", candidate)
        if not match:
            continue
        url = match.group(0).rstrip(".,;")
        if URL_OK.match(url):
            return url
    return ""


def main():
    try:
        catalog = json.load(sys.stdin)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        print("registry.py: not the catalogue JSON: %s" % exc, file=sys.stderr)
        return 1

    plugins = catalog.get("plugins")
    if not isinstance(plugins, list):
        print("registry.py: no `plugins` array — is that the right URL?",
              file=sys.stderr)
        return 1

    dropped = {"kind": 0, "status": 0, "install": 0, "layout": 0,
               "builtin": 0, "url": 0, "id": 0}
    rows = []
    for plugin in plugins:
        if not isinstance(plugin, dict):
            continue
        if HOSTABLE_KIND not in (plugin.get("kind") or ""):
            dropped["kind"] += 1
            continue
        if plugin.get("sourceType") != "community":
            dropped["builtin"] += 1
            continue
        if plugin.get("status") != "Available":
            dropped["status"] += 1
            continue
        if plugin.get("installAvailable") is not True:
            dropped["install"] += 1
            continue
        if plugin.get("repositoryLayout") != "root-plugin":
            dropped["layout"] += 1
            continue

        # ⚠ The id has to survive being a directory name and an awk key. A
        # plugin id is namespaced dots and dashes; anything else is not one.
        ident = clean(plugin.get("id"))
        if not ident or not re.match(r"\A[A-Za-z0-9][A-Za-z0-9._-]*\Z", ident):
            dropped["id"] += 1
            continue

        url = repo_url(plugin)
        if not url:
            dropped["url"] += 1
            continue

        stars = plugin.get("stars")
        tags = plugin.get("tags")
        rows.append({
            "id": ident,
            "name": clean(plugin.get("name")) or ident,
            "description": clean(plugin.get("description")),
            "repo": url,
            # Empty: clone the repository's default branch. A listing pins a
            # commit it validated, but a shallow clone of one plugin wants the
            # branch people are actually shipping from.
            "ref": "",
            # Empty path and base mean "the repository IS the plugin", which is
            # what root-plugin says. The sparse fetch is for catalogue.tsv's
            # rows, where one repository holds many widgets.
            "path": "",
            "base": "",
            "category": clean(plugin.get("category")),
            "tags": ",".join(clean(t) for t in tags) if isinstance(tags, list) else "",
            "author": clean(plugin.get("author")),
            "stars": str(stars) if isinstance(stars, int) else "",
            "trust": "verified" if plugin.get("verificationStatus") == "verified"
                     else "unverified",
        })

    # Most-starred first, then by name. `browse` prints a page at a time, so the
    # order here is what somebody who types nothing sees — and "what everybody
    # else installed" is a better first page than alphabetical.
    rows.sort(key=lambda r: (-int(r["stars"] or 0), r["name"].lower()))

    out = sys.stdout
    out.write("# " + "\t".join(COLUMNS) + "\n")
    for row in rows:
        out.write("\t".join(row[c] for c in COLUMNS) + "\n")

    print("registry.py: %d widget(s) kept; dropped %s" %
          (len(rows), ", ".join("%d %s" % (n, k) for k, n in dropped.items() if n)),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
