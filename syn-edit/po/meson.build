# syn-edit's message catalogs, compiled to JSON rather than to .mo.
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
# `ninja -C <build> syn-edit-pot`.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
se_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    se_linguas += l
  endif
endforeach

# ⚠ INSTALLED BESIDE I18n.qml, not under the datadir root. It finds its
# catalogs with Qt.resolvedUrl("i18n") against its own file — which is what lets
# one byte-identical copy of it serve every quickshell app in this project — so
# the JSON has to live under data/qml/, not somewhere a path would have to name.
foreach lang : se_linguas
  custom_target('syn-edit-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    install_dir : get_option('datadir') / 'syn-edit' / 'qml' / 'i18n')
endforeach

# ⛔ AND THE SAME .po AGAIN, AS A .mo. The window is QML and reads the JSON
# above; the CLI, the TUI and `syn-edit serve` are C and read a .mo through
# libintl. ONE catalog per language, two compiled forms.
#
# ⛔ VIA meson's i18n MODULE, NOT A custom_target: libintl looks for
# <localedir>/<lang>/LC_MESSAGES/**syn-edit**.mo — named after the DOMAIN — and
# a loop over the languages can only name its output de.mo, which installs to
# the right directory under a name nothing ever looks for. synpkg 47 shipped
# that mistake for an afternoon; i18n.gettext() knows the rule.
i18n = import('i18n')
i18n.gettext('syn-edit', preset: 'glib', install: true)

# ⚠ THE TEMPLATE COMES FROM po/pot.sh, not the QML extractor alone — more than
# half these msgids are in the C. Its header carries the xgettext flag that
# silently mangles them.
run_target('syn-edit-template',
  command : [find_program('pot.sh'),
             meson.project_source_root(),
             meson.current_source_dir()])
