# synstudio's message catalogs, compiled to JSON rather than to .mo.
#
# ⛔ NOT i18n.gettext(), AND NOT BECAUSE gettext IS WRONG. These are ordinary
# .po files and take msgmerge, msgfmt -c and tools/po-fill.py. What cannot
# happen is the LAST step: a .mo is read by libintl, which is reached from C,
# and this window is QML running inside quickshell — which has no translator
# machinery at all. There is no installTranslator anywhere in the quickshell
# binary, so qsTr() compiles, returns the source text, and translates nothing
# while looking exactly like a marked string in review.
#
# tools/po2json.py compiles each catalog to a small JSON object and
# data/qml/I18n.qml reads it through a blocking FileView.
#
# ⚠ THE .pot IS NOT GENERATED HERE. xgettext cannot read QML — there is no
# --language=QML, and --language=JavaScript chokes on the object syntax and
# extracts a silent subset — so tools/qml-xgettext.py writes it, and
# tests/i18n_test.sh fails when the checked-in template is stale. Regenerate with
# `ninja -C <build> synstudio-pot`.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
st_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    st_linguas += l
  endif
endforeach

# ⚠ INSTALLED BESIDE I18n.qml, not under the datadir root. It finds its
# catalogs with Qt.resolvedUrl("i18n") against its own file — which is what lets
# one byte-identical copy of it serve every quickshell app in this project — so
# the JSON has to live under data/qml/, not somewhere a path would have to name.
foreach lang : st_linguas
  custom_target('synstudio-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    install_dir : get_option('datadir') / 'synstudio' / 'qml' / 'i18n')
endforeach

# ⛔ TWO SOURCE LANGUAGES, ONE TEMPLATE. The window's own strings come from
# tools/qml-xgettext.py; the develop, clip and thumbnail panels are built from
# TABLES IN C (src/develop.c, src/timeline.c, src/thumb.c) whose group names and
# row labels travel over the record protocol and are drawn as they arrive. Those
# rows are marked N_() — see src/i18n.h for why there is no _() beside it — and
# real xgettext reads them.
#
# ⚠ THE C HALF IS WHAT MAKES THE WINDOW'S DYNAMIC LOOKUP SAFE. `I18n.tr(row.label)`
# is not a literal and tools/qml-xgettext.py cannot see it; it does not have to,
# because every string it can ever be handed is extracted from the tables here.
# Drop this half and the panels go quietly English with a full catalog.
#
# msgcat, not two catalogs: one .po per language and one JSON for the window.
run_target('synstudio-pot',
  command : [find_program('pot.sh'),
             meson.project_source_root(),
             meson.current_source_dir()])
