#!/usr/bin/env bash
# syn-settings — smoke tests.
#
# These check the CONTRACT, not the values: a machine with no NVIDIA, no
# localectl or no compositor must still produce a well-formed table, because
# the whole design rests on "a missing tool is a value, not a failure". A test
# that asserted "keymap is us" would pass here and fail on the ISO.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

BIN=${1:?usage: syn_settings_test.sh /path/to/syn-settings}
fails=0

ok()   { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
# Named, not silent: a check that does not apply on this machine has to say so,
# or a suite that quietly stopped covering something still prints all green.
skip() { printf '  --   %s\n' "$1"; }

# `((n++))` evaluates to the OLD value, so it returns 1 the first time and
# kills the script under `set -e`. Hence fails=$((fails + 1)) above.

check_table() {
    local pane=$1 out ncols nrows line
    out=$("$BIN" --rec "$pane") || { bad "$pane: exited non-zero"; return; }

    [ -n "$out" ] || { bad "$pane: printed nothing"; return; }

    ncols=$(head -1 <<<"$out" | awk -F'\t' '{print NF}')
    [ "$ncols" -ge 2 ] || { bad "$pane: header has $ncols columns"; return; }
    ok "$pane: header has $ncols columns"

    # Every row must have exactly as many fields as the header. This is the
    # test that catches an unescaped tab in a value, which would silently
    # shift every column to its right rather than fail.
    nrows=0
    while IFS= read -r line; do
        nrows=$((nrows + 1))
        local n
        n=$(awk -F'\t' '{print NF}' <<<"$line")
        if [ "$n" -ne "$ncols" ]; then
            bad "$pane: row $nrows has $n fields, header has $ncols"
            return
        fi
    done < <(tail -n +2 <<<"$out")
    ok "$pane: $nrows rows, all $ncols fields wide"
}

echo "syn-settings smoke tests"

for pane in display region network bluetooth power kernel system; do
    check_table "$pane"
done

# The `action` column is the contract between the C reader and the GUI: the
# window decides what is editable purely from it. A verb the QML does not know
# renders a dead button, and a malformed one renders a button that builds a
# command nobody validated — so the shape is checked here rather than
# discovered by clicking.
check_actions() {
    local pane=$1 out col line a
    out=$("$BIN" --rec "$pane") || { bad "$pane: exited non-zero"; return; }
    col=$(head -1 <<<"$out" | awk -F'\t' '{for(i=1;i<=NF;i++) if($i=="action") print i}')
    [ -n "$col" ] || { ok "$pane: no action column (read-only pane)"; return; }

    while IFS= read -r line; do
        a=$(awk -F'\t' -v c="$col" '{print $c}' <<<"$line")
        case "$a" in
            -|set:*|toggle:*|unit:*|probe:*|mode:*|pkg:*|device:*|boot:*) ;;
            *) bad "$pane: unknown action verb '$a'"; return ;;
        esac
        # A verb with an empty argument is the one that looks fine in a table
        # and builds `syn-settings set  <value>` when clicked.
        case "$a" in
            *:) bad "$pane: action '$a' has no argument"; return ;;
        esac
    done < <(tail -n +2 <<<"$out")
    ok "$pane: every action is a known verb with an argument"
}

for pane in display region network bluetooth power kernel; do
    check_actions "$pane"
done

# An unknown pane must be refused, not silently empty.
if "$BIN" --rec nonesuch >/dev/null 2>&1; then
    bad "unknown pane was accepted"
else
    ok "unknown pane refused"
fi

# --help must work and must not need a display.
if "$BIN" --help | grep -q 'syn-settings'; then
    ok "--help prints usage"
else
    bad "--help printed nothing useful"
fi

# Writes must never run during a test. --dry-run is the guarantee, so it is
# itself tested: if this ever stopped being honoured the suite would start
# changing the machine it runs on.
if "$BIN" --dry-run set timezone UTC | grep -q '^would run: timedatectl set-timezone UTC$'; then
    ok "--dry-run shows the command and runs nothing"
else
    bad "--dry-run did not report the expected command"
fi

# Argument validation: a value that could be read as a flag must be refused
# before it reaches a tool that would honour it.
for bad_val in "--adjust" "a;b" 'x$(id)' "back\`tick\`"; do
    if "$BIN" --dry-run set timezone "$bad_val" >/dev/null 2>&1; then
        bad "accepted hostile value: $bad_val"
    else
        ok "refused: $bad_val"
    fi
done

