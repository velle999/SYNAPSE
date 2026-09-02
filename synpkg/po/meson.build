# synpkg's message catalogs, compiled to JSON rather than to .mo.
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
# tests/i18n.sh fails when the checked-in template is stale. Regenerate with
# `ninja -C <build> synpkg-pot`.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
sp_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    sp_linguas += l
  endif
endforeach

# ⚠ INSTALLED BESIDE I18n.qml, not under the datadir root. It finds its
# catalogs with Qt.resolvedUrl("i18n") against its own file — which is what lets
# one byte-identical copy of it serve every quickshell app in this project — so
# the JSON has to live under data/qml/, not somewhere a path would have to name.
foreach lang : sp_linguas
  custom_target('synpkg-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    install_dir : get_option('datadir') / 'synpkg' / 'qml' / 'i18n')
endforeach

run_target('synpkg-pot',
  command : [find_program('../tools/qml-xgettext.py'),
             '--root',  meson.project_source_root() / 'data',
             '--files', meson.current_source_dir() / 'POTFILES',
             '-o',      meson.current_source_dir() / 'synpkg.pot',
             '--strict'])
