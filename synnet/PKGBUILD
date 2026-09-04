pkgname=synnet
pkgver=0.1.0
# 6: THE FIREWALL WAS THERE AND THE STATUS COMMAND DENIED IT. synnet has
#   applied a default-drop input chain at every start since it grew
#   synnet_nft_ensure_firewall(), and `synnet --status` printed "no synnet table
#   loaded — daemon has not run" regardless: listing an nftables object needs
#   CAP_NET_ADMIN, so as an ordinary user that call ALWAYS failed, and the old
#   status read any failure as absence. It never mentioned the input firewall at
#   all either — only the egress block set. --status now makes three separate
#   claims: what synnet ASSERTED (from /run/synnet/firewall.state, which the
#   daemon publishes so an unprivileged answer is possible), what the KERNEL
#   holds (only when it can look, and it says so when it cannot), and the
#   blocklist.
#   ⚠ AND THE "SELF-HEALS A FLUSH" COMMENT WAS FALSE. The chain was built once
#   at start; `nft flush ruleset` from anything took it away until the next
#   daemon restart, with the journal still showing the firewall came up —
#   because it did, once. The monitor loop now re-checks once a minute and
#   rebuilds, logging a running count, since "this happened at boot" and "this
#   has happened forty times today" are different problems.
#   New: `synnet --firewall` applies it now, without restarting the daemon and
#   dropping the process event stream with it. tests/firewall_test.sh drives the
#   status states through $SYNNET_FW_STATE_FILE and reads the real ruleset back
#   out of a stub `nft`, so neither half needs root.
# 7: the firewall test assumed it was not root, and CI is a container that is.
#   Every "needs root" assertion failed there. The REGRESSION itself no longer
#   depends on privilege at all: the bug was "any nft failure is read as
#   absence", and a stub nft that exits non-zero produces that for either user.
#   Privilege now only decides which WORDING is expected, and those checks say
#   which case they are in. ⚠ It also found the same lie one level down —
#   `nft` failing as root was reported as "the input chain is NOT loaded in the
#   kernel", which is equally untrue when nft is simply not installed, as in a
#   container. --status checks for nft before drawing that conclusion.
# 8: the firewall gets an OFF switch, and it is remembered. Settings ▸ Network
#   can turn it off now, which needs more than deleting the chain: the daemon
#   re-checks once a minute and would put it straight back — the re-assert
#   undoing the user instead of the flush it exists for. `synnet --firewall
#   on|off` writes /etc/synnet/firewall and the daemon re-READS it every tick.
#   ⚠ ABSENT MEANS ON, and so does unreadable, empty or garbage: the one string
#   that disarms the box is the exact word `off`. A parser that failed open
#   would unfilter a machine because a disk filled up.
#   ⚠ Off deletes the input CHAIN only — never the table, never `flush ruleset`
#   — or turning off ingress filtering would silently unblock every address the
#   AI has flagged, which lives in the same table.
# 9: A CONTAINER BRIDGE GOT NO DHCP, AND THE FIREWALL WAS WHY. The LAN-trust
#   rule accepts anything from 10/8, 172.16/12 or 192.168/16, so a Waydroid or
#   libvirt guest is trusted the moment it HAS an address — and the packet that
#   asks for one is not, because a DHCPDISCOVER is sent from 0.0.0.0 to
#   255.255.255.255:67 and matches no accept in the chain. The drop policy ate
#   it, the guest came up with no network, and nothing anywhere said firewall.
#   ⚠ THE CONTAINER RUNTIME'S OWN RULE DOES NOT SAVE IT. waydroid-net.sh inserts
#   `iptables -I INPUT -i waydroid0 -p udp --dport 67 -j ACCEPT`, which lands in
#   a DIFFERENT nftables base chain. A packet traverses every base chain on the
#   hook; an accept in one only ends that chain, while our drop policy is final.
#   Two firewalls compose as the STRICTER of the two, never the looser.
#   New: /etc/synnet/trusted-ifaces (backup=), one bridge per line, plus
#   `synnet --trust-if` / `--untrust-if`, which apply immediately — the daemon's
#   re-assert tick only rebuilds a chain that has GONE, so a chain that is
#   merely out of date would have kept dropping DHCP until the next reboot.
#   ⚠ THE RULES ARE `iifname`, NEVER `iif`. `iif` resolves the name to an
#   ifindex at LOAD time and fails if the interface is absent — and these
#   bridges are created when the container starts, hours after this chain came
#   up at boot. The chain is one atomic `nft -f`, so a single `iif` naming an
#   absent interface would not lose that rule, it would lose THE FIREWALL.
#   ⚠ It is deliberately not ufw's `allow in on <iface>`. Trusting the interface
#   wholesale would open every host port to whatever the guest runs — an
#   arbitrary Android APK, for Waydroid — and would add nothing, since an
#   addressed guest is already trusted. The gateway services are the whole delta.
# ── 0.1.0-11: thirteen languages, and three things that are not words ────────
#
# Everything synnet prints on stdout is now in de, fr, es, pt, it, nl, pl, ru,
# ja, zh, ko, hi and ar — 39 strings, the whole of `--status` and every
# diagnostic the CLI gives.
#
# ⛔ AND THREE DESTINATIONS STAY ENGLISH, EACH FOR ITS OWN REASON.
#
#   · THE JOURNAL. Every syslog() line is unmarked. When the firewall fails to
#     load, syn-settings' network pane tells somebody "`journalctl -u synnet`
#     has what nft said" — that is the line they will read, paste into a search
#     and attach to a bug report. A journal that changed language with the
#     desktop is one nobody else can help with.
#
#   · THE STATE FILE. /run/synnet/firewall.state is key=value, and
#     syn-settings parses `state`, `links` and `reasserts` out of it to decide
#     whether this machine reports itself filtered. Text between two programs.
#
#   · THE AI PROMPTS. synnet asks synapd "Reply with just BLOCK or ALLOW" and
#     then matches on those two words. A translated prompt is a different
#     question, answered in a language nothing here reads.
#
#   And the nft script least of all — it is a program's input.
#
# ⛔ tests/i18n_test.sh RUNS THE FIREWALL UNDER A CATALOG THAT TRANSLATES
# EVERYTHING and diffs the ruleset the daemon actually hands `nft` and the
# state file it publishes. Both must be byte-identical; `--status` must not be.
# ⚠ It runs under fakeroot when there is no root, because `--firewall` refuses
# without it and returns having written nothing — two empty logs comparing
# equal is the shape of a check that tests nothing and says ok. Proved by
# marking the state-file writer and watching it fail.
# ⚠ `since=` is the one field excluded: it is the epoch second the firewall was
# asserted, and a diff that always fails is a diff nobody reads.
#
# ⚠ AND THE nft HALF IS A GUARD, NOT A DEMONSTRATION. Every line of that script
# concatenates literals with the SYNNET_NFT_* macros, and xgettext extracts only
# the FIRST literal of such a run — so a `_()` around one marks a msgid the
# runtime string can never equal, and gettext hands it straight back. Tried; it
# changed nothing. What the check catches is the first plain whole-literal
# fragment somebody adds, which is exactly when it starts mattering.
#
# ⚠ FOUR SENTENCES WERE ASSEMBLED FROM PIECES — trusted/untrusted,
# trusting/no-longer-trusting, accepted/no-longer-accepted and (up)/(not
# present yet) — and a word in a %s slot reaches every reader in English
# however the line around it is translated. Whole sentences per branch now. The
# two %s that stay English are the FLAG SPELLINGS in "sudo synnet --trust-if",
# which are what you type.
#
# ⛔ LC_NUMERIC IS PINNED TO C. Everything synnet composes with snprintf goes
# somewhere that is not a person: an nft ruleset, a key=value file a sibling
# parses, and a prompt whose answer is matched against two English words.
# 12: `--open` / `--close`, the verb that was missing.
#   ⛔ NOTHING COULD OPEN A PORT TO A SOURCE OUTSIDE THE PRIVATE RANGES, and the
#     knob that looks like it does is `--allow`, which is an UNBLOCK — it only
#     removes an address from the drop set `--block` fills. `--trust-if` is
#     DHCP+DNS on a gateway bridge and deliberately not `allow in on <iface>`.
#     So a mesh VPN was unusable and gave no clue why: Tailscale's 100.64.0.0/10
#     is not a private range, so the tunnel came up (established outbound, and
#     replies are accepted) and then every packet inside it hit the drop policy,
#     with nothing in any log mentioning the firewall.
#   ⚠ THE SOURCE IS NEVER OPTIONAL IN THE STORED FORM. A bare `tcp/5900` is
#     ignored with a note rather than read as `any`: "open to my VPN" and "open
#     to the internet" are one missing word apart, and a default would
#     eventually guess wrong in the direction that matters. `--open` with no
#     CIDR writes `any` in full AND says out loud what that means.
#   ⚠ Every value is validated before it reaches the ruleset — proto, port
#     range, and the CIDR's prefix length against its OWN family. The chain is
#     one atomic `nft -f`, so a rule nft refuses does not cost that rule, it
#     costs the whole firewall.
#   New: /etc/synnet/open-ports (backup=), `ports=` in the published state, and
#   a --status section that is printed even when empty — a section that
#   disappears when nothing is open says nothing at all.
pkgrel=12
pkgdesc="SynapseOS AI Network Policy Daemon"
arch=('x86_64')
license=('GPL-2.0-or-later')
depends=('nftables' 'synapd')
makedepends=('meson' 'ninja')
# ⚠ backup=, or every upgrade would overwrite the list of bridges the user has
# trusted — silently un-firewalling their containers on a routine syn-update.
backup=('etc/synnet/trusted-ifaces' 'etc/synnet/open-ports')
# ⛔ THE RELEASE URL, AND IT CARRIES THE pkgrel. The filename before `::` is
# what makepkg looks for on disk, so a build from this checkout uses the tarball
# build-all.sh just collected and never downloads. The URL after it is for
# everybody else, and it names <pkgver>-<pkgrel> because that tag is the only
# thing that makes a published source unambiguously the one this PKGBUILD was
# written against.
#
# ⛔ AND sha256sums STAYS 'SKIP' — a real checksum breaks every local build the
# moment the tree changes, which is every build that matters here.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/synnet-0.1.0"
    meson setup build --prefix=/usr
    ninja -C build
}

package() {
    cd "$srcdir/synnet-0.1.0"
    DESTDIR="$pkgdir" ninja -C build install
    install -Dm644 systemd/synnet.service "$pkgdir/usr/lib/systemd/system/synnet.service"
    # World-readable on purpose: `synnet --status` and the Settings ▸ Network
    # pane both report this list, and neither is something you should have to
    # sudo in order to read.
    install -Dm644 config/trusted-ifaces "$pkgdir/etc/synnet/trusted-ifaces"
    # Same reasoning, and more so: what is open is exactly the thing somebody
    # should be able to read without being root.
    install -Dm644 config/open-ports "$pkgdir/etc/synnet/open-ports"
}