# pkg is restricted to the kernels this pane manages. It must never become a
# general "install anything by name" endpoint reachable from a GUI row.
for evil in "firefox" "base" "sudo" "linux-lts-evil"; do
    if "$BIN" --dry-run pkg install "$evil" >/dev/null 2>&1; then
        bad "pkg accepted a package outside the kernel list: $evil"
    else
        ok "pkg refused: $evil"
    fi
done
if "$BIN" --dry-run pkg install linux-lts | grep -q 'synpkg .*install linux-lts linux-lts-headers'; then
    ok "pkg install pulls the matching headers"
else
    bad "pkg install did not include headers"
fi

# ⚠ --noconfirm is LOAD-BEARING, not cosmetic. synpkg's confirm() refuses when
# stdin is not a terminal, and this binary has no terminal — so without the
# flag every install authenticated through polkit and then declined itself,
# exiting 0 with nothing installed. That was the Kernel pane's install button
# for its whole life, and nothing failed loudly enough to catch it.
if "$BIN" --dry-run pkg install linux-zen | grep -q -- '--noconfirm'; then
    ok "pkg install passes --noconfirm"
else
    bad "pkg install dropped --noconfirm — it will silently install nothing"
fi

# --verbose is what makes the DOWNLOAD visible. synpkg names each file only
# once it has arrived and only when asked; a kernel is a couple of hundred
# megabytes, so without this the longest stretch of the operation reports
# nothing and the window looks hung — which is how this was reported.
if "$BIN" --dry-run pkg install linux-zen | grep -q -- '--verbose'; then
    ok "pkg install asks synpkg to say what it is doing"
else
    bad "pkg install dropped --verbose — the download will be silent"
fi

# A CachyOS kernel is not in any Arch repository, so the repo has to be added
# BEFORE the install, or pacman reports "not found" after the password prompt.
#
# ⚠ BOTH branches are pinned. A machine is only ever in one of them, and the
# interesting one — repo absent — stops being reachable the moment the repo is
# enabled, which is the state every machine ends up in. Read from the live
# answer this check would have quietly rotted into a no-op.
out=$(SYN_SETTINGS_CACHYOS_ENABLED=0 "$BIN" --dry-run pkg install linux-cachyos)
if printf '%s' "$out" | grep -q 'synpkg cachyos enable-repo'; then
    ok "repo absent: installing a Cachy kernel enables it first"
else
    bad "repo absent: installing a Cachy kernel did not enable the CachyOS repo"
fi
if printf '%s' "$out" | grep -q 'install linux-cachyos linux-cachyos-headers'; then
    ok "the Cachy kernel still pulls its headers"
else
    bad "the Cachy kernel did not pull its headers"
fi

# Already enabled: enable-repo needs root, so running it anyway would be a
# second password prompt before every install, to be told "already enabled".
out=$(SYN_SETTINGS_CACHYOS_ENABLED=1 "$BIN" --dry-run pkg install linux-cachyos)
if printf '%s' "$out" | grep -q 'enable-repo'; then
    bad "repo present: installing prompted for a redundant enable-repo"
else
    ok "repo present: installing does not re-enable the repo"
fi
if printf '%s' "$out" | grep -q 'install linux-cachyos linux-cachyos-headers'; then
    ok "repo present: the install still runs"
else
    bad "repo present: the install did not run"
fi

# ── Progress records ────────────────────────────────────────────────────────
#
# Installing a kernel takes minutes. It used to run with its output on
# /dev/null, so the window showed a dimmed button and a status line frozen at
# the moment of the click — reported, twice in one sitting, as a hung app.
#
# The fix is that everything the child says is re-emitted as "progress<TAB>…"
# on our stdout, flushed per line. This drives the whole path with a STUB
# synpkg first on PATH, so the contract is checked without installing a kernel
# on the machine running the tests.
#
# What is checked is the part that is easy to get wrong and impossible to see:
# that a '\r' redraw — which is how pacman-shaped progress arrives, with no
# newline from beginning to end — becomes one record per update rather than
# one enormous record at the end.
stubdir=$(mktemp -d)
trap 'rm -rf "$stubdir"' EXIT
cat > "$stubdir/synpkg" <<'STUB'
#!/bin/sh
printf ':: synchronising package databases\n' >&2
printf '\r(1/2) linux-lts   7%%' >&2
printf '\r(1/2) linux-lts 100%%' >&2
printf '\n' >&2
exit 0
STUB
chmod +x "$stubdir/synpkg"

