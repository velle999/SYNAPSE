pragma Singleton

import QtQuick

/* Util — the two helpers Omarchy's own widgets reach for. Reimplemented from
 * their behaviour rather than copied; both are a few lines and neither has a
 * SynapseOS opinion in it. */
QtObject {
    /* A plain `{}`, as opposed to an array, a QObject or null. Widgets use it to
     * tell a settings map from a list. */
    function isPlainObject(v) {
        return v !== null && typeof v === "object" && !Array.isArray(v)
    }

    /*
     * One argument, safe to paste into a shell command line.
     *
     * ⛔ SINGLE QUOTES AND AN ESCAPED SINGLE QUOTE, which is the only form that
     * is safe for EVERY byte: inside '…' the shell expands nothing at all, so a
     * value containing $, `, \ or a newline is inert. The `'\''` dance is how a
     * literal quote gets in. A double-quoted form would still expand $(…).
     *
     * A widget should be using argv rather than a shell at all; this exists
     * because theirs does, and a quoting helper that is subtly wrong is worse
     * than one that is absent.
     */
    function shellQuote(s) {
        return "'" + String(s).split("'").join("'\\''") + "'"
    }
}
