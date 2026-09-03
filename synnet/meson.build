project('synnet', 'c',
  version : '0.1.0',
  default_options : ['warning_level=2', 'c_std=gnu17'])

add_project_arguments(['-D_GNU_SOURCE', '-D_DEFAULT_SOURCE'], language: 'c')

conf = configuration_data()
conf.set_quoted('SYNNET_LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output : 'config.h', configuration : conf)

synnet_exe = executable('synnet',
  sources : [
    'src/main.c',
    'src/monitor.c',
  ],
  include_directories : include_directories('include', '.'),
  install : true)

# The firewall, and whether the box can find out it has one.
#
# synnet has carried a default-drop input firewall for a while and applied it at
# every start; `--status` reported "no synnet table loaded — daemon has not run"
# regardless, because listing an nftables object needs CAP_NET_ADMIN and any
# failure was read as absence. A firewalled box denied its own firewall.
#
# Needs no root: the published-state half is driven through
# $SYNNET_FW_STATE_FILE, and the ruleset half puts a stub `nft` on PATH and
# reads back what `--firewall` actually handed it — the recipe as cooked, not
# as written.
firewall_test = find_program('tests/firewall_test.sh')
test('firewall', firewall_test,
     args    : [synnet_exe.full_path()],
     depends : [synnet_exe],
     timeout : 60)

# ⛔ VIA meson's i18n MODULE, NOT A custom_target: libintl looks for
# <localedir>/<lang>/LC_MESSAGES/**synnet**.mo — named after the DOMAIN — and a
# loop over the languages can only name its output de.mo, which installs to the
# right directory under a name nothing ever looks for.
i18n = import('i18n')
i18n.gettext('synnet', preset : 'glib', install : true)

# ⛔ REGENERATE WITH `ninja -C <build> synnet-template`, NOT `synnet-pot`:
# i18n.gettext() defines that second name itself and drives it from po/POTFILES,
# which this component does not keep — po/pot.sh runs xgettext over src/*.c.
run_target('synnet-template',
  command : [find_program('po/pot.sh'), meson.project_source_root()])

# ⛔ THE ONE THAT PROVES THE JOURNAL, THE STATE FILE AND THE PROMPTS STAYED
# ENGLISH. Three destinations that are read by programs — or by a person
# following a search result — and none of them may move with the desktop's
# language. It runs --status under a catalog that translates EVERYTHING and
# diffs the state file and the nft script the daemon actually hands over.
test('i18n', find_program('tests/i18n_test.sh'),
     args    : [meson.current_source_dir(), synnet_exe.full_path()],
     depends : [synnet_exe],
     timeout : 120)
