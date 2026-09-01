# The message catalogs. i18n.gettext() reads POTFILES and LINGUAS beside this
# file, compiles each .po with msgfmt, and installs the .mo under localedir —
# which is the path synui/src/i18n.c was given as SYNUI_LOCALEDIR.
#
# ⛔ EVERY LANGUAGE IN LINGUAS MUST HAVE A .po FILE OR MESON REFUSES TO
# CONFIGURE — "ERROR: File de.po does not exist". Adding a language is
# therefore two steps, not one: the line in LINGUAS and `msginit` for the file.
# An UNFINISHED .po is fine and is the normal state — msgstr "" prints the
# English — but an ABSENT one stops the build.
i18n.gettext('synui',
  args: [
    '--from-code=UTF-8',
    # N_() marks a string in a static table — declared before a locale exists,
    # looked up later at the point it is drawn. xgettext has to be told, or
    # every table entry in the UI is invisible to it. See src/i18n.h.
    '--keyword=_',
    '--keyword=N_',
    '--add-comments=TRANSLATORS:',
  ])
