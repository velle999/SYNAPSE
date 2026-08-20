project('synnet', 'c',
  version : '0.1.0',
  default_options : ['warning_level=2', 'c_std=gnu17'])

add_project_arguments(['-D_GNU_SOURCE', '-D_DEFAULT_SOURCE'], language: 'c')

synnet_exe = executable('synnet',
  sources : [
    'src/main.c',
    'src/monitor.c',
  ],
  include_directories : include_directories('include'),
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
