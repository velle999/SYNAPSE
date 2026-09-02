# syn-cal's message catalogs, compiled TWICE — to JSON and to a .mo.
#
# ⛔ THE WINDOW CANNOT USE THE .mo. A .mo is read by libintl, which is reached
# from C; the window is QML running inside quickshell, which has no translator
# machinery at all. There is no installTranslator anywhere in the quickshell
# binary, so qsTr() compiles, returns the source text, and translates nothing
# while looking exactly like a marked string in review. tools/po2json.py
# compiles each catalog to a small JSON object and data/qml/I18n.qml reads it
# through a blocking FileView.
#
# ⛔ AND THE CLI CANNOT USE THE JSON. syn-cal's terminal half is C and reads a
# .mo through libintl. So the SAME po/<lang>.po is compiled both ways: one
# catalog per language, two compiled forms, and a word both front-ends use is
# translated once and cannot come out differently in each.
#
# ⚠ THE .pot IS NOT GENERATED HERE, AND NOT BY xgettext ALONE. xgettext cannot
# read QML — there is no --language=QML, and --language=JavaScript chokes on the
# object syntax and extracts a silent subset — so po/pot.sh runs
# tools/qml-xgettext.py over the QML, real xgettext over src/*.c, and msgcat
# merges the two. tests/i18n_test.sh fails when the checked-in template is
# stale.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
sc_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    sc_linguas += l
  endif
endforeach

# ⚠ INSTALLED BESIDE I18n.qml, not under the datadir root. It finds its
# catalogs with Qt.resolvedUrl("i18n") against its own file — which is what lets
# one byte-identical copy of it serve every quickshell app in this project — so
# the JSON has to live under data/qml/, not somewhere a path would have to name.
foreach lang : sc_linguas
  custom_target('syn-cal-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    install_dir : get_option('datadir') / 'syn-cal' / 'qml' / 'i18n')
endforeach

# ⛔ VIA meson's i18n MODULE, NOT A custom_target: libintl looks for
# <localedir>/<lang>/LC_MESSAGES/**syn-cal**.mo — named after the DOMAIN — and a
# loop over the languages can only name its output de.mo, which installs to the
# right directory under a name nothing ever looks for, so every string falls
# back to English with nothing to say so. synpkg 47 shipped that mistake for an
# afternoon; i18n.gettext() knows the rule.
i18n = import('i18n')
i18n.gettext('syn-cal', preset: 'glib', install: true)

# ⚠ THE TEMPLATE COMES FROM BOTH LANGUAGES, so this target runs po/pot.sh —
# qml-xgettext.py over the QML, real xgettext over src/*.c, msgcat merging the
# two. Regenerate with `ninja -C <build> syn-cal-template`.
#
# ⛔ AND IT IS **NOT** CALLED syn-cal-pot, BECAUSE i18n.gettext() ABOVE ALREADY
# DEFINES THAT NAME and its version is wrong for this project. meson's
# <domain>-pot target runs plain xgettext over po/POTFILES — a file that lists
# .qml, which xgettext cannot read. Running it would overwrite the template with
# a half-empty one. Meson refuses the duplicate name outright, which is the only
# reason this was noticed rather than shipped; the name here says which one to
# reach for. tests/i18n_test.sh regenerates with pot.sh and fails on a stale
# template, so a wrong-target run does not survive review either.
run_target('syn-cal-template',
  command : [find_program('pot.sh'),
             meson.project_source_root(),
             meson.current_source_dir()])