stream=$(PATH="$stubdir:$PATH" "$BIN" pkg install linux-lts 2>/dev/null)
n=$(printf '%s\n' "$stream" | grep -c '^progress	' || true)
if [ "$n" -eq 3 ]; then
    ok "a write streams one progress record per line, carriage returns included"
else
    bad "expected 3 progress records from the stub, got $n"
fi
if printf '%s\n' "$stream" | grep -q '^progress	(1/2) linux-lts 100%$'; then
    ok "the record carries the percentage the GUI parses"
else
    bad "the percentage line did not survive as its own record"
fi
# An escape sequence reaching the status bar would print as literal "[0m".
printf '#!/bin/sh\nprintf "\\033[2m::\\033[0m coloured\\n" >&2\n' > "$stubdir/synpkg"
if PATH="$stubdir:$PATH" "$BIN" pkg install linux-lts 2>/dev/null \
     | grep -q '^progress	:: coloured$'; then
    ok "colour is stripped out of a progress record"
else
    bad "an ANSI escape survived into a progress record"
fi
rm -rf "$stubdir"
trap - EXIT

# An Arch kernel must NOT drag the CachyOS repo onto the machine.
if "$BIN" --dry-run pkg install linux-zen | grep -q 'cachyos'; then
    bad "installing an Arch kernel touched the CachyOS repo"
else
    ok "an Arch kernel does not enable the CachyOS repo"
fi

# Neither must REMOVE, in either direction. A button labelled Remove that
# configures a new repository would be a genuinely surprising thing.
if "$BIN" --dry-run pkg remove linux-cachyos | grep -q 'enable-repo'; then
    bad "removing a Cachy kernel enabled a repository"
else
    ok "remove never enables a repository"
fi

# A mode string reaches a command line; anything that is not WxH[@R] is refused
# here with a readable message rather than by wlr-randr with a usage dump.
#
# Checked by EXIT CODE, not merely non-zero. On a machine without wlr-randr
# every one of these fails whatever the argument is, so "it failed" would be a
# test that passes for the wrong reason and would keep passing if the
# validation were deleted. 2 is "refused the argument", 1 is "tool missing".
for m in "--output" "1920" "abcxdef" "1920x1080; reboot" ""; do
    # `|| rc=$?` and not a bare call: under `set -e` a non-zero exit that is
    # not part of a conditional kills the script, and every one of these is
    # SUPPOSED to be non-zero. The first version of this loop ended the suite
    # silently at the first case it was testing for.
    rc=0
    "$BIN" --dry-run mode DP-1 "$m" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 2 ]; then
        ok "mode refused (exit 2): ${m:-<empty>}"
    else
        bad "mode did not refuse '${m:-<empty>}' as a bad argument (exit $rc)"
    fi
done

# And the converse: a WELL-FORMED mode must get PAST validation. Without this
# the four checks above would still pass if sane_mode() rejected everything.
rc=0
"$BIN" --dry-run mode DP-1 1920x1080@60 >/dev/null 2>&1 || rc=$?
case "$rc" in
    0) ok "a valid mode passes validation (wlr-randr present)" ;;
    1) ok "a valid mode passes validation (stops at missing wlr-randr)" ;;
    *) bad "a valid mode was refused as a bad argument (exit $rc)" ;;
esac

# Loopback must be refused before nmcli ever sees it: taking lo down breaks
# every daemon on the machine that talks to itself over a UNIX socket.
if "$BIN" --dry-run device disconnect lo >/dev/null 2>&1; then
    bad "device accepted loopback"
else
    ok "device refused loopback"
fi
if "$BIN" --dry-run device disconnect enp0s1 | grep -q 'nmcli device disconnect enp0s1'; then
    ok "device builds the nmcli command"
else
    bad "device did not build the expected command"
fi
for bad_act in "up" "delete" "--help"; do
    if "$BIN" --dry-run device "$bad_act" eth0 >/dev/null 2>&1; then
        bad "device accepted action: $bad_act"
    else
        ok "device refused action: $bad_act"
    fi
done

if "$BIN" --dry-run unit enable not-a-unit >/dev/null 2>&1; then
    bad "accepted a name with no unit suffix"
else
    ok "refused a name with no unit suffix"
fi

