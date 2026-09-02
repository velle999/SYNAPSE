/*
 * i18n.h — marking the ENGINE's table labels for a translator.
 *
 * ⛔ THERE IS NO _() HERE, AND THAT IS THE DESIGN. synstudio's C side does not
 * look anything up: the develop, clip and thumbnail tables are emitted over the
 * record protocol as `key <TAB> value <TAB> … <TAB> group <TAB> label`, and both
 * the group and the label are KEYS as well as words —
 *
 *   - the window matches on the group (`modelData === "Basic"`, rowsIn(group)),
 *   - `synstudio develop set <key>` and every test parse the same records,
 *   - and a translated record would make the CLI's output depend on the locale,
 *     which is the bug pacman -Qi taught this project twice.
 *
 * So the record stays English and the WINDOW translates at the draw site, the
 * same split synui uses for a settings key beside its label. N_() exists purely
 * so xgettext can see the strings; it expands to nothing.
 *
 * ⚠ The msgids therefore land in synstudio's QML catalog, not a separate one:
 * po/meson.build runs xgettext over these tables and msgcat's the result into
 * the .pot that tools/qml-xgettext.py writes. One catalog, two source
 * languages, one JSON the window reads.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSTUDIO_I18N_H
#define SYNSTUDIO_I18N_H

#define N_(s) (s)

#endif /* SYNSTUDIO_I18N_H */
