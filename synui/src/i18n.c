/*
 * i18n.c — bind the message catalogs. See i18n.h for the two macros and the
 * one rule about which strings may be translated.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <libintl.h>
#include <locale.h>

#include "i18n.h"

#ifndef SYNUI_LOCALEDIR
#define SYNUI_LOCALEDIR "/usr/share/locale"
#endif

void synui_i18n_init(void)
{
    bindtextdomain(SYNUI_GETTEXT_DOMAIN, SYNUI_LOCALEDIR);
    /*
     * ⚠ UTF-8 REGARDLESS OF THE LOCALE'S OWN CHARSET. Every string here ends
     * up in cairo through pango/FreeType, which take UTF-8 and nothing else.
     * Without this libintl converts the catalog to the locale's codeset, and a
     * non-UTF-8 locale — still legal, still installable — would hand the
     * renderer bytes it draws as replacement boxes. The catalogs are UTF-8;
     * say so and skip the conversion.
     */
    bind_textdomain_codeset(SYNUI_GETTEXT_DOMAIN, "UTF-8");
    textdomain(SYNUI_GETTEXT_DOMAIN);
}
