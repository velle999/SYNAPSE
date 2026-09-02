pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * I18n — the bar's words, in the user's language.
 *
 * ⛔ THIS EXISTS BECAUSE quickshell HAS NO TRANSLATOR MACHINERY AND qsTr() IS A
 * TRAP. Qt's translation path needs someone to call QTranslator::load() and
 * QCoreApplication::installTranslator() before the QML engine starts; quickshell
 * 0.3.1 does neither — there is no installTranslator anywhere in the binary and
 * no way to reach one from QML. qsTr() still COMPILES, still returns the source
 * text, and still looks exactly like a translated string in the diff. Marking
 * the bar up with qsTr() would therefore be a whole day's work that ships an
 * English bar and reports success. Checked, not assumed: see tests/i18n_bar.sh,
 * which fails on a qsTr( anywhere under quickshell/.
 *
 * So the catalog is carried as JSON and looked up here. The .po files in
 * po-bar/ are still the source of truth and still take msgmerge, msgfmt and
 * tools/po-fill.py — only the last mile changes, from .mo read by libintl to
 * .json read by a FileView. tools/po2json.py is that mile, run at build time.
 *
 * ── Why the lookup is a function and not a property ────────────────────────
 *
 * A QML binding re-evaluates when something it reads changes. The catalog is
 * loaded once, before the first surface is built, and never changes for the
 * life of the process — the language is fixed at login, exactly as it is for
 * the compositor's own gettext. So `I18n.tr("Volume")` is a plain function call
 * whose answer is stable, and nothing has to invalidate.
 *
 * ⚠ WHICH ALSO MEANS THE READ HAS TO BE SYNCHRONOUS. A FileView is async by
 * default: `onLoaded` arrives a turn later, and by then every label in the bar
 * has already been laid out from the English it got on the first pass. There is
 * no signal to rebind to, because tr() is not a binding. `blockLoading: true`
 * plus a text() call inside the loader is the whole reason this works — the
 * same reason PluginConfig blocks for its config file.
 *
 * ── The failure mode is English, always ────────────────────────────────────
 *
 * No catalog for the language, no file at all, malformed JSON, a msgid that is
 * not in the catalog, an empty msgstr: every one of them returns the English
 * that was passed in. That is the correct failure for a shell — a bar with one
 * untranslated label is a bar; a bar that throws on a missing key is not.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
QtObject {
    id: root

    /*
     * Where the catalogs are: the `i18n/` directory BESIDE THIS FILE.
     *
     * ⛔ RESOLVED FROM THE FILE'S OWN URL, AND THAT IS WHAT MAKES THIS FILE
     * COPYABLE. Eight quickshell trees in this project need translating — the
     * bar, Antiquity, synfiles, synstudio, synpkg, syn-settings, syn-edit,
     * syn-cal — and they are separate PACKAGES: an app that imported the bar's
     * copy would depend on synui, and these all install on plain Arch without
     * it. So each tree carries a copy, and the only way copies do not drift is
     * for them to be BYTE-IDENTICAL. A hardcoded "/usr/share/synui/i18n" would
     * have made every one of them different in exactly one line, which is the
     * line nobody reads.
     *
     * Qt.resolvedUrl() inside a singleton resolves against the singleton's own
     * file, so this answers <wherever I am>/i18n whether that is the source
     * tree, the build directory or /usr/share/<app>/. Checked, not assumed.
     *
     * ⚠ THE file:// PREFIX HAS TO COME OFF. FileView takes a filesystem path,
     * not a URL, and a path beginning "file://" is simply a file that does not
     * exist — which fails the way a missing catalog does, silently and in
     * English.
     *
     * SYN_I18N_DIR overrides it, for the test rigs and for running an app out
     * of a tree whose catalogs were generated into a build directory. One name
     * for every app, because the file is the same file.
     */
    readonly property string catalogDir: Quickshell.env("SYN_I18N_DIR")
        || String(Qt.resolvedUrl("i18n")).replace(/^file:\/\//, "")

    /*
     * The language, as a bare two-letter code, or "" for English.
     *
     * ⚠ THE PRECEDENCE IS glibc's, NOT A SIMPLIFICATION OF IT. The compositor
     * beside this process resolves its own catalog through setlocale() and
     * libintl, and a bar that answered in English on a machine whose panels
     * spoke German would read as the bar being broken rather than as two
     * different rules. LANGUAGE wins over everything and is a colon-separated
     * PREFERENCE LIST — `LANGUAGE=de:en` means German, then English — and it is
     * ignored when the locale is C/POSIX, which is glibc's own rule and the
     * reason `LANG=C LANGUAGE=de` is English and not German.
     */
    readonly property string language: {
        const messages = Quickshell.env("LC_ALL")
                      || Quickshell.env("LC_MESSAGES")
                      || Quickshell.env("LANG")
                      || ""
        const base = root.bareCode(messages)
        // C and POSIX are "no translation", and they veto LANGUAGE too.
        if (base === "c" || base === "posix" || base === "") return ""

        const preference = Quickshell.env("LANGUAGE") || ""
        for (const entry of preference.split(":")) {
            const code = root.bareCode(entry)
            if (code === "") continue
            if (code === "c" || code === "posix" || code === "en") return ""
            return code
        }
        return base === "en" ? "" : base
    }

    /*
     * "de_DE.UTF-8@euro" -> "de". Everything after the first _ . or @ is the
     * territory, the codeset and the modifier, none of which name a catalog
     * here: po-bar/LINGUAS is thirteen bare codes, the same thirteen the
     * compositor and syn-install carry.
     *
     * ⚠ IT LOWERCASES. `LANG=DE_de` is unusual and legal, and a lookup for a
     * "DE.json" that is not on disk is silently English.
     */
    function bareCode(value: string): string {
        return String(value).split(/[_.@]/)[0].toLowerCase()
    }

    /*
     * ⛔ NOT `path: catalogDir + "/" + language + ".json"` WITH language "".
     * An empty language would ask for "/usr/share/synui/i18n/.json", which is a
     * miss the FileView would report as an error on every English desktop —
     * i.e. on most of them. English never touches the file at all: `table` is
     * the empty object and every lookup falls through to its argument.
     */
    readonly property string catalogPath:
        root.language === "" ? "" : root.catalogDir + "/" + root.language + ".json"

    property FileView catalogFile: FileView {
        // Left unbound and set by load(): binding it to catalogPath would start
        // an async read the moment this object is built, and the answer has to
        // be in hand before the first tr() call, not a turn after it.
        blockLoading: true
        printErrors: false
    }

    /*
     * msgid -> msgstr, or msgid -> [form, form, …] for a counted string.
     * Empty until load() runs and empty forever on an English desktop.
     */
    property var table: ({})

    // How many plural forms the language has, and which one a count selects.
    // The defaults are English's, which is also every catalog's fallback if its
    // header could not be read.
    property int nplurals: 2
    property var pluralIndex: function (n) { return n === 1 ? 0 : 1 }

    property bool loaded: false

    /*
     * Read the catalog. Called from Component.onCompleted AND from the first
     * tr() that arrives before it — a singleton is built the first time it is
     * touched, and that can be from inside another singleton's initialisation,
     * which runs before any onCompleted anywhere.
     */
    function load() {
        if (root.loaded) return
        root.loaded = true
        if (root.catalogPath === "") return

        root.catalogFile.path = root.catalogPath
        const text = root.catalogFile.text()
        if (!text) return

        let parsed
        try {
            parsed = JSON.parse(text)
        } catch (e) {
            // A corrupt catalog is a packaging bug, and one worth saying out
            // loud: it is the only failure here that is not also a normal state.
            console.warn("I18n: cannot parse " + root.catalogPath + ": " + e)
            return
        }
        if (!parsed || typeof parsed !== "object") return

        root.table = parsed
        root.readPluralRule(parsed[""])
    }

    /*
     * The "" entry is the .po header, carried across by po2json: the language
     * it is for, how many plural forms it has, and the C expression that picks
     * one. Every field is optional and a missing one leaves English's rule.
     *
     * ⚠ THE EXPRESSION IS COMPILED ONCE, HERE, and not evaluated per call. It
     * is gettext's own Plural-Forms text — `n%10==1 && n%100!=11 ? 0 : …` for
     * Russian — which is valid JavaScript for every rule the thirteen catalogs
     * use, because the operators gettext allows are a subset of C's that
     * JavaScript spells identically. A rule that somehow is not leaves the
     * default in place rather than throwing on every counted string in the bar.
     */
    function readPluralRule(header) {
        if (!header || typeof header !== "object") return
        if (header.nplurals > 0) root.nplurals = header.nplurals
        if (!header.plural) return
        try {
            /*
             * ⚠ `new Function` IS THE POINT, AND IT IS COMPILED ONCE. qmllint
             * flags it as a literal-constructor and is right to in general;
             * here the alternative is a plural-rule interpreter written by
             * hand, to evaluate an expression gettext already writes in a
             * syntax JavaScript reads. The input is a package-owned file under
             * /usr/share, generated by tools/po2json.py from our own .po — not
             * user data — and this runs once per process, not per string.
             */
            // qmllint disable literal-constructor
            const rule = new Function("n", "return (" + header.plural + ")")
            // qmllint enable literal-constructor
            // Exercised before it is trusted: a rule that throws or answers
            // out of range would otherwise fail at the first counted string.
            const probe = Number(rule(1))
            if (!(probe >= 0 && probe < root.nplurals)) throw new Error("out of range")
            root.pluralIndex = function (n) {
                const i = Number(rule(n))
                return (i >= 0 && i < root.nplurals) ? i : 0
            }
        } catch (e) {
            console.warn("I18n: unusable Plural-Forms in " + root.catalogPath + ": " + e)
        }
    }

    /*
     * tr(s) — the string, translated if the catalog has it.
     *
     * ⚠ THE ARGUMENT MUST BE A PLAIN LITERAL. tools/qml-xgettext.py reads the
     * source, not the running program, so `I18n.tr(someVariable)` extracts
     * nothing and translates nothing — it is the QML shape of the N_() trap
     * i18n.h documents. tests/i18n_bar.sh fails on a tr() whose argument is not
     * a literal, because the alternative is a string that looks marked and is
     * English in all thirteen languages.
     */
    function tr(s: string): string {
        if (!root.loaded) root.load()
        const hit = root.table[s]
        if (typeof hit === "string" && hit !== "") return hit
        // A counted msgid reached through tr() rather than trn(): answer with
        // the singular form rather than "[object Array]".
        if (Array.isArray(hit) && typeof hit[0] === "string" && hit[0] !== "") return hit[0]
        return s
    }

    /*
     * trn(singular, plural, n) — a string that counts something. ngettext, and
     * the same argument i18n.h makes for P_(): "%1 window" + (n === 1 ? "" : "s")
     * is English grammar written into the format string and no catalog can
     * reach it. German would read "3 Fensters"; Polish and Russian choose
     * between three forms by the last two digits and Arabic between six.
     *
     * ⚠ THE COUNT IS PASSED TWICE at the call site — once here to choose the
     * form, once to .arg() to be printed. Both English forms carry the %1.
     */
    function trn(singular: string, plural: string, n: int): string {
        if (!root.loaded) root.load()
        const hit = root.table[singular]
        if (Array.isArray(hit)) {
            const form = hit[root.pluralIndex(n)]
            if (typeof form === "string" && form !== "") return form
        }
        return n === 1 ? singular : plural
    }

    Component.onCompleted: root.load()
}