# probe resolves a connector against the ones that ACTUALLY EXIST rather than
# building a path from the argument, so a traversal attempt cannot become a
# path at all. Checked with --dry-run so nothing is written even when this
# suite runs as root.
for bogus in "nope" "../../etc/passwd" "card1-DP-3/../../.."; do
    if "$BIN" --dry-run probe "$bogus" >/dev/null 2>&1; then
        bad "probe accepted a non-connector: $bogus"
    else
        ok "probe refused: $bogus"
    fi
done

# A real connector must resolve, whatever this machine happens to have. Skipped
# rather than failed on a box with no DRM at all (a VM, a container, the CI
# runner) — the test is that resolution WORKS, not that hardware is present.
real=$(ls /sys/class/drm 2>/dev/null | grep -m1 -- '-' || true)
if [ -n "$real" ]; then
    short=${real#*-}
    if "$BIN" --dry-run probe "$short" | grep -q "would write: detect"; then
        ok "probe resolves $short without writing"
    else
        bad "probe did not resolve the real connector $short"
    fi
else
    ok "no DRM connectors here; probe resolution not exercised"
fi

# ── Bootloaders ─────────────────────────────────────────────────────────────
#
# The reason these are fixture-driven rather than run against the real /boot:
# SynapseOS installs three bootloaders and every machine this project is
# developed on boots exactly one of them. The systemd-boot path shipped broken
# — the loop that was supposed to read loader/entries stopped one index short,
# and the "checked below" case was never written — and no amount of running it
# here would have caught that, because there is no systemd-boot here to run it
# against. SYN_SETTINGS_BOOT_ROOT exists so the suite can pose all three.
bootfx=$(mktemp -d)
trap 'rm -rf "$bootfx"' EXIT

mkdir -p "$bootfx/limine/boot" \
         "$bootfx/grub/boot/grub" \
         "$bootfx/sdb/boot/loader/entries" \
         "$bootfx/bls/boot/loader/entries" \
         "$bootfx/multi/boot/grub" \
         "$bootfx/none/boot"

printf 'timeout: 5\n/+SynapseOS\n //SynapseOS\n kernel_path: boot():/vmlinuz-linux\n' \
    > "$bootfx/limine/boot/limine.conf"
printf 'menuentry "SynapseOS" {\n linux /vmlinuz-linux root=UUID=x\n}\n' \
    > "$bootfx/grub/boot/grub/grub.cfg"
printf 'title SynapseOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\n' \
    > "$bootfx/sdb/boot/loader/entries/synapseos.conf"
printf 'timeout: 5\n' > "$bootfx/multi/boot/limine.conf"
printf 'menuentry x {}\n' > "$bootfx/multi/boot/grub/grub.cfg"

# The pure kernel-install layout. This entry names neither "vmlinuz" nor the
# package — only the kernel RELEASE — so matching on the image filename cannot
# find it at all. It is why the release is carried alongside.
run_rel=$(cat /usr/lib/modules/*/pkgbase >/dev/null 2>&1 && \
          for d in /usr/lib/modules/*/; do
              [ -f "$d/pkgbase" ] && [ "$(cat "$d/pkgbase")" = linux ] && basename "$d"
          done | head -1)
if [ -n "${run_rel:-}" ]; then
    printf 'title SynapseOS\nlinux /b153/%s/linux\ninitrd /b153/%s/initrd\n' \
        "$run_rel" "$run_rel" > "$bootfx/bls/boot/loader/entries/b153-$run_rel.conf"
fi

# ⚠ The running release is PINNED to something no package can own.
#
# "running" takes precedence over the bootable check in the state ladder, so
# on a machine booted into plain `linux` — every box here — the bootable
# answer for `linux` is unobservable, and these assertions had to accept
# "running" as a pass. That made them vacuous exactly where they mattered: the
# prefix trap below could only ever be checked on a machine booted into
# something else, and on this one it FAILED and took the package build with
# it. Pinned, every assertion below means the same thing everywhere.
boot_state() {
    SYN_SETTINGS_BOOT_ROOT="$1" \
    SYN_SETTINGS_RUNNING_RELEASE="0.0.0-synsettings-test" \
        "$BIN" --rec kernel | awk -F'\t' '$1=="linux" {print $3}'
}

# All of it rests on the `linux` package being installed; where it is not,
# every state below is "not installed" and proves nothing. Skipped rather than
# failed, the same way the BLS fixture is.
if [ "$(boot_state "$bootfx/none")" = "not installed" ]; then
    linux_here=0
    ok "boot: no linux package here; bootloader matching not exercised"
else
    linux_here=1
    # The pin must actually have taken, or everything below silently reverts
    # to the coincidence it replaced.
    if [ "$(boot_state "$bootfx/none")" = "running" ]; then
        bad "boot: SYN_SETTINGS_RUNNING_RELEASE was ignored — the checks below are vacuous"
        linux_here=0
    fi
fi

if [ "$linux_here" = 1 ]; then
    for fx in limine grub sdb; do
        if [ "$(boot_state "$bootfx/$fx")" = "installed, bootable" ]; then
            ok "boot: $fx entry naming vmlinuz-linux reads as bootable"
        else
            bad "boot: $fx did not see its own entry (got '$(boot_state "$bootfx/$fx")')"
        fi
    done
fi

# systemd-boot with ONLY a Boot Loader Spec entry. Skipped rather than failed
# where the linux package owns no module tree, since the fixture cannot then be
# written — the test is that release matching works, not that a kernel is here.
if [ -n "${run_rel:-}" ] && [ "$linux_here" = 1 ]; then
    if [ "$(boot_state "$bootfx/bls")" = "installed, bootable" ]; then
        ok "boot: a BLS entry naming only the release reads as bootable"
    else
        bad "boot: BLS entry missed (got '$(boot_state "$bootfx/bls")')"
    fi
else
    ok "boot: no linux module tree here; BLS release matching not exercised"
fi

# THE PREFIX TRAP. "vmlinuz-linux" is a prefix of "vmlinuz-linux-lts", so an
# unanchored search reports the stock kernel bootable on the strength of an LTS
# entry — the pane would say "bootable" about the one kernel that is not.
printf 'menuentry "SynapseOS" {\n linux /vmlinuz-linux-lts root=UUID=x\n}\n' \
    > "$bootfx/grub/boot/grub/grub.cfg"
if [ "$linux_here" != 1 ]; then
    ok "boot: no linux package here; the prefix trap is not exercised"
elif [ "$(boot_state "$bootfx/grub")" = "installed, NO BOOT ENTRY" ]; then
    ok "boot: an LTS-only entry does not make plain linux look bootable"
else
    bad "boot: prefix match leaked (got '$(boot_state "$bootfx/grub")')"
fi

# Nothing configured is an ANSWER, not a crash.
rc=0; SYN_SETTINGS_BOOT_ROOT="$bootfx/none" "$BIN" --rec kernel >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 0 ]; then
    ok "boot: a machine with no bootloader config still renders the pane"
