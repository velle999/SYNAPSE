# The BAR's message catalogs, compiled to JSON rather than to .mo.
#
# ⛔ NOT i18n.gettext(), AND NOT BECAUSE gettext IS WRONG. The .po files here
# are ordinary gettext catalogs and take msgmerge, msgfmt -c and
# tools/po-fill.py exactly like po/ next door. What cannot happen is the LAST
# step: a .mo is read by libintl, which is reached from C, and the bar is QML
# running inside quickshell — which has no translator machinery at all. There
# is no installTranslator anywhere in the quickshell binary, so qsTr() compiles,
# returns the source text, and translates nothing while looking exactly like a
# marked string in review.
#
# So tools/po2json.py compiles each catalog to a small JSON object and
# quickshell/I18n.qml reads it through a blocking FileView. See its header for
# why the read has to be synchronous.
#
# ⚠ THE .pot IS NOT GENERATED HERE. xgettext cannot read QML — there is no
# --language=QML, and --language=JavaScript chokes on the object syntax and
# extracts a silent subset — so tools/qml-xgettext.py writes it, and
# tests/i18n_bar.sh fails when the checked-in template is stale. Regenerate
# with `ninja -C <build> bar-pot`.

po2json = find_program('../tools/po2json.py')

# ⛔ READ FROM LINGUAS, NEVER TYPED HERE. A language added to that file and not
# to a list in this one would build, install, and be English forever with
# nothing to say so. tests/i18n_bar.sh also fails when po-bar/LINGUAS and
# po/LINGUAS disagree — a German control panel beside an English bar reads as
# the bar being broken.
bar_linguas = []
foreach line : run_command('cat', meson.current_source_dir() / 'LINGUAS',
                           check : true).stdout().strip().split('\n')
  l = line.strip()
  if l != '' and not l.startswith('#')
    bar_linguas += l
  endif
endforeach

bar_catalogs = []
foreach lang : bar_linguas
  bar_catalogs += custom_target('bar-i18n-' + lang,
    input   : lang + '.po',
    output  : lang + '.json',
    command : [po2json, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default : true,
    install : true,
    # ⛔ BESIDE I18n.qml, not in a sibling directory. It finds its catalogs with
    # Qt.resolvedUrl("i18n") against its own file — that is what lets one
    # byte-identical copy of it serve eight separate packages — so the JSON has
    # to live under the QML tree, not under the datadir root.
    install_dir : synui_datadir / 'quickshell' / 'i18n')
endforeach

# The template, for `ninja bar-pot` after marking new strings. A run_target and
# not a custom_target: it writes back into the SOURCE tree, which is where the
# .pot is reviewed, translated against and committed from.
run_target('bar-pot',
  command : [find_program('../tools/qml-xgettext.py'),
             '--root',  meson.project_source_root() / 'quickshell',
             '--files', meson.current_source_dir() / 'POTFILES',
             '-o',      meson.current_source_dir() / 'synui-bar.pot',
             '--strict'])
