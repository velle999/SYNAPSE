#!/usr/bin/env python3
"""
po2json.py — compile a .po into the JSON catalog the bar reads.

    tools/po2json.py po-bar/de.po -o build/i18n/de.json

⛔ THE LAST MILE ONLY. The .po files are still the source of truth and still
take msgmerge, msgfmt -c and tools/po-fill.py; what changes is what the running
program reads. quickshell 0.3.1 has no translator machinery — no
installTranslator anywhere in the binary — so a .mo has nothing to load it, and
qsTr() would compile, return the source text, and translate nothing while
looking exactly like a marked string in review. quickshell/I18n.qml reads this
JSON through a FileView instead. See its header.

The shape:

    {"": {"language": "de", "nplurals": 2, "plural": "n != 1"},
     "Volume": "Lautstärke",
     "%1 update": ["%1 Aktualisierung", "%1 Aktualisierungen"]}

⚠ AN UNTRANSLATED ENTRY IS OMITTED, NOT WRITTEN EMPTY. I18n.tr() falls back to
the msgid it was passed, so a missing key and an empty string reach the same
answer — but the omission keeps a half-finished catalog small and makes
`jq 'length'` a completeness count rather than a file-size one.

⛔ AND A FUZZY ENTRY IS OMITTED TOO. msgmerge writes #, fuzzy when it MATCHED
A DIFFERENT STRING and guessed — that is a translation of a sentence somebody
edited, not of this one. msgfmt refuses them by default for the same reason;
shipping them would put a plausible wrong sentence on the bar, which is worse
than English because nothing looks broken.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import pathlib
import re
import sys


def unescape(s):
    out, i = [], 0
    simple = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\'}
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            out.append(simple.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def parse_po(text):
    """[(flags, {key: value})] — one dict per entry, values already joined."""
    entries = []
    cur, flags, key = {}, [], None
    for raw in text.splitlines() + ['']:
        line = raw.strip()
        if not line:
            if cur:
                entries.append((flags, cur))
            cur, flags, key = {}, [], None
            continue
        if line.startswith('#,'):
            flags += [f.strip() for f in line[2:].split(',')]
            continue
        if line.startswith('#'):
            continue
        m = re.match(r'^(msgid_plural|msgid|msgctxt|msgstr(?:\[\d+\])?)\s+(".*")$', line)
        if m:
            key = m.group(1)
            cur[key] = unescape(m.group(2)[1:-1])
            continue
        if line.startswith('"') and key is not None:
            cur[key] += unescape(line[1:-1])
    return entries


def header_field(header, name):
    for part in header.split('\n'):
        if part.lower().startswith(name.lower() + ':'):
            return part.split(':', 1)[1].strip()
    return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('po')
    ap.add_argument('-o', '--output', required=True)
    args = ap.parse_args()

    path = pathlib.Path(args.po)
    entries = parse_po(path.read_text(encoding='utf-8'))

    catalog, meta = {}, {'language': path.stem, 'nplurals': 2, 'plural': 'n != 1'}
    translated = 0

    for flags, e in entries:
        msgid = e.get('msgid')
        if msgid is None:
            continue

        if msgid == '' and 'msgstr' in e:
            # The header. Its Plural-Forms is the rule I18n.qml compiles.
            pf = header_field(e['msgstr'], 'Plural-Forms')
            n = re.search(r'nplurals\s*=\s*(\d+)', pf)
            r = re.search(r'plural\s*=\s*(.+?);?\s*$', pf)
            if n:
                meta['nplurals'] = int(n.group(1))
            if r:
                meta['plural'] = r.group(1).strip().rstrip(';')
            lang = header_field(e['msgstr'], 'Language')
            if lang:
                meta['language'] = lang
            continue

        if 'fuzzy' in flags:
            continue

        if 'msgid_plural' in e:
            forms = [e[k] for k in sorted(
                (k for k in e if k.startswith('msgstr[')),
                key=lambda k: int(k[7:-1]))]
            if forms and any(f for f in forms):
                catalog[msgid] = forms
                translated += 1
        else:
            if e.get('msgstr'):
                catalog[msgid] = e['msgstr']
                translated += 1

    out = {'': meta}
    out.update(catalog)

    dest = pathlib.Path(args.output)
    dest.parent.mkdir(parents=True, exist_ok=True)
    # ⚠ ensure_ascii=False: the catalogs ARE the non-ASCII, and \uXXXX escapes
    # would roughly triple a CJK or Arabic file for nothing — the FileView reads
    # UTF-8. separators without spaces for the same reason.
    dest.write_text(json.dumps(out, ensure_ascii=False, separators=(',', ':'),
                               sort_keys=True) + '\n', encoding='utf-8')
    print(f"po2json: {path.name} -> {dest.name}  "
          f"{translated} translated, {meta['nplurals']} plural form(s)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