else
    bad "boot: no-bootloader machine failed the pane (exit $rc)"
fi

# ── The confirmation gate ───────────────────────────────────────────────────
#
# The GUI shows a dialogue, but the binary is the boundary. A confirmation that
# only exists in QML is one that anything else can skip, so it is enforced here
# and tested here. Without --confirm nothing may run, and the exit must be 2
# (refused) rather than 0 with a silent no-op.
rc=0
SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot linux >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 2 ]; then
    ok "boot: refuses to change anything without --confirm"
else
    bad "boot: ran without --confirm (exit $rc)"
fi

# And the refusal must SAY what it would have done, or the GUI has nothing to
# put in the dialogue and the user has nothing to approve.
#
# Captured into a variable rather than piped into grep: this command exits 2 on
# purpose, `set -o pipefail` is on, and a pipeline reports the FIRST non-zero
# status — so `"$BIN" boot linux | grep -q x` returns 2 even when grep matched,
# and the check fails for a reason that has nothing to do with the output.
refusal=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot linux 2>&1 || true)
if grep -qF 'grub-mkconfig' <<<"$refusal"; then
    ok "boot: the refusal names the command it would run"
else
    bad "boot: the refusal did not name the command"
fi

# Each bootloader gets ITS OWN mechanism. Getting this wrong means running
# grub-mkconfig on a limine machine, which succeeds and changes nothing.
if SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" -n boot linux \
     | grep -q '^command	pkexec grub-mkconfig -o '; then
    ok "boot: grub gets grub-mkconfig"
else
    bad "boot: wrong mechanism for grub"
fi
if [ -n "${run_rel:-}" ]; then
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/sdb" "$BIN" -n boot linux \
         | grep -q '^command	pkexec kernel-install add '; then
        ok "boot: systemd-boot gets kernel-install"
    else
        bad "boot: wrong mechanism for systemd-boot"
    fi
fi
if SYN_SETTINGS_BOOT_ROOT="$bootfx/limine" "$BIN" -n boot linux \
     | grep -qE '^command	(pkexec limine-update|synpkg( --[a-z]+)* install limine-mkinitcpio-hook)'; then
    ok "boot: limine gets limine-update or the hook that provides it"
