# syn-arcade's message catalogs, compiled TWICE — to JSON and to a .mo.
#
# ⛔ THE WINDOW CANNOT USE THE .mo. A .mo is read by libintl, which is reached
# from C; the window is QML running inside quickshell, which has no translator
# machinery at all. There is no installTranslator anywhere in the quickshell
# binary, so qsTr() compiles, returns the source text, and translates nothing
# while looking exactly like a marked string in review. tools/po2json.py
# compiles each catalog to a small JSON object and data/qml/I18n.qml reads it
# through a blocking FileView.
#
# ⛔ AND THE CLI CANNOT USE THE JSON. It is C and reads a .mo through libintl,
# and it is the front-end that has to keep working over SSH — half of what this
# tool does is answering questions about a machine you are not sitting at. So
# the SAME po/<lang>.po is compiled both ways: one catalog per language, two
# compiled forms, and a word the CLI and the windows share is translated once
# and cannot come out differently in each.
#
# ⚠ THE .pot IS NOT GENERATED HERE, AND NOT BY xgettext ALONE. xgettext cannot
# read QML — there is no --language=QML, and --language=JavaScript chokes on the
# object syntax and extracts a silent subset — so po/pot.sh runs
# tools/qml-xgettext.py over the QML, real xgettext over src/*.c, and msgcat
# merges the two. tests/i18n_test.sh fails when the checked-in template is stale.
#
# ⛔ REGENERATE WITH `ninja -C <build> syn-arcade-template`, **NOT** `syn-arcade-pot`.
# i18n.gettext() below defines <domain>-pot itself, and that one runs plain
# xgettext over po/POTFILES — a file that lists .qml, which xgettext cannot read.
# Running it overwrites the template with a half-empty one.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
sa_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    sa_linguas += l
  endif
endforeach

# ⚠ INSTALLED BESIDE I18n.qml, not under the datadir root. It finds its
# catalogs with Qt.resolvedUrl("i18n") against its own file — which is what lets
# one byte-identical copy of it serve every quickshell app in this project — so
# the JSON has to live under data/qml/, not somewhere a path would have to name.
#
# ⚠ AND ONE SET SERVES BOTH WINDOWS. The desktop window and big screen mode are
# separate quickshell processes; they import the same "qml" directory, so there
# is one catalog per language and not one per window.
foreach lang : sa_linguas
  custom_target('syn-arcade-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    install_dir : get_option('datadir') / 'syn-arcade' / 'qml' / 'i18n')
endforeach

# ⛔ AND THE SAME .po AGAIN, AS A .mo. The window is QML and reads the JSON
# above; the CLI is C and reads a .mo through libintl. ONE catalog per
# language, two compiled forms.
#
# ⛔ VIA meson's i18n MODULE, NOT A custom_target: libintl looks for
# <localedir>/<lang>/LC_MESSAGES/**syn-arcade**.mo — named after the DOMAIN — and
# a loop over the languages can only name its output de.mo, which installs to
# the right directory under a name nothing ever looks for. synpkg 47 shipped
# that mistake for an afternoon; i18n.gettext() knows the rule.
i18n = import('i18n')
i18n.gettext('syn-arcade', preset: 'glib', install: true)

# ⚠ THE TEMPLATE COMES FROM po/pot.sh, not the QML extractor alone — more than
# half these msgids are in the C. Its header carries the xgettext flag that
# silently mangles them.
run_target('syn-arcade-template',
  command : [find_program('pot.sh'),
             meson.project_source_root(),
             meson.current_source_dir()])
