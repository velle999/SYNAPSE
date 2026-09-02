# The ANTIQUITY shell's message catalogs, compiled to JSON rather than to .mo.
#
# A third catalog directory, and the reason is that there is a third consumer,
# not a third mechanism. po/ is synui's own C, read by libintl. po-bar/ is the
# SYNAPSE bar's QML. This is the Antiquity bar's QML — a SECOND shell that
# ships beside it and is selected with `bar_shell = antiquity`. Only one of the
# two bars runs at a time, but synui's panels, lock screen and menus are on
# screen beside whichever one it is, so all three lists carry the same
# thirteen languages. See po-bar/meson.build for why the last mile is JSON.
#
# ⚠ THE .pot IS NOT GENERATED HERE, for the same reason it is not next door:
# xgettext cannot read QML. tools/qml-xgettext.py writes it and
# tests/i18n_antiquity.sh fails when the checked-in template is stale.
# Regenerate with `ninja -C <build> antiquity-pot`.

po2json_antiquity = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so.
antiquity_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    antiquity_linguas += l
  endif
endforeach

foreach lang : antiquity_linguas
  custom_target('antiquity-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json_antiquity, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    # ⛔ BESIDE THIS TREE'S OWN I18n.qml, not the bar's. Each copy finds its
    # catalogs with Qt.resolvedUrl("i18n") against its own file, which is what
    # lets one byte-identical singleton serve both shells and six other
    # packages without any of them depending on another.
    install_dir : synui_datadir / 'quickshell-antiquity' / 'i18n')
endforeach

run_target('antiquity-pot',
  command : [find_program('../tools/qml-xgettext.py'),
             '--root',  meson.project_source_root() / 'quickshell-antiquity',
             '--files', meson.current_source_dir() / 'POTFILES',
             '-o',      meson.current_source_dir() / 'synui-antiquity.pot',
             '--strict'])