else
    bad "boot: wrong mechanism for limine"
fi

# ⚠ --noconfirm is LOAD-BEARING here for the same reason it is on `pkg install`
# — and this is the check that was missing when it was. The pattern above
# tolerates any flags, so it passed happily while "Make bootable" authenticated
# through polkit three times on 2026-08-12 and installed nothing: synpkg's
# confirm() refuses with no terminal to ask in, and a declined transaction was
# a zero exit. Only assert it on the branch that actually invokes synpkg — a
# machine with limine-update already installed builds a pkexec command instead.
bootcmd=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/limine" "$BIN" -n boot linux | grep '^command	')
case "$bootcmd" in
*synpkg*)
    case "$bootcmd" in
    *"--noconfirm"*) ok "boot: the synpkg install passes --noconfirm" ;;
    *) bad "boot: synpkg install dropped --noconfirm — it will install nothing" ;;
    esac
    # Globals must precede the verb; synpkg stops parsing them at the first
    # non-option argument, so "install --noconfirm" is a flag it never sees.
    case "$bootcmd" in
    *"--noconfirm"*install*) ok "boot: --noconfirm comes before the verb" ;;
    *) bad "boot: --noconfirm lands after 'install', where synpkg ignores it" ;;
    esac
    ;;
*)
    skip "boot: limine-update is installed, so no synpkg install to check"
    ;;
esac

# With several bootloaders configured, picking one for the user means picking
# which config file to rewrite. Being wrong there is the failure this pane
# exists to prevent, so it must refuse rather than guess.
rc=0
SYN_SETTINGS_BOOT_ROOT="$bootfx/multi" "$BIN" boot linux --confirm >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 2 ]; then
    ok "boot: refuses to guess between two configured bootloaders"
else
    bad "boot: picked a bootloader on its own (exit $rc)"
fi
if SYN_SETTINGS_BOOT_ROOT="$bootfx/multi" "$BIN" -n boot linux --loader grub \
     | grep -q '^loader	grub'; then
    ok "boot: --loader resolves the ambiguity"
else
    bad "boot: --loader did not select the named bootloader"
fi

# boot is restricted to the kernels this pane manages, exactly as pkg is. A GUI
# row must never become a way to run a boot-config generator against an
# arbitrary name.
for evil in "firefox" "base" "../../etc/passwd" "linux-lts-evil"; do
    rc=0
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot "$evil" --confirm >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 2 ]; then
        ok "boot refused: $evil"
    else
        bad "boot accepted a name outside the kernel list: $evil (exit $rc)"
    fi
done

# ── the window follows the desktop font ─────────────────────────────────────
# This pane is where the font gets PICKED, so it is the one window that must
# never drift from font.state. The file carries the desktop's family AND its
# text scale; it is deliberately not a key in theme.json, because the font
# outlives a theme switch.
#
# Qt resolves an application's default font ONCE at startup, so both have to be
# BINDINGS on every Text. A bare `font.pixelSize: 13` or a literal family is the
# regression, and it fails silently — the window just stops moving with the
# desktop, which is how syn-arsenal and synpkg behaved until 2026-08-11.
QML="$(dirname "$0")/../data/syn-settings.qml"
if [ -f "$QML" ]; then
    grep -q 'config/synui/font.state' "$QML" \
        && ok "the desktop font file is watched" \
        || bad "syn-settings.qml does not read font.state"
    grep -q 'root.textScale = s' "$QML" \
        && ok "the scale is read from the same file" \
        || bad "syn-settings.qml reads the family but not the scale"

    # awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches,
    # and under pipefail a correct zero would be read as a failed check.
    n=$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "no pixel size bypasses ui()" \
                 || bad "$n pixel size(s) bypass ui()"

    n=$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every literal family is the deliberate monospace" \
                 || bad "$n literal font family/families are not monospace"

    # Qt.application.font.family is the STARTUP font: as a fallback it freezes
    # the very thing this is fixing.
    n=$(grep -c 'Qt.application.font' "$QML" || true)
    [ "$n" = 0 ] && ok "no fallback pins the startup font" \
                 || bad "$n use(s) of Qt.application.font"
else
    bad "syn-settings.qml not found beside the tests: $QML"
fi

if [ "$fails" -gt 0 ]; then
    printf '\n%d test(s) failed\n' "$fails"
    exit 1
fi
printf '\nall tests passed\n'
